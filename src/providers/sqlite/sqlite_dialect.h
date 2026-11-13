#pragma once

#include <quarry/provider_api.h>

namespace quarry::sqlite {

class SqliteDialect : public ISqlDialect
{
public:
    QString quoteIdentifier(QStringView name) const override;
    QString quoteLiteral(QStringView text) const override;
    QString highlightingDefinition() const override;
    QStringList keywords() const override;
    // Ranges are half-open, trimmed of surrounding whitespace, exclude the terminating ';',
    // and skip fragments with nothing but comments/whitespace.
    std::vector<StatementRange> splitStatements(QStringView script) const override;
    QString limitClause(qint64 limit, qint64 offset) const override;
    QString explainPrefix(ExplainMode mode) const override;
};

} // namespace quarry::sqlite
