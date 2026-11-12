#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>
#include <type_traits>
#include <utility>

namespace quarry {

namespace detail {
// Wrong access is a programming error: fail loudly in release too (Q_ASSERT would vanish).
[[noreturn]] inline void badAccess(const char *what)
{
    qFatal("quarry::Result misuse: %s", what);
}
} // namespace detail

enum class ErrorKind : uint8_t {
    Connection,
    Authentication,
    Timeout,
    Cancelled,
    Syntax,
    Permission,
    Constraint,
    NotFound,
    Unsupported,
    Driver,
    Internal
};

struct Error
{
    ErrorKind kind = ErrorKind::Internal;
    QString message; // plain, translatable language
    QString detail; // technical: literal driver text
    QString sqlState; // "28P01", if any
    std::optional<int> position; // position in the SQL, for editor underlining
};

// Wrapper that selects the error constructor of Result (like std::unexpected).
struct Unexpected
{
    Error error;
};

inline Unexpected fail(Error error)
{
    return Unexpected { std::move(error) };
}

// Same shape as std::expected<T, Error>, so it can be swapped out with C++23.
// Wrong access asserts (debug only); no exceptions.
template<typename T> class Result
{
public:
    Result(T value)
        : m_value(std::move(value))
    { }
    // Lets `return std::make_unique<Derived>()` produce a Result<unique_ptr<Base>>.
    template<typename U>
        requires(!std::is_same_v<std::remove_cvref_t<U>, T>
                 && !std::is_same_v<std::remove_cvref_t<U>, Result>
                 && !std::is_same_v<std::remove_cvref_t<U>, Unexpected>
                 && std::is_constructible_v<T, U &&>)
    Result(U &&value)
        : m_value(std::in_place, std::forward<U>(value))
    { }
    Result(Unexpected failure)
        : m_error(std::move(failure.error))
    { }

    bool has_value() const noexcept { return m_value.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }

    T &value() &
    {
        if (!m_value)
            detail::badAccess("Result::value: no value");
        return *m_value;
    }
    const T &value() const &
    {
        if (!m_value)
            detail::badAccess("Result::value: no value");
        return *m_value;
    }
    T &&value() &&
    {
        if (!m_value)
            detail::badAccess("Result::value: no value");
        return std::move(*m_value);
    }
    const Error &error() const &
    {
        if (has_value())
            detail::badAccess("Result::error: no error");
        return m_error;
    }
    Error &&error() &&
    {
        if (has_value())
            detail::badAccess("Result::error: no error");
        return std::move(m_error);
    }

    T &operator*() & { return value(); }
    const T &operator*() const & { return value(); }
    T &&operator*() && { return std::move(*this).value(); }
    T *operator->() { return &value(); }
    const T *operator->() const { return &value(); }

private:
    std::optional<T> m_value;
    Error m_error;
};

template<> class Result<void>
{
public:
    Result() = default;
    Result(Unexpected failure)
        : m_error(std::move(failure.error))
    { }

    bool has_value() const noexcept { return !m_error.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }
    void value() const
    {
        if (!has_value())
            detail::badAccess("Result::value: no value");
    }
    const Error &error() const &
    {
        if (!m_error)
            detail::badAccess("Result::error: no error");
        return *m_error;
    }
    Error &&error() &&
    {
        if (!m_error)
            detail::badAccess("Result::error: no error");
        return std::move(*m_error);
    }

private:
    std::optional<Error> m_error;
};

} // namespace quarry
