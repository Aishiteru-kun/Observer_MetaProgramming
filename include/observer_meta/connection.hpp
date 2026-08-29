#pragma once

#include <cstdint>
#include <memory>
#include <utility>

namespace observer_meta
{
namespace detail
{
class ConnectionState
{
public:
    virtual ~ConnectionState() = default;

    virtual void disconnect(std::uint64_t id) noexcept = 0;
    [[nodiscard]] virtual bool connected(std::uint64_t id) const noexcept = 0;
};
} // namespace detail

class Connection
{
public:
    Connection() noexcept = default;

    void disconnect() const noexcept
    {
        if (const auto state = state_.lock(); state && id_ != 0)
        {
            state->disconnect(id_);
        }
    }

    [[nodiscard]] bool connected() const noexcept
    {
        if (const auto state = state_.lock(); state && id_ != 0)
        {
            return state->connected(id_);
        }
        return false;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return connected();
    }

    friend bool operator==(const Connection& lhs, const Connection& rhs) noexcept
    {
        if (lhs.id_ != rhs.id_)
        {
            return false;
        }

        return !lhs.state_.owner_before(rhs.state_) && !rhs.state_.owner_before(lhs.state_);
    }

private:
    template <typename, typename>
    friend class BasicSignal;

    Connection(std::weak_ptr<detail::ConnectionState> state, const std::uint64_t id) noexcept
        : state_(std::move(state)), id_(id)
    {
    }

    std::weak_ptr<detail::ConnectionState> state_;
    std::uint64_t id_ = 0;
};

class ScopedConnection
{
public:
    ScopedConnection() noexcept = default;

    explicit ScopedConnection(Connection connection) noexcept
        : connection_(std::move(connection))
    {
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept
        : connection_(std::exchange(other.connection_, {}))
    {
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept
    {
        if (this != &other)
        {
            disconnect();
            connection_ = std::exchange(other.connection_, {});
        }
        return *this;
    }

    ~ScopedConnection()
    {
        disconnect();
    }

    void disconnect() noexcept
    {
        connection_.disconnect();
        connection_ = {};
    }

    void reset(Connection connection = {}) noexcept
    {
        disconnect();
        connection_ = std::move(connection);
    }

    [[nodiscard]] Connection release() noexcept
    {
        return std::exchange(connection_, {});
    }

    [[nodiscard]] bool connected() const noexcept
    {
        return connection_.connected();
    }

    [[nodiscard]] const Connection& connection() const noexcept
    {
        return connection_;
    }

private:
    Connection connection_;
};
} // namespace observer_meta
