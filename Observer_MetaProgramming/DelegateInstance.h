#pragma once
#include <assert.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace Delegates
{
    // ======================= Handle =======================
    class FDelegateHandle
    {
    public:
        enum GenerateNewHandleType
        {
            GenerateNewHandle
        };

    public:
        FDelegateHandle()
            : DelegateID(0)
        {
        }

        explicit FDelegateHandle(GenerateNewHandleType)
            : DelegateID(GenerateNewID())
        {
        }

        __forceinline bool IsValid() const { return DelegateID != 0; }
        __forceinline void Reset() { DelegateID = 0; }

        bool operator==(const FDelegateHandle& Other) const { return DelegateID == Other.DelegateID; }
        bool operator!=(const FDelegateHandle& Other) const { return DelegateID != Other.DelegateID; }

    private:
        static uint64_t GenerateNewID();

        uint64_t DelegateID;
    };

    // ==================== Base Instance ====================

    class IDelegateInstance
    {
    public:
        virtual ~IDelegateInstance() = default;

        virtual bool IsCompactable() const { return !IsSafeToExecute(); }
        virtual bool IsSafeToExecute() const = 0;

        virtual FDelegateHandle GetHandle() const = 0;
    };

    template <typename Signature>
    struct TFuncPtrType;

    template <typename RetType, typename... ArgsType>
    struct TFuncPtrType<RetType(ArgsType...)>
    {
        using Type = RetType(*)(ArgsType...);
    };

    template <typename Class, typename Signature>
    struct TMemFunсPtrType;

    template <typename Class, typename RetType, typename... ArgsType>
    struct TMemFunсPtrType<Class, RetType(ArgsType...)>
    {
        using Type = RetType(Class::*)(ArgsType...);
    };

    template <typename Class, typename RetType, typename... ArgsType>
    struct TMemFunсPtrType<Class, RetType(ArgsType...) const>
    {
        using Type = RetType(Class::*)(ArgsType...) const;
    };

    template <typename Signature>
    using TFuncPtr = typename TFuncPtrType<Signature>::Type;

    template <typename Class, typename Signature>
    using TMemFuncPtr = typename TMemFunсPtrType<Class, Signature>::Type;

    template <typename RetType, typename... ArgsType>
    class TDelegateInstanceBase : public IDelegateInstance
    {
    public:
        TDelegateInstanceBase() = default;
        virtual ~TDelegateInstanceBase() = default;

    public:
        virtual RetType Execute(ArgsType... Args) = 0;
        virtual RetType ExecuteIfSafe(ArgsType... Args) = 0;
    };

    // ============ Concrete Instances (Static, Raw, Weak, Lambda) ============

    template <typename RetType, typename... ArgsType>
    class TStaticDelegateInstance final : public TDelegateInstanceBase<RetType, ArgsType...>
    {
    public:
        explicit TStaticDelegateInstance(TFuncPtr<RetType(ArgsType...)> InFn)
            : Func(InFn), Handle(FDelegateHandle::GenerateNewHandle) {}

        __forceinline bool IsSafeToExecute() const { return Func != nullptr; }
        __forceinline FDelegateHandle GetHandle() const override { return Handle; }

        RetType Execute(ArgsType... Args) override
        {
            assert(IsSafeToExecute());
            if constexpr (std::is_void_v<RetType>) { Func(std::forward<ArgsType>(Args)...); }
            else { return Func(std::forward<ArgsType>(Args)...); }
        }

        RetType ExecuteIfSafe(ArgsType... Args) override
        {
            if (IDelegateInstance::IsCompactable())
            {
                if constexpr (!std::is_void_v<RetType>) return RetType{};
                else return;
            }
            return Func(std::forward<ArgsType>(Args)...);
        }

    private:
        TFuncPtr<RetType(ArgsType...)> Func = nullptr;
        FDelegateHandle Handle;
    };

    template <typename Class, typename RetType, typename... ArgsType>
    class TRawDelegateInstance final : public TDelegateInstanceBase<RetType, ArgsType...>
    {
        using MethodType = TMemFuncPtr<Class, RetType(ArgsType...)>;

    public:

        TRawDelegateInstance(Class* InClassPtr, MethodType InMethod)
            : ClassPtr(InClassPtr), Method(InMethod), Handle(FDelegateHandle::GenerateNewHandle) {}

        __forceinline bool IsSafeToExecute() const override { return ClassPtr != nullptr && Method != nullptr; }
        __forceinline FDelegateHandle GetHandle() const override { return Handle; }

        RetType Execute(ArgsType... Args) override
        {
            assert(IsSafeToExecute());
            if constexpr (std::is_void_v<RetType>) { (ClassPtr->*Method)(std::forward<ArgsType>(Args)...); }
            else { return (ClassPtr->*Method)(std::forward<ArgsType>(Args)...); }
        }

        RetType ExecuteIfSafe(ArgsType... Args) override
        {
            if (IDelegateInstance::IsCompactable())
            {
                if constexpr (!std::is_void_v<RetType>) return RetType{};
                else return;
            }
            return Execute(std::forward<ArgsType>(Args)...);
        }

    private:
        Class* ClassPtr = nullptr;
        MethodType Method = nullptr;
        FDelegateHandle Handle;
    };

    template <typename Class, typename RetType, typename... ArgsType>
    class TWeakDelegateInstance final : public TDelegateInstanceBase<RetType, ArgsType...>
    {
    public:
        using MethodType = TMemFuncPtr<Class, RetType(ArgsType...)>;

        TWeakDelegateInstance(std::weak_ptr<Class> InWeak, MethodType InMethod)
            : Weak(InWeak), Method(InMethod), Handle(FDelegateHandle::GenerateNewHandle) {}

        __forceinline bool IsSafeToExecute() const override { return !Weak.expired() && Method != nullptr; }
        __forceinline FDelegateHandle GetHandle() const override { return Handle; }

        RetType Execute(ArgsType... Args) override
        {
            assert(IsSafeToExecute());
            auto ObjectContext = Weak.lock();
            if constexpr (std::is_void_v<RetType>) { (ObjectContext.get()->*Method)(std::forward<ArgsType>(Args)...); }
            else { return (ObjectContext.get()->*Method)(std::forward<ArgsType>(Args)...); }
        }

        RetType ExecuteIfSafe(ArgsType... Args) override
        {
            if (!IsSafeToExecute())
            {
                if constexpr (!std::is_void_v<RetType>) return RetType{};
                else return;
            }
            return Execute(std::forward<ArgsType>(Args)...);
        }

    private:
        std::weak_ptr<Class> Weak;
        MethodType Method = nullptr;
        FDelegateHandle Handle;
    };

    template <typename RetType, typename Func, typename... ArgsType>
    class TLambdaDelegateInstance final : public TDelegateInstanceBase<RetType, ArgsType...>
    {
    public:
        explicit TLambdaDelegateInstance(Func&& InFn)
            : Fn(std::forward<Func>(InFn)), Handle(FDelegateHandle::GenerateNewHandle) {}

        __forceinline bool IsSafeToExecute() const override { return static_cast<bool>(Fn); }
        __forceinline FDelegateHandle GetHandle() const override { return Handle; }

        RetType Execute(ArgsType... Args) override
        {
            assert(IsSafeToExecute());
            if constexpr (std::is_void_v<RetType>) { Fn(std::forward<ArgsType>(Args)...); }
            else { return Fn(std::forward<ArgsType>(Args)...); }
        }

        RetType ExecuteIfSafe(ArgsType... Args) override
        {
            if (!IsSafeToExecute())
            {
                if constexpr (!std::is_void_v<RetType>) return RetType{};
                else return;
            }
            return Execute(std::forward<ArgsType>(Args)...);
        }

    private:
        std::function<RetType(ArgsType...)> Fn;
        FDelegateHandle Handle;
    };

    // ============================ TDelegate (unicast) ============================
    template <typename Signature>
    class TDelegate;

    template <typename RetType, typename... ArgsType>
    class TDelegate<RetType(ArgsType...)>
    {
        using InstanceBase = TDelegateInstanceBase<RetType, ArgsType...>;

    public:

        TDelegate() = default;

        __forceinline bool IsBound() const { return Instance != nullptr && Instance->IsSafeToExecute(); }
        __forceinline void Unbind() { Instance.reset(); }

        __forceinline FDelegateHandle GetHandle() const { return Instance ? Instance->GetHandle() : FDelegateHandle{}; }

        void BindStatic(TFuncPtr<RetType(ArgsType...)> Func)
        {
            Instance = std::make_shared<TStaticDelegateInstance<RetType, ArgsType...>>(Func);
        }

        template<typename Class>
        void AddRaw(Class* InObjPtr, TMemFuncPtr<Class, RetType(ArgsType...)> InMethod)
        {
            Instance = std::make_shared<TRawDelegateInstance<Class, RetType, ArgsType...>>(InObjPtr, InMethod);
        }
        template<typename Class>
        void AddRaw(const Class* InObjPtr, TMemFuncPtr<const Class, RetType(ArgsType...)> InMethod)
        {
            Instance = std::make_shared<TRawDelegateInstance<const Class, RetType, ArgsType...>>(InObjPtr, InMethod);
        }

        // Bind weak_ptr
        template<typename Class>
        void AddWeak(std::weak_ptr<Class> InWeak, TMemFuncPtr<Class, RetType(ArgsType...)> InMethod)
        {
            Instance = std::make_shared<TWeakDelegateInstance<Class, RetType, ArgsType...>>(InWeak, InMethod);
        }
        template<typename Class>
        void AddWeak(std::weak_ptr<const Class> InWeak, TMemFuncPtr<const Class, RetType(ArgsType...)> InMethod)
        {
            Instance = std::make_shared<TWeakDelegateInstance<const Class, RetType, ArgsType...>>(InWeak, InMethod);
        }

        template<typename Func>
        void AddLambda(Func&& InLambdaFunc)
        {
            Instance = std::make_shared<TLambdaDelegateInstance<RetType, ArgsType..., Func>>(std::forward<Func>(InLambdaFunc));
        }

        RetType Execute(ArgsType... Args) const
        {
            assert(IsBound());
            return Instance->Execute(std::forward<ArgsType>(Args)...);
        }

        RetType ExecuteIfBound(ArgsType... Args) const
        {
            if (!IsBound())
            {
                if constexpr (!std::is_void_v<RetType>) return RetType{};
                else return;
            }
            return Instance->Execute(std::forward<ArgsType>(Args)...);
        }

    private:
        std::shared_ptr<InstanceBase> Instance;
    };

    // ======================= TMulticastDelegate (multicast) =======================

    template <typename Signature>
    class TMulticastDelegate;

    template <typename RetType, typename... ArgsType>
class TMulticastDelegate<RetType(ArgsType...)>
    {
        using Unicast = TDelegate<RetType(ArgsType...)>;

        struct FEntry
        {
            FDelegateHandle Handle;
            Unicast Delegate;
            void* Owner = nullptr;
        };

    public:
        __forceinline bool IsBound() const { return !Entries.empty(); }
        __forceinline void Clear() { Entries.clear(); }

        FDelegateHandle AddStatic(TFuncPtr<RetType(ArgsType...)> Fn)
        {
            Unicast D; D.BindStatic(Fn);
            return AddInternal(std::move(D), nullptr);
        }

        template<typename Class>
        FDelegateHandle AddRaw(Class* InObjPtr, TMemFuncPtr<Class, RetType(ArgsType...)> InMethod)
        {
            Unicast Delegate; Delegate.AddRaw(InObjPtr, InMethod);
            return AddInternal(std::move(Delegate), InObjPtr);
        }
        template<typename Class>
        FDelegateHandle AddRaw(const Class* InObjPtr, TMemFuncPtr<const Class, RetType(ArgsType...)> InMethod)
        {
            Unicast Delegate; Delegate.AddRaw(InObjPtr, InMethod);
            return AddInternal(std::move(Delegate), const_cast<Class*>(InObjPtr));
        }

        template<typename Class>
        FDelegateHandle AddWeak(std::weak_ptr<Class> InWeak, TMemFuncPtr<Class, RetType(ArgsType...)> InMethod)
        {
            Unicast Delegate; Delegate.AddWeak(InWeak, InMethod);
            return AddInternal(std::move(Delegate), nullptr);
        }
        template<typename Class>
        FDelegateHandle AddWeak(std::weak_ptr<const Class> InWeak, TMemFuncPtr<const Class, RetType(ArgsType...)> InMethod)
        {
            Unicast Delegate; Delegate.AddWeak(InWeak, InMethod);
            return AddInternal(std::move(Delegate), nullptr);
        }

        template<typename Func>
        FDelegateHandle AddLambda(Func&& InFunc)
        {
            Unicast Delegate; Delegate.AddLambda(std::forward<Func>(InFunc));
            return AddInternal(std::move(Delegate), nullptr);
        }

        void Remove(FDelegateHandle InHandle)
        {
            auto Iter = std::find_if(Entries.begin(), Entries.end(),
                [&](const FEntry& E)
                {
                    return E.Handle == InHandle;
                });

            if (Iter != Entries.end()) Entries.erase(Iter);
        }

        void RemoveAll(const void* InOwner)
        {
            if (!InOwner) return;

            Entries.erase(
                std::remove_if(Entries.begin(), Entries.end(),
                    [&](const FEntry& E){ return E.Owner == InOwner; }),
                Entries.end());
        }

        void Broadcast(ArgsType... Args)
        {
            if (Entries.empty()) return;

            auto Snapshot = Entries;
            for (const FEntry& Entry : Snapshot)
            {
                Entry.Delegate.ExecuteIfBound(std::forward<ArgsType>(Args)...);
            }

            Entries.erase(
                std::remove_if(Entries.begin(), Entries.end(),
                    [](const FEntry& Entry){ return !Entry.Delegate.IsBound(); }),
                Entries.end());
        }

    private:
        FDelegateHandle AddInternal(Unicast&& InDelegate, void* InOwner)
        {
            FEntry Entry;
            Entry.Handle = InDelegate.GetHandle();

            if (!Entry.Handle.IsValid()) Entry.Handle = FDelegateHandle(FDelegateHandle::GenerateNewHandle);

            Entry.Delegate = std::move(InDelegate);
            Entry.Owner = InOwner;
            Entries.emplace_back(std::move(Entry));

            return Entries.back().Handle;
        }

    private:
        std::vector<FEntry> Entries;
    };
}
