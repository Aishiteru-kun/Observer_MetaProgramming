#pragma once

#include "lifetime_token.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace observer_meta
{
template <typename Signature>
class Delegate;

template <typename Return, typename... Args>
class Delegate<Return(Args...)>
{
    static_assert(!std::is_void_v<Return>);
    static_assert(!std::is_reference_v<Return>,
                  "Delegate reference returns are unsupported; return a pointer or reference_wrapper instead");

public:
    Delegate() = default;

    template <typename Callable>
        requires std::is_invocable_r_v<Return, Callable&, Args...>
    explicit Delegate(Callable&& callable)
    {
        bind(std::forward<Callable>(callable));
    }

    template <typename Callable>
        requires std::is_invocable_r_v<Return, Callable&, Args...>
    void bind(Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        invoker_ = [function = Stored(std::forward<Callable>(callable))](Args... args) mutable
            -> std::optional<Return>
        {
            return std::invoke(function, std::forward<Args>(args)...);
        };
        alive_ = {};
    }

    template <typename Callable>
        requires std::is_invocable_r_v<Return, Callable&, Args...>
    void bind(const LifetimeToken& lifetime, Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        auto weak = lifetime.weak_handle();
        invoker_ = [weak, function = Stored(std::forward<Callable>(callable))](Args... args) mutable
            -> std::optional<Return>
        {
            const auto guard = weak.lock();
            if (!guard)
            {
                return std::nullopt;
            }
            return std::invoke(function, std::forward<Args>(args)...);
        };
        alive_ = [weak] { return !weak.expired(); };
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<Return, Method, Object&, Args...>
    void bind_weak(std::weak_ptr<Object> object, Method method)
    {
        invoker_ = [object, method](Args... args) mutable -> std::optional<Return>
        {
            const auto locked = object.lock();
            if (!locked)
            {
                return std::nullopt;
            }
            return std::invoke(method, *locked, std::forward<Args>(args)...);
        };
        alive_ = [object] { return !object.expired(); };
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<Return, Method, Object&, Args...>
    void bind_shared(std::shared_ptr<Object> object, Method method)
    {
        const bool has_object = static_cast<bool>(object);
        invoker_ = [object = std::move(object), method](Args... args) mutable -> std::optional<Return>
        {
            if (!object)
            {
                return std::nullopt;
            }
            return std::invoke(method, *object, std::forward<Args>(args)...);
        };
        alive_ = [has_object] { return has_object; };
    }

    void reset() noexcept
    {
        invoker_ = {};
        alive_ = {};
    }

    [[nodiscard]] bool bound() const
    {
        return static_cast<bool>(invoker_) && (!alive_ || alive_());
    }

    [[nodiscard]] std::optional<Return> try_invoke(Args... args) const
    {
        if (!invoker_)
        {
            return std::nullopt;
        }
        return invoker_(std::forward<Args>(args)...);
    }

    Return invoke(Args... args) const
    {
        auto result = try_invoke(std::forward<Args>(args)...);
        if (!result)
        {
            throw std::bad_function_call{};
        }
        return std::move(*result);
    }

    Return operator()(Args... args) const
    {
        return invoke(std::forward<Args>(args)...);
    }

private:
    std::function<std::optional<Return>(Args...)> invoker_;
    std::function<bool()> alive_;
};

template <typename... Args>
class Delegate<void(Args...)>
{
public:
    Delegate() = default;

    template <typename Callable>
        requires std::is_invocable_r_v<void, Callable&, Args...>
    explicit Delegate(Callable&& callable)
    {
        bind(std::forward<Callable>(callable));
    }

    template <typename Callable>
        requires std::is_invocable_r_v<void, Callable&, Args...>
    void bind(Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        invoker_ = [function = Stored(std::forward<Callable>(callable))](Args... args) mutable
        {
            std::invoke(function, std::forward<Args>(args)...);
            return true;
        };
        alive_ = {};
    }

    template <typename Callable>
        requires std::is_invocable_r_v<void, Callable&, Args...>
    void bind(const LifetimeToken& lifetime, Callable&& callable)
    {
        using Stored = std::decay_t<Callable>;
        auto weak = lifetime.weak_handle();
        invoker_ = [weak, function = Stored(std::forward<Callable>(callable))](Args... args) mutable
        {
            const auto guard = weak.lock();
            if (!guard)
            {
                return false;
            }
            std::invoke(function, std::forward<Args>(args)...);
            return true;
        };
        alive_ = [weak] { return !weak.expired(); };
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<void, Method, Object&, Args...>
    void bind_weak(std::weak_ptr<Object> object, Method method)
    {
        invoker_ = [object, method](Args... args) mutable
        {
            const auto locked = object.lock();
            if (!locked)
            {
                return false;
            }
            std::invoke(method, *locked, std::forward<Args>(args)...);
            return true;
        };
        alive_ = [object] { return !object.expired(); };
    }

    template <typename Object, typename Method>
        requires std::is_invocable_r_v<void, Method, Object&, Args...>
    void bind_shared(std::shared_ptr<Object> object, Method method)
    {
        const bool has_object = static_cast<bool>(object);
        invoker_ = [object = std::move(object), method](Args... args) mutable
        {
            if (!object)
            {
                return false;
            }
            std::invoke(method, *object, std::forward<Args>(args)...);
            return true;
        };
        alive_ = [has_object] { return has_object; };
    }

    void reset() noexcept
    {
        invoker_ = {};
        alive_ = {};
    }

    [[nodiscard]] bool bound() const
    {
        return static_cast<bool>(invoker_) && (!alive_ || alive_());
    }

    [[nodiscard]] bool try_invoke(Args... args) const
    {
        if (!invoker_)
        {
            return false;
        }
        return invoker_(std::forward<Args>(args)...);
    }

    void invoke(Args... args) const
    {
        if (!try_invoke(std::forward<Args>(args)...))
        {
            throw std::bad_function_call{};
        }
    }

    void operator()(Args... args) const
    {
        invoke(std::forward<Args>(args)...);
    }

private:
    std::function<bool(Args...)> invoker_;
    std::function<bool()> alive_;
};
} // namespace observer_meta
