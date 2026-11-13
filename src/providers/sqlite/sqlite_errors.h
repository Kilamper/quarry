#pragma once

#include <quarry/result.h>

#include <QByteArray>

#include <sqlite3.h>

namespace quarry::sqlite {

// Builds an Error from the state of `db` after a failed call returning `rc`. When `preparing`
// is true, `sql` (UTF-8, as passed to sqlite3_prepare) is used to turn the byte offset of the
// error into a UTF-16 position.
Error makeError(sqlite3 *db, int rc, const QByteArray &sql = {}, bool preparing = false);

Error cancelledError();

} // namespace quarry::sqlite
