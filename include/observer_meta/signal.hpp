#pragma once

#include "connection.hpp"
#include "lifetime_token.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace observer_meta
{
namespace collector
{
template <typename Value>
class Vector
{
public:
    using result_type = std::vector<Value>;

    void add(Value value)
    {
        values_.push_back(std::move(value));
    }

    [[nodiscard]] result_type finish() &&
    {
        return std::move(values_);
    }

private:
    result_type values_;
};

template <typename Value>
class LastValue
{
public:
    using result_type = std::optional<Value>;

    void add(Value value)
    {
        value_ = std::move(value);
    }

    [[nodiscard]] result_type finish() &&
    {
        return std::move(value_);
    }

private:
    result_type value_;
};
} // namespace collector

namespace detail
{
struct VoidSignalPolicy final
{
};

template <typename... Args>
inline constexpr bool multicast_arguments_are_reusable =
    ((!std::is_rvalue_reference_v<Args>
      && (std::is_reference_v<Args> || std::is_copy_constructible_v<std::remove_cvref_t<Args>>))
     && ...);
} // namespace detail

template <typename Signature, typename Policy>
class BasicSignal;

template <typename Signature>
using Signal = BasicSignal<Signature, detail::VoidSignalPolicy>;

template <typename Signature, typename Collector>
using CollectingSignal = BasicSignal<Signature, Collector>;

template <typename... Args>
class BasicSignal<void(Args...), detail::VoidSignalPolicy>
{
    static_assert(detail::multicast_arguments_are_reusable<Args...>,
                  "Multicast value arguments must be copyable and rvalue-reference arguments are unsupported");

    struct Slot final
    {
        std::uint64_t id = 0;
        bool active = true;
        std::function<bool(Args...)> invoke;
        std::function<bool()> alive;
    };

    class State final : public detail::ConnectionState
    {
    public:
        [[nodiscard]] std::uint64_t add(Slot slot)
        {
            if (slot.alive && !slot.alive())
            {
                return 0;
            }
            slot.id = allocate_id();
            const auto id = slot.id;
            slots.push_back(std::move(slot));
            return id;
        }

        void disconnect(const std::uint64_t id) noexcept override
        {
            if (id == 0)
            {
                return;
            }

            for (auto& slot : slots)
            {
                if (slot.id == id)
                {
                    slot.active = false;
                    break;
                }
            }

            compact_if_idle();
        }

        [[nodiscard]] bool connected(const std::uint64_t id) const noexcept override
        {
            const auto iterator = std::find_if(slots.begin(), slots.end(), [id](const Slot& slot)
            {
                return slot.id == id;
            });
            return iterator != slots.end() && iterator->active && (!iterator->alive || iterator->alive());
        }

        void clear() noexcept
        {
            if (dispatch_depth == 0)
            {
                slots.clear();
                return;
            }

            for (auto& slot : slots)
            {
                slot.active = false;
            }
        }

        [[nodiscard]] std::size_t live_size() const
        {
            return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(), [](const Slot& slot)
            {
                return slot.active && (!slot.alive || slot.alive());
            }));
        }

        void begin_dispatch() noexcept
        {
            ++dispatch_depth;
        }

        void end_dispatch() noexcept
        {
            --dispatch_depth;
            compact_if_idle();
        }

        std::vector<Slot> slots;

    private:
        [[nodiscard]] std::uint64_t allocate_id()
        {
            for (;;)
            {
                const auto candidate = next_id++;
                if (next_id == 0)
                {
                    next_id = 1;
                }

                if (candidate != 0 && std::none_of(slots.begin(), slots.end(), [candidate](const Slot& slot)
                    {
                        return slot.id == candidate;
                    }))
                {
                    return candidate;
                }
            }
        }

        void compact_if_idle() noexcept
        {
            if (dispatch_depth != 0)
            {
                return;
            }

            std::erase_if(slots, [](const Slot& slot)
            {
                return !slot.active || (slot.alive && !slot.alive());
            });
        }

        std::uint64_t next_id = 1;
        std::size_t dispatch_depth = 0;
    };

    class DispatchScope final
    {
    public:
        explicit DispatchScope(std::shared_ptr<State> state) noexcept
            : state_(std::move(state))
        {
            state_->begin_dispatch();
        }

        ~DispatchScope()
        {
            state_->end_dispatch();
        }

    private:
        std::shared_ptr<State> state_;
    };

