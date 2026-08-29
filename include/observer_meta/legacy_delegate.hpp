#pragma once

#include "delegate.hpp"
#include "signal.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Delegates
{
class FDelegateHandle
{
public:
    FDelegateHandle() = default;

    [[nodiscard]] bool IsValid() const noexcept { return Connection.connected(); }
    void Reset() noexcept { Connection = {}; }

    friend bool operator==(const FDelegateHandle&, const FDelegateHandle&) noexcept = default;
    friend bool operator!=(const FDelegateHandle&, const FDelegateHandle&) noexcept = default;

private:
    template <typename>
    friend class TMulticastDelegate;

    explicit FDelegateHandle(observer_meta::Connection connection) noexcept
        : Connection(std::move(connection))
    {
    }

    observer_meta::Connection Connection;
};

template <typename Signature>
struct TFuncPtrType;

template <typename Return, typename... Args>
struct TFuncPtrType<Return(Args...)>
{
    using Type = Return (*)(Args...);
};

template <typename Signature>
using TFuncPtr = typename TFuncPtrType<Signature>::Type;

template <typename Class, typename Signature>
struct TMemFuncPtrType;

template <typename Class, typename Return, typename... Args>
struct TMemFuncPtrType<Class, Return(Args...)>
{
    using Type = Return (Class::*)(Args...);
};

template <typename Class, typename Return, typename... Args>
struct TMemFuncPtrType<Class, Return(Args...) const>
{
    using Type = Return (Class::*)(Args...) const;
};

template <typename Class, typename Signature>
using TMemFuncPtr = typename TMemFuncPtrType<Class, Signature>::Type;

template <typename Signature>
class TDelegate;

template <typename Return, typename... Args>
class TDelegate<Return(Args...)>
{
public:
    [[nodiscard]] bool IsBound() const { return Delegate.bound(); }
    void Unbind() noexcept { Delegate.reset(); }
    [[nodiscard]] FDelegateHandle GetHandle() const noexcept { return {}; }

    void BindStatic(TFuncPtr<Return(Args...)> function) { Delegate.bind(function); }

    template <typename Class, typename Method>
    [[deprecated("AddRaw is lifetime-unsafe; use AddWeak or a lifetime-bound callable")]]
    void AddRaw(Class* object, Method method)
    {
        Delegate.bind([object, method](Args... args) -> Return
        {
            if constexpr (std::is_void_v<Return>)
            {
                std::invoke(method, *object, std::forward<Args>(args)...);
            }
            else
            {
                return std::invoke(method, *object, std::forward<Args>(args)...);
            }
        });
    }

    template <typename Class, typename Method>
    void AddWeak(std::weak_ptr<Class> object, Method method)
    {
        Delegate.bind_weak(std::move(object), method);
    }

    template <typename Callable>
    void AddLambda(Callable&& callable)
    {
        Delegate.bind(std::forward<Callable>(callable));
    }

    Return Execute(Args... args) const
    {
        if constexpr (std::is_void_v<Return>)
        {
            Delegate.invoke(std::forward<Args>(args)...);
        }
        else
        {
            return Delegate.invoke(std::forward<Args>(args)...);
        }
    }

    Return ExecuteIfBound(Args... args) const
    {
        if constexpr (std::is_void_v<Return>)
        {
            static_cast<void>(Delegate.try_invoke(std::forward<Args>(args)...));
        }
        else
        {
            auto result = Delegate.try_invoke(std::forward<Args>(args)...);
            return result ? std::move(*result) : Return{};
        }
    }

private:
    observer_meta::Delegate<Return(Args...)> Delegate;
};

template <typename Signature>
class TMulticastDelegate;

template <typename... Args>
class TMulticastDelegate<void(Args...)>
{
    struct OwnedConnection final
    {
        const void* Owner = nullptr;
        observer_meta::Connection Connection;
    };

public:
    [[nodiscard]] bool IsBound() const { return !Signal.empty(); }

    void Clear()
    {
        Signal.clear();
        Connections.clear();
    }

    FDelegateHandle AddStatic(TFuncPtr<void(Args...)> function)
    {
        return track(nullptr, Signal.connect(function));
    }

    template <typename Class, typename Method>
    [[deprecated("AddRaw is lifetime-unsafe; use AddWeak or a lifetime-bound callable")]]
    FDelegateHandle AddRaw(Class* object, Method method)
    {
        return track(object, Signal.connect([object, method](Args... args)
        {
            std::invoke(method, *object, std::forward<Args>(args)...);
        }));
    }

    template <typename Class, typename Method>
    FDelegateHandle AddWeak(std::weak_ptr<Class> object, Method method)
    {
        return track(nullptr, Signal.connect_weak(std::move(object), method));
    }

    template <typename Callable>
    FDelegateHandle AddLambda(Callable&& callable)
    {
        return track(nullptr, Signal.connect(std::forward<Callable>(callable)));
    }

    void Remove(const FDelegateHandle& handle)
    {
        handle.Connection.disconnect();
        compact_connections();
    }

    void RemoveAll(const void* owner)
    {
        if (!owner)
        {
            return;
        }

        for (const auto& entry : Connections)
        {
            if (entry.Owner == owner)
            {
                entry.Connection.disconnect();
            }
        }
        compact_connections();
    }

    void Broadcast(Args... args)
    {
        Signal.emit(std::forward<Args>(args)...);
        compact_connections();
    }

private:
    FDelegateHandle track(const void* owner, observer_meta::Connection connection)
    {
        Connections.push_back({owner, connection});
        return FDelegateHandle{std::move(connection)};
    }

    void compact_connections()
    {
        std::erase_if(Connections, [](const OwnedConnection& entry)
        {
            return !entry.Connection.connected();
        });
    }

    observer_meta::Signal<void(Args...)> Signal;
    std::vector<OwnedConnection> Connections;
};
} // namespace Delegates
