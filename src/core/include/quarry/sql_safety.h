#pragma once

#include <QStringView>

namespace quarry {

enum class SqlRisk {
    None,
    UpdateWithoutWhere, // any connection
    DeleteWithoutWhere, // any connection
    Drop, // only if production
    Truncate, // only if production
};

class SqlSafety
{
public:
    // One statement as produced by splitStatements(). Only the main command counts: after a WITH
    // (CTE) it skips to the command; DELETE/UPDATE need a WHERE at parenthesis depth 0 after the
    // command (a WHERE inside a CTE or a subquery does not count). Anything inside comments,
    // strings and quoted identifiers is ignored. DROP/TRUNCATE count only as the main command (not
    // `ALTER TABLE ... DROP COLUMN`).
    static SqlRisk check(QStringView statement, bool production);
};

} // namespace quarry
