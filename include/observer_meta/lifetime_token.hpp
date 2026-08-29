#pragma once

#include "connection.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace observer_meta
{
class LifetimeToken
{
public:
    LifetimeToken()
        : state_(std::make_shared<State>())
    {
    }

    LifetimeToken(const LifetimeToken&) = delete;
    LifetimeToken& operator=(const LifetimeToken&) = delete;
    LifetimeToken(LifetimeToken&&) noexcept = default;
    LifetimeToken& operator=(LifetimeToken&&) noexcept = default;

    ~LifetimeToken() = default;

    void reset() noexcept
    {
        auto state = std::exchange(state_, {});
        if (state)
        {
            state->invalidate();
        }
    }

    // Creates a fresh generation. Handles and connections associated with the
    // previous generation stay invalid and cannot affect the renewed owner.
    void renew()
    {
        reset();
        state_ = std::make_shared<State>();
    }

    [[nodiscard]] bool alive() const noexcept
    {
        return state_ && state_->alive();
    }

    [[nodiscard]] std::weak_ptr<const void> weak_handle() const noexcept
    {
        return state_;
    }

    // Registers teardown that must run while the lifetime owner is still
    // loaded. Cleanup is invoked immediately when the token is already dead.
    void add_cleanup(std::function<void()> cleanup) const
    {
        if (!cleanup)
        {
            return;
        }

        const auto state = state_;
        if (!state || !state->add(cleanup))
        {
            invoke_noexcept(cleanup);
        }
    }

    // Connections tracked by a lifetime are eagerly disconnected on reset;
    // expiry is not deferred until a future broadcast.
    void track(Connection connection) const
    {
        const auto state = state_;
        if (!state || !state->add_connection(connection))
        {
            connection.disconnect();
        }
    }

private:
    struct State final
    {
        ~State()
        {
            invalidate();
        }

        [[nodiscard]] bool alive() const noexcept
        {
            std::lock_guard lock(mutex);
            return alive_flag;
        }

        [[nodiscard]] bool add(const std::function<void()>& cleanup)
        {
            std::lock_guard lock(mutex);
            if (!alive_flag)
            {
                return false;
            }
            cleanups.push_back(cleanup);
            return true;
        }

        [[nodiscard]] bool add_connection(const Connection& connection)
        {
            std::lock_guard lock(mutex);
            if (!alive_flag)
            {
                return false;
            }
            std::erase_if(connections, [](const Connection& existing)
            {
                return !existing.connected();
            });
            connections.push_back(connection);
            return true;
        }

        void invalidate() noexcept
        {
            std::vector<std::function<void()>> pending;
            std::vector<Connection> pending_connections;
            {
                std::lock_guard lock(mutex);
                if (!alive_flag)
                {
                    return;
                }
                alive_flag = false;
                pending.swap(cleanups);
                pending_connections.swap(connections);
            }

            for (auto iterator = pending_connections.rbegin();
                 iterator != pending_connections.rend(); ++iterator)
            {
                iterator->disconnect();
            }

            for (auto iterator = pending.rbegin(); iterator != pending.rend(); ++iterator)
            {
                invoke_noexcept(*iterator);
            }
        }

        mutable std::mutex mutex;
        std::vector<std::function<void()>> cleanups;
        std::vector<Connection> connections;
        bool alive_flag = true;
    };

    static void invoke_noexcept(const std::function<void()>& cleanup) noexcept
    {
        try
        {
            cleanup();
        }
        catch (...)
        {
        }
    }

    std::shared_ptr<State> state_;
};
} // namespace observer_meta
