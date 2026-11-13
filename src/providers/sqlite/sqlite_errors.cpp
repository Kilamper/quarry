#include "sqlite_errors.h"

#include "sqlite_text.h"

#include <QRegularExpression>

namespace quarry::sqlite {

namespace {

struct CodeName
{
    int code;
    const char *name;
};

constexpr CodeName kCodeNames[] = {
    { SQLITE_ERROR, "SQLITE_ERROR" },
    { SQLITE_INTERNAL, "SQLITE_INTERNAL" },
    { SQLITE_PERM, "SQLITE_PERM" },
    { SQLITE_ABORT, "SQLITE_ABORT" },
    { SQLITE_BUSY, "SQLITE_BUSY" },
    { SQLITE_LOCKED, "SQLITE_LOCKED" },
    { SQLITE_NOMEM, "SQLITE_NOMEM" },
    { SQLITE_READONLY, "SQLITE_READONLY" },
    { SQLITE_INTERRUPT, "SQLITE_INTERRUPT" },
    { SQLITE_IOERR, "SQLITE_IOERR" },
    { SQLITE_CORRUPT, "SQLITE_CORRUPT" },
    { SQLITE_FULL, "SQLITE_FULL" },
    { SQLITE_CANTOPEN, "SQLITE_CANTOPEN" },
    { SQLITE_PROTOCOL, "SQLITE_PROTOCOL" },
    { SQLITE_SCHEMA, "SQLITE_SCHEMA" },
    { SQLITE_TOOBIG, "SQLITE_TOOBIG" },
    { SQLITE_CONSTRAINT, "SQLITE_CONSTRAINT" },
    { SQLITE_MISMATCH, "SQLITE_MISMATCH" },
    { SQLITE_MISUSE, "SQLITE_MISUSE" },
    { SQLITE_AUTH, "SQLITE_AUTH" },
    { SQLITE_RANGE, "SQLITE_RANGE" },
    { SQLITE_NOTADB, "SQLITE_NOTADB" },
    { SQLITE_BUSY_RECOVERY, "SQLITE_BUSY_RECOVERY" },
    { SQLITE_BUSY_SNAPSHOT, "SQLITE_BUSY_SNAPSHOT" },
    { SQLITE_BUSY_TIMEOUT, "SQLITE_BUSY_TIMEOUT" },
    { SQLITE_LOCKED_SHAREDCACHE, "SQLITE_LOCKED_SHAREDCACHE" },
    { SQLITE_READONLY_DBMOVED, "SQLITE_READONLY_DBMOVED" },
    { SQLITE_CANTOPEN_NOTEMPDIR, "SQLITE_CANTOPEN_NOTEMPDIR" },
    { SQLITE_CANTOPEN_ISDIR, "SQLITE_CANTOPEN_ISDIR" },
    { SQLITE_CANTOPEN_FULLPATH, "SQLITE_CANTOPEN_FULLPATH" },
    { SQLITE_CONSTRAINT_CHECK, "SQLITE_CONSTRAINT_CHECK" },
    { SQLITE_CONSTRAINT_COMMITHOOK, "SQLITE_CONSTRAINT_COMMITHOOK" },
    { SQLITE_CONSTRAINT_FOREIGNKEY, "SQLITE_CONSTRAINT_FOREIGNKEY" },
    { SQLITE_CONSTRAINT_FUNCTION, "SQLITE_CONSTRAINT_FUNCTION" },
    { SQLITE_CONSTRAINT_NOTNULL, "SQLITE_CONSTRAINT_NOTNULL" },
    { SQLITE_CONSTRAINT_PRIMARYKEY, "SQLITE_CONSTRAINT_PRIMARYKEY" },
    { SQLITE_CONSTRAINT_TRIGGER, "SQLITE_CONSTRAINT_TRIGGER" },
    { SQLITE_CONSTRAINT_UNIQUE, "SQLITE_CONSTRAINT_UNIQUE" },
    { SQLITE_CONSTRAINT_ROWID, "SQLITE_CONSTRAINT_ROWID" },
};

QString codeName(int code)
{
    for (const auto &entry : kCodeNames) {
        if (entry.code == code)
            return QString::fromLatin1(entry.name);
    }
    return QString::number(code);
}

ErrorKind kindFor(int primary)
{
    switch (primary) {
    case SQLITE_CONSTRAINT:
        return ErrorKind::Constraint;
    case SQLITE_AUTH:
    case SQLITE_PERM:
    case SQLITE_READONLY:
        return ErrorKind::Permission;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return ErrorKind::Timeout;
    case SQLITE_CANTOPEN:
    case SQLITE_NOTADB:
        return ErrorKind::Connection;
    case SQLITE_INTERRUPT:
        return ErrorKind::Cancelled;
    case SQLITE_MISUSE:
    case SQLITE_INTERNAL:
        return ErrorKind::Internal;
    default:
        return ErrorKind::Driver;
    }
}

QString constraintMessage(int extended)
{
    switch (extended) {
    case SQLITE_CONSTRAINT_UNIQUE:
    case SQLITE_CONSTRAINT_PRIMARYKEY:
    case SQLITE_CONSTRAINT_ROWID:
        return Text::tr("A unique or primary key constraint was violated.");
    case SQLITE_CONSTRAINT_NOTNULL:
        return Text::tr("A required (NOT NULL) value is missing.");
    case SQLITE_CONSTRAINT_FOREIGNKEY:
        return Text::tr("A foreign key constraint was violated.");
    case SQLITE_CONSTRAINT_CHECK:
        return Text::tr("A CHECK constraint was violated.");
    default:
        return Text::tr("A constraint was violated.");
    }
}

QString messageFor(ErrorKind kind, int extended)
{
    switch (kind) {
    case ErrorKind::Constraint:
        return constraintMessage(extended);
    case ErrorKind::Permission:
        return (extended & 0xff) == SQLITE_READONLY
            ? Text::tr("The database is read-only.")
            : Text::tr("You do not have permission to do that.");
    case ErrorKind::Timeout:
        return Text::tr("The database is locked by another connection.");
    case ErrorKind::Connection:
        return (extended & 0xff) == SQLITE_NOTADB
            ? Text::tr("The file is not a SQLite database.")
            : Text::tr("The database file could not be opened.");
    case ErrorKind::Cancelled:
        return Text::tr("The operation was cancelled.");
    case ErrorKind::Internal:
        return Text::tr("An internal error occurred in the SQLite provider.");
    default:
        return Text::tr("The database reported an error.");
    }
}

int utf16Position(const QByteArray &sql, int byteOffset)
{
    return static_cast<int>(QString::fromUtf8(sql.constData(), byteOffset).size());
}

} // namespace

Error cancelledError()
{
    return Error { ErrorKind::Cancelled,
                   messageFor(ErrorKind::Cancelled, 0),
                   QStringLiteral("SQLITE_INTERRUPT"),
                   {},
                   {} };
}

Error makeError(sqlite3 *db, int rc, const QByteArray &sql, bool preparing)
{
    const int extended = db ? sqlite3_extended_errcode(db) : rc;
    const QString driverText = db ? QString::fromUtf8(sqlite3_errmsg(db)) : QString();
    Error error;
    error.kind = kindFor(extended & 0xff);
    error.detail = QStringLiteral("%1 (%2)").arg(driverText, codeName(extended));
    error.message = messageFor(error.kind, extended);

    if (preparing && error.kind == ErrorKind::Driver && (extended & 0xff) == SQLITE_ERROR) {
        static const QRegularExpression near(QStringLiteral("^near \"(.*)\": syntax error$"),
                                             QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression missing(
            QStringLiteral("^no such (table|column|index|view|function|collation sequence|module"
                           "|trigger): (.+)$"));
        if (const auto m = near.match(driverText); m.hasMatch()) {
            error.kind = ErrorKind::Syntax;
            error.message = Text::tr("There is a syntax error near \"%1\".").arg(m.captured(1));
        } else if (driverText.contains(QLatin1String("syntax error"))
                   || driverText.contains(QLatin1String("incomplete input"))) {
            error.kind = ErrorKind::Syntax;
            error.message = driverText.contains(QLatin1String("incomplete input"))
                ? Text::tr("The statement is incomplete.")
                : Text::tr("There is a syntax error in the statement.");
        } else if (const auto m2 = missing.match(driverText); m2.hasMatch()) {
            error.kind = ErrorKind::NotFound;
            error.message
                = Text::tr("The %1 \"%2\" does not exist.").arg(m2.captured(1), m2.captured(2));
        } else {
            error.message = Text::tr("The database rejected the statement.");
        }
    }
    if (preparing && db) {
        const int offset = sqlite3_error_offset(db);
        if (offset >= 0 && offset <= sql.size())
            error.position = utf16Position(sql, offset);
    }
    return error;
}

} // namespace quarry::sqlite
