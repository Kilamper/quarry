#pragma once

#include <QCoreApplication>

namespace quarry::sqlite {

// Translation context for every user-visible string of this provider.
struct Text
{
    Q_DECLARE_TR_FUNCTIONS(quarry::sqlite)
};

} // namespace quarry::sqlite