public:
    BasicSignal()
        : state_(std::make_shared<State>())
    {
    }

    BasicSignal(const BasicSignal&) = delete;
    BasicSignal& operator=(const BasicSignal&) = delete;
    BasicSignal(BasicSignal&&) noexcept = default;
    BasicSignal& operator=(BasicSignal&&) noexcept = default;

    template <typename Callable>
        requires std::is_invocable_r_v<void, Callable&, Args...>
              && std::copy_constructible<std::decay_t<Callable>>
    Connection connect(Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        Slot slot;
        slot.invoke = [function = Stored(std::forward<Callable>(callable))](Args... args) mutable
        {
            std::invoke(function, std::forward<Args>(args)...);
            return true;
        };
        return add(std::move(slot));
    }

    template <typename Callable>
        requires std::is_invocable_r_v<void, Callable&, Args...>
              && std::copy_constructible<std::decay_t<Callable>>
    Connection connect(const LifetimeToken& lifetime, Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        auto weak = lifetime.weak_handle();
        Slot slot;
        slot.invoke = [weak, function = Stored(std::forward<Callable>(callable))](Args... args) mutable
        {
            const auto guard = weak.lock();
            if (!guard)
            {
                return false;
            }
            std::invoke(function, std::forward<Args>(args)...);
            return true;
        };
        slot.alive = [weak] { return !weak.expired(); };
        auto connection = add(std::move(slot));
        try
        {
            lifetime.track(connection);
        }
        catch (...)
        {
            connection.disconnect();
            throw;
        }
        return connection;
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<void, Method, Object&, Args...>
    Connection connect_weak(std::weak_ptr<Object> object, Method method)
    {
        Slot slot;
        slot.invoke = [object, method](Args... args) mutable
        {
            const auto locked = object.lock();
            if (!locked)
            {
                return false;
            }
            std::invoke(method, *locked, std::forward<Args>(args)...);
            return true;
        };
        slot.alive = [object] { return !object.expired(); };
        return add(std::move(slot));
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<void, Method, Object&, Args...>
    Connection connect_shared(std::shared_ptr<Object> object, Method method)
    {
        const bool has_object = static_cast<bool>(object);
        Slot slot;
        slot.invoke = [object = std::move(object), method](Args... args) mutable
        {
            if (!object)
            {
                return false;
            }
            std::invoke(method, *object, std::forward<Args>(args)...);
            return true;
        };
        slot.alive = [has_object] { return has_object; };
        return add(std::move(slot));
    }

    void emit(Args... args) const
    {
        if (!state_)
        {
            return;
        }

        const auto state = state_;
        DispatchScope dispatch{state};
        const auto dispatch_limit = state->slots.size();

        for (std::size_t index = 0; index < dispatch_limit; ++index)
        {
            if (!state->slots[index].active)
            {
                continue;
            }

            const auto id = state->slots[index].id;
            auto invoke = state->slots[index].invoke;
            if (!invoke(args...))
            {
                state->disconnect(id);
            }
        }
    }

    void operator()(Args... args) const
    {
        emit(std::forward<Args>(args)...);
    }

    void clear() noexcept
    {
        if (state_)
        {
            state_->clear();
        }
    }

    [[nodiscard]] std::size_t size() const
    {
        return state_ ? state_->live_size() : 0;
    }

    [[nodiscard]] bool empty() const
    {
        return size() == 0;
    }

private:
    [[nodiscard]] Connection add(Slot slot)
    {
        ensure_state();
        const auto id = state_->add(std::move(slot));
        return Connection{state_, id};
    }

    void ensure_state()
    {
        if (!state_)
        {
            state_ = std::make_shared<State>();
        }
    }

    std::shared_ptr<State> state_;
};

template <typename Return, typename Collector, typename... Args>
class BasicSignal<Return(Args...), Collector>
{
    static_assert(!std::is_void_v<Return>, "Use Signal<void(...)> for void callbacks");
    static_assert(!std::is_reference_v<Return>,
                  "CollectingSignal reference returns are unsupported; use reference_wrapper instead");
    static_assert(!std::is_same_v<Collector, detail::VoidSignalPolicy>,
                  "Non-void multicast requires an explicit collector policy");
    static_assert(detail::multicast_arguments_are_reusable<Args...>,
                  "Multicast value arguments must be copyable and rvalue-reference arguments are unsupported");

    struct Slot final
    {
        std::uint64_t id = 0;
        bool active = true;
        std::function<std::optional<Return>(Args...)> invoke;
        std::function<bool()> alive;
    };

    class State final : public detail::ConnectionState
    {
    public:
        [[nodiscard]] std::uint64_t add(Slot slot)
        {
            if (slot.alive && !slot.alive())
            {
                return 0;
            }
            slot.id = allocate_id();
            const auto id = slot.id;
            slots.push_back(std::move(slot));
            return id;
        }

        void disconnect(const std::uint64_t id) noexcept override
        {
            for (auto& slot : slots)
            {
                if (slot.id == id)
                {
                    slot.active = false;
                    break;
                }
            }
            compact_if_idle();
        }

        [[nodiscard]] bool connected(const std::uint64_t id) const noexcept override
        {
            const auto iterator = std::find_if(slots.begin(), slots.end(), [id](const Slot& slot)
            {
                return slot.id == id;
            });
            return iterator != slots.end() && iterator->active && (!iterator->alive || iterator->alive());
        }

        void clear() noexcept
        {
            if (dispatch_depth == 0)
            {
                slots.clear();
            }
            else
            {
                for (auto& slot : slots)
                {
                    slot.active = false;
                }
            }
        }

        [[nodiscard]] std::size_t live_size() const
        {
            return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(), [](const Slot& slot)
            {
                return slot.active && (!slot.alive || slot.alive());
            }));
        }

        void begin_dispatch() noexcept { ++dispatch_depth; }
        void end_dispatch() noexcept
        {
            --dispatch_depth;
            compact_if_idle();
        }

        std::vector<Slot> slots;

    private:
        [[nodiscard]] std::uint64_t allocate_id()
        {
            for (;;)
            {
                const auto candidate = next_id++;
                if (next_id == 0)
                {
                    next_id = 1;
                }
                if (candidate != 0 && std::none_of(slots.begin(), slots.end(), [candidate](const Slot& slot)
                    {
                        return slot.id == candidate;
                    }))
                {
                    return candidate;
                }
            }
        }

        void compact_if_idle() noexcept
        {
            if (dispatch_depth == 0)
            {
                std::erase_if(slots, [](const Slot& slot)
                {
                    return !slot.active || (slot.alive && !slot.alive());
                });
            }
        }

        std::uint64_t next_id = 1;
        std::size_t dispatch_depth = 0;
    };

    class DispatchScope final
    {
    public:
        explicit DispatchScope(std::shared_ptr<State> state) noexcept
            : state_(std::move(state))
        {
            state_->begin_dispatch();
        }
        ~DispatchScope() { state_->end_dispatch(); }

    private:
        std::shared_ptr<State> state_;
    };

