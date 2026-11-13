// Core streaming benchmark: 1M rows x 10 columns from a SQLite file, through the real plugin,
// in batches of 500. Fixture generation is excluded from the timing.
#include <quarry/provider_registry.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <sqlite3.h>
#include <vector>

#ifdef Q_OS_WIN
// clang-format off
#  define NOMINMAX
#  include <windows.h>
#  include <psapi.h>
// clang-format on
#elif defined(Q_OS_MACOS)
#include <sys/resource.h>
#else
#include <cstdlib>
#include <cstring>
#endif

namespace {

constexpr int kBatch = 500;
constexpr int kRuns = 3;

double peakRssMb()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return -1;
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#elif defined(Q_OS_MACOS)
    rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0); // bytes on macOS
#else
    FILE *file = std::fopen("/proc/self/status", "r");
    if (!file)
        return -1;
    char line[256];
    double kb = -1;
    while (std::fgets(line, sizeof(line), file)) {
        if (std::strncmp(line, "VmHWM:", 6) == 0) {
            kb = std::atof(line + 6);
            break;
        }
    }
    std::fclose(file);
    return kb / 1024.0;
#endif
}

bool check(int rc, sqlite3 *db)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::fprintf(stderr, "sqlite error: %s\n", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

// Deterministic: fixed seed. 3 int64, 2 double, 3 text (20-50 bytes), 1 ISO timestamp,
// 1 nullable text with ~10% NULLs.
bool generate(const QString &path, int rows)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK)
        return false;
    bool ok = check(sqlite3_exec(db,
                                 "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;"
                                 "CREATE TABLE t(i1 INTEGER, i2 INTEGER, i3 INTEGER, d1 REAL, d2 "
                                 "REAL, s1 TEXT, s2 TEXT, s3 TEXT, ts TEXT, n TEXT);"
                                 "BEGIN",
                                 nullptr, nullptr, nullptr),
                    db);
    sqlite3_stmt *stmt = nullptr;
    ok = ok
        && check(sqlite3_prepare_v2(db, "INSERT INTO t VALUES(?,?,?,?,?,?,?,?,?,?)", -1, &stmt,
                                    nullptr),
                 db);
    std::mt19937_64 rng(42);
    static const char kAlphabet[]
        = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    auto randomText = [&](char *buffer) {
        const int length = 20 + static_cast<int>(rng() % 31);
        for (int i = 0; i < length; ++i)
            buffer[i] = kAlphabet[rng() % (sizeof(kAlphabet) - 1)];
        return length;
    };
    char text[64];
    char stamp[32];
    for (int row = 0; ok && row < rows; ++row) {
        sqlite3_bind_int64(stmt, 1, row);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(rng() % 1000000));
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(rng()));
        sqlite3_bind_double(stmt, 4, static_cast<double>(rng() % 1000000) / 100.0);
        sqlite3_bind_double(stmt, 5, static_cast<double>(rng() % 1000000000) / 7.0);
        for (int c = 6; c <= 8; ++c) {
            const int length = randomText(text);
            sqlite3_bind_text(stmt, c, text, length, SQLITE_TRANSIENT);
        }
        const unsigned secs = static_cast<unsigned>(rng() % (std::uint64_t(86400) * 365 * 5));
        std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                      2020 + secs / 31536000U, 1 + (secs / 86400U) % 12, 1 + (secs / 3600U) % 28,
                      (secs / 3600U) % 24, (secs / 60U) % 60, secs % 60);
        sqlite3_bind_text(stmt, 9, stamp, -1, SQLITE_TRANSIENT);
        if (rng() % 10 == 0) {
            sqlite3_bind_null(stmt, 10);
        } else {
            const int length = randomText(text);
            sqlite3_bind_text(stmt, 10, text, length, SQLITE_TRANSIENT);
        }
        ok = check(sqlite3_step(stmt), db);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    ok = ok && check(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr), db);
    sqlite3_close(db);
    return ok;
}

} // namespace

int run(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int rows = argc > 1 ? std::max(1, QString::fromLocal8Bit(argv[1]).toInt()) : 1'000'000;

    QTemporaryDir dir;
    if (!dir.isValid())
        return 2;
    const QString file = dir.filePath(QStringLiteral("bench.sqlite"));
    QElapsedTimer clock;
    clock.start();
    if (!generate(file, rows)) {
        std::fprintf(stderr, "fixture generation failed\n");
        return 2;
    }
    std::fprintf(stderr, "fixture: %d rows in %lld ms (excluded)\n", rows,
                 static_cast<long long>(clock.elapsed()));

    quarry::ProviderRegistry registry;
    registry.scan(QStringLiteral(QUARRY_PLUGIN_DIR));
    QString error;
    quarry::IDatabaseProvider *provider = registry.provider(QStringLiteral("sqlite"), &error);
    if (!provider) {
        std::fprintf(stderr, "cannot load the sqlite plugin: %s\n", qPrintable(error));
        return 2;
    }

    std::vector<qint64> times;
    qint64 checksum = 0;
    for (int run = 0; run < kRuns; ++run) {
        quarry::ConnectionConfig config;
        config.values.insert(QStringLiteral("file"), file);
        config.values.insert(QStringLiteral("readOnly"), true);
        auto connection = provider->createConnection(config, {});
        if (!connection->open({})) {
            std::fprintf(stderr, "open failed\n");
            return 2;
        }
        clock.start();
        auto stream = connection->execute({ QStringLiteral("SELECT * FROM t"), {}, kBatch }, {});
        if (!stream) {
            std::fprintf(stderr, "execute failed: %s\n", qPrintable(stream.error().detail));
            return 2;
        }
        qint64 total = 0;
        checksum = 0;
        while (!stream.value()->atEnd()) {
            auto batch = stream.value()->fetch(kBatch, {});
            if (!batch) {
                std::fprintf(stderr, "fetch failed: %s\n", qPrintable(batch.error().detail));
                return 2;
            }
            total += batch.value().rowCount;
            if (batch.value().rowCount > 0)
                checksum += batch.value().columns[2].int64At(batch.value().rowCount - 1);
        }
        times.push_back(clock.elapsed());
        if (total != rows) {
            std::fprintf(stderr, "expected %d rows, got %lld\n", rows,
                         static_cast<long long>(total));
            return 1;
        }
    }
    std::sort(times.begin(), times.end());
    const qint64 median = times[times.size() / 2];
    const double rss = peakRssMb();
    QTextStream out(stdout);
    out << "bench_core: " << rows << " rows x 10 cols, batch " << kBatch << ", median of " << kRuns
        << " runs = " << median << " ms ("
        << static_cast<qint64>(rows * 1000.0 / double(std::max<qint64>(median, 1)))
        << " rows/s), best " << times.front() << " ms, peak RSS " << rss << " MB, checksum "
        << checksum << "\n";
    out << "{\"metric\":\"core_stream_1m_ms\",\"value\":" << median << ",\"unit\":\"ms\"}\n";
    out << "{\"metric\":\"core_stream_1m_peak_rss_mb\",\"value\":" << qRound(rss * 10) / 10.0
        << ",\"unit\":\"MB\"}\n";
    return 0;
}

int main(int argc, char **argv)
{
    try {
        return run(argc, argv);
    } catch (...) {
        std::fprintf(stderr, "bench_core: unexpected exception\n");
        return 2;
    }
}
