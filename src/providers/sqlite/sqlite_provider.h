#pragma once

#include "sqlite_dialect.h"

#include <quarry/provider_api.h>

namespace quarry::sqlite {

class SqliteProvider : public IDatabaseProvider
{
public:
    ProviderInfo info() const override;
    std::vector<ConnectionField> connectionFields() const override;
    const ISqlDialect &dialect() const override { return m_dialect; }
    std::unique_ptr<IConnection> createConnection(const ConnectionConfig &config,
                                                  const Secrets &secrets) const override;

private:
    SqliteDialect m_dialect;
};

} // namespace quarry::sqlite
