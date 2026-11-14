#pragma once

#include <quarry/result.h>

#include <QString>

struct sqlite3;

namespace quarry {

// The one place that opens quarry.db and owns its migrations. Each store worker owns one
// instance, used only from that worker's thread. Opening is safe while another thread or process
// opens the same file: the version check and the migrations run under BEGIN IMMEDIATE.
class AppDatabase
{
public:
    explicit AppDatabase(QString path);
    ~AppDatabase();
    AppDatabase(const AppDatabase &) = delete;
    AppDatabase &operator=(const AppDatabase &) = delete;

    // Opens (creating folder and file), sets pragmas and migrates. No-op if already open.
    bool open(Error &error);
    void close();
    sqlite3 *handle() const { return m_db; }

    bool exec(const char *sql, Error &error);
    // Error for a failed SQLite call; `context` goes to `detail`.
    Error error(int rc, const QString &context) const;

    static constexpr int kLatestVersion = 2;

private:
    bool migrate(Error &error);
    bool readVersion(int &version, Error &error);

    QString m_path;
    sqlite3 *m_db = nullptr;
};

} // namespace quarry