public:
    BasicSignal()
        : state_(std::make_shared<State>())
    {
    }

    BasicSignal(const BasicSignal&) = delete;
    BasicSignal& operator=(const BasicSignal&) = delete;
    BasicSignal(BasicSignal&&) noexcept = default;
    BasicSignal& operator=(BasicSignal&&) noexcept = default;

    template <typename Callable>
        requires std::is_invocable_r_v<Return, Callable&, Args...>
              && std::copy_constructible<std::decay_t<Callable>>
    Connection connect(Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        Slot slot;
        slot.invoke = [function = Stored(std::forward<Callable>(callable))](Args... args) mutable
            -> std::optional<Return>
        {
            return std::invoke(function, std::forward<Args>(args)...);
        };
        return add(std::move(slot));
    }

    template <typename Callable>
        requires std::is_invocable_r_v<Return, Callable&, Args...>
              && std::copy_constructible<std::decay_t<Callable>>
    Connection connect(const LifetimeToken& lifetime, Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        auto weak = lifetime.weak_handle();
        Slot slot;
        slot.invoke = [weak, function = Stored(std::forward<Callable>(callable))](Args... args) mutable
            -> std::optional<Return>
        {
            const auto guard = weak.lock();
            if (!guard)
            {
                return std::nullopt;
            }
            return std::invoke(function, std::forward<Args>(args)...);
        };
        slot.alive = [weak] { return !weak.expired(); };
        auto connection = add(std::move(slot));
        try
        {
            lifetime.track(connection);
        }
        catch (...)
        {
            connection.disconnect();
            throw;
        }
        return connection;
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<Return, Method, Object&, Args...>
    Connection connect_weak(std::weak_ptr<Object> object, Method method)
    {
        Slot slot;
        slot.invoke = [object, method](Args... args) mutable -> std::optional<Return>
        {
            const auto locked = object.lock();
            if (!locked)
            {
                return std::nullopt;
            }
            return std::invoke(method, *locked, std::forward<Args>(args)...);
        };
        slot.alive = [object] { return !object.expired(); };
        return add(std::move(slot));
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<Return, Method, Object&, Args...>
    Connection connect_shared(std::shared_ptr<Object> object, Method method)
    {
        const bool has_object = static_cast<bool>(object);
        Slot slot;
        slot.invoke = [object = std::move(object), method](Args... args) mutable
            -> std::optional<Return>
        {
            if (!object)
            {
                return std::nullopt;
            }
            return std::invoke(method, *object, std::forward<Args>(args)...);
        };
        slot.alive = [has_object] { return has_object; };
        return add(std::move(slot));
    }

    [[nodiscard]] typename Collector::result_type emit(Args... args) const
    {
        Collector collector;
        if (!state_)
        {
            return std::move(collector).finish();
        }

        const auto state = state_;
        DispatchScope dispatch{state};
        const auto dispatch_limit = state->slots.size();
        for (std::size_t index = 0; index < dispatch_limit; ++index)
        {
            if (!state->slots[index].active)
            {
                continue;
            }

            const auto id = state->slots[index].id;
            auto invoke = state->slots[index].invoke;
            auto value = invoke(args...);
            if (!value)
            {
                state->disconnect(id);
                continue;
            }
            collector.add(std::move(*value));
        }
        return std::move(collector).finish();
    }

    void clear() noexcept
    {
        if (state_)
        {
            state_->clear();
        }
    }

    [[nodiscard]] std::size_t size() const { return state_ ? state_->live_size() : 0; }
    [[nodiscard]] bool empty() const { return size() == 0; }

private:
    [[nodiscard]] Connection add(Slot slot)
    {
        ensure_state();
        const auto id = state_->add(std::move(slot));
        return Connection{state_, id};
    }

    void ensure_state()
    {
        if (!state_)
        {
            state_ = std::make_shared<State>();
        }
    }

    std::shared_ptr<State> state_;
};
} // namespace observer_meta
