#pragma once

#include <atomic>
#include <memory>

namespace quarry {

// Read side of a cooperative cancellation flag. Copies share the same flag.
// A default-constructed token is never cancelled.
class CancellationToken
{
public:
    CancellationToken() = default;
    bool isCancelled() const noexcept { return m_flag && m_flag->load(std::memory_order_acquire); }

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> flag)
        : m_flag(std::move(flag))
    { }
    std::shared_ptr<std::atomic<bool>> m_flag;
};

class CancellationSource
{
public:
    CancellationSource()
        : m_flag(std::make_shared<std::atomic<bool>>(false))
    { }
    CancellationToken token() const { return CancellationToken(m_flag); }
    void cancel() noexcept { m_flag->store(true, std::memory_order_release); }
    // Also un-cancels tokens already handed out.
    void reset() noexcept { m_flag->store(false, std::memory_order_release); }
    bool isCancelled() const noexcept { return m_flag->load(std::memory_order_acquire); }

private:
    std::shared_ptr<std::atomic<bool>> m_flag;
};

} // namespace quarry
