#include <observer_meta/observer_meta.hpp>
#include <observer_meta/legacy_delegate.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFailure final : public std::runtime_error
{
public:
    TestFailure(const std::string_view expression, const std::string_view file, const int line)
        : std::runtime_error(std::string(file) + ":" + std::to_string(line)
                             + " check failed: " + std::string(expression))
    {
    }
};

#define CHECK(expression) \
    do \
    { \
        if (!(expression)) \
        { \
            throw TestFailure{#expression, __FILE__, __LINE__}; \
        } \
    } while (false)

struct Listener final
{
    void add(const int value) { total += value; }
    void add_const(const int value) const { *external_total += value; }
    [[nodiscard]] int multiply(const int value) const { return value * factor; }

    int total = 0;
    int factor = 1;
    int* external_total = nullptr;
};

void connection_and_order()
{
    observer_meta::Signal<void(int)> signal;
    std::vector<int> order;

    const auto first = signal.connect([&](const int value) { order.push_back(value + 1); });
    const auto second = signal.connect([&](const int value) { order.push_back(value + 2); });

    signal.emit(10);
    CHECK((order == std::vector<int>{11, 12}));
    CHECK(first.connected());
    CHECK(second.connected());

    first.disconnect();
    signal.emit(20);
    CHECK((order == std::vector<int>{11, 12, 22}));
    CHECK(!first.connected());
    CHECK(signal.size() == 1);
}

void disconnect_before_turn_is_skipped()
{
    observer_meta::Signal<void()> signal;
    int calls = 0;
    observer_meta::Connection second;

    const auto first = signal.connect([&]
    {
        ++calls;
        second.disconnect();
    });
    second = signal.connect([&] { calls += 100; });

    signal.emit();
    CHECK(calls == 1);
    CHECK(first.connected());
    CHECK(!second.connected());
}

void add_during_dispatch_starts_next_dispatch()
{
    observer_meta::Signal<void()> signal;
    int original_calls = 0;
    int late_calls = 0;
    observer_meta::Connection late;

    const auto original = signal.connect([&]
    {
        ++original_calls;
        if (!late.connected())
        {
            late = signal.connect([&] { ++late_calls; });
        }
    });

    signal.emit();
    CHECK(original_calls == 1);
    CHECK(late_calls == 0);

    signal.emit();
    CHECK(original_calls == 2);
    CHECK(late_calls == 1);
    CHECK(original.connected());
}

void scoped_connection_is_raii()
{
    observer_meta::Signal<void()> signal;
    int calls = 0;

    {
        observer_meta::ScopedConnection scoped{signal.connect([&] { ++calls; })};
        CHECK(scoped.connected());
        signal.emit();
    }

    signal.emit();
    CHECK(calls == 1);
    CHECK(signal.empty());
}

void lifetime_token_invalidates_subscription()
{
    observer_meta::Signal<void()> signal;
    int calls = 0;
    observer_meta::Connection connection;

    {
        observer_meta::LifetimeToken lifetime;
        connection = signal.connect(lifetime, [&] { ++calls; });
        signal.emit();
        CHECK(calls == 1);
        CHECK(connection.connected());
    }

    CHECK(!connection.connected());
    signal.emit();
    CHECK(calls == 1);
    CHECK(signal.empty());
}

void lifetime_token_eagerly_disconnects_and_renews()
{
    observer_meta::Signal<void()> signal;
    observer_meta::LifetimeToken lifetime;
    auto captured = std::make_shared<int>(7);
    const std::weak_ptr<int> captured_weak = captured;

    const auto old_connection = signal.connect(lifetime, [captured] {});
    captured.reset();
    CHECK(old_connection.connected());
    CHECK(!captured_weak.expired());

    lifetime.renew();
    CHECK(!old_connection.connected());
    CHECK(captured_weak.expired());
    CHECK(signal.empty());

    int calls = 0;
    const auto renewed_connection = signal.connect(lifetime, [&] { ++calls; });
    signal.emit();
    CHECK(calls == 1);
    CHECK(renewed_connection.connected());
}

void lifetime_reset_during_dispatch_skips_later_callback()
{
    observer_meta::Signal<void()> signal;
    observer_meta::LifetimeToken lifetime;
    int calls = 0;

    const auto resetter = signal.connect([&]
    {
        ++calls;
        lifetime.reset();
    });
    const auto later = signal.connect(lifetime, [&] { calls += 100; });

    signal.emit();
    CHECK(calls == 1);
    CHECK(resetter.connected());
    CHECK(!later.connected());
    CHECK(signal.size() == 1);
}

void weak_and_shared_bindings()
{
    observer_meta::Signal<void(int)> signal;
    auto weak_listener = std::make_shared<Listener>();
    const auto weak_connection = signal.connect_weak(std::weak_ptr<Listener>{weak_listener}, &Listener::add);

    int const_total = 0;
    auto const_listener = std::make_shared<const Listener>(Listener{0, 1, &const_total});
    const auto const_connection =
        signal.connect_weak(std::weak_ptr<const Listener>{const_listener}, &Listener::add_const);

    auto shared_listener = std::make_shared<Listener>();
    const auto shared_connection = signal.connect_shared(shared_listener, &Listener::add);
    shared_listener.reset();

    signal.emit(5);
    CHECK(weak_listener->total == 5);
    CHECK(const_total == 5);
    CHECK(shared_connection.connected());

    weak_listener.reset();
    const_listener.reset();
    signal.emit(3);

    CHECK(!weak_connection.connected());
    CHECK(!const_connection.connected());
    CHECK(signal.size() == 1);
}

void value_payload_is_preserved_for_each_listener()
{
    observer_meta::Signal<void(std::string)> signal;
    std::vector<std::string> received;

    const auto first = signal.connect([&](std::string value) { received.push_back(std::move(value)); });
    const auto second = signal.connect([&](std::string value) { received.push_back(std::move(value)); });

    signal.emit("payload");
    CHECK((received == std::vector<std::string>{"payload", "payload"}));
    CHECK(first.connected());
    CHECK(second.connected());
}

void delegate_supports_arguments_and_const_members()
{
    observer_meta::Delegate<int(int, int)> lambda;
    lambda.bind([](const int lhs, const int rhs) { return lhs + rhs; });
    CHECK(lambda.invoke(2, 3) == 5);

    auto listener = std::make_shared<const Listener>(Listener{0, 4, nullptr});
    observer_meta::Delegate<int(int)> weak;
    weak.bind_weak(std::weak_ptr<const Listener>{listener}, &Listener::multiply);
    CHECK(weak.invoke(3) == 12);

    listener.reset();
    CHECK(!weak.bound());
    CHECK(!weak.try_invoke(3).has_value());
}

void collecting_signal_requires_explicit_policy()
{
    observer_meta::CollectingSignal<int(int), observer_meta::collector::Vector<int>> signal;
    const auto first = signal.connect([](const int value) { return value + 1; });
    const auto second = signal.connect([](const int value) { return value * 2; });

    const auto values = signal.emit(4);
    CHECK((values == std::vector<int>{5, 8}));
    CHECK(first.connected());
    CHECK(second.connected());
}

void collecting_signal_supports_lifetime_and_shared_bindings()
{
    observer_meta::CollectingSignal<int(int), observer_meta::collector::Vector<int>> signal;
    observer_meta::LifetimeToken lifetime;
    const auto bound = signal.connect(lifetime, [](const int value) { return value + 1; });

    auto listener = std::make_shared<const Listener>(Listener{0, 3, nullptr});
    const auto shared = signal.connect_shared(listener, &Listener::multiply);
    listener.reset();

    CHECK((signal.emit(4) == std::vector<int>{5, 12}));
    lifetime.reset();
    CHECK(!bound.connected());
    CHECK(shared.connected());
    CHECK((signal.emit(4) == std::vector<int>{12}));

    const auto null_shared = signal.connect_shared(
        std::shared_ptr<const Listener>{}, &Listener::multiply);
    CHECK(!null_shared.connected());
}

void connection_expires_with_signal()
{
    observer_meta::Connection connection;
    {
        observer_meta::Signal<void()> signal;
        connection = signal.connect([] {});
        CHECK(connection.connected());
    }
    CHECK(!connection.connected());
}

void legacy_facade_remains_usable()
{
    Delegates::TMulticastDelegate<void(int)> signal;
    int total = 0;
    const auto handle = signal.AddLambda([&](const int value) { total += value; });

    signal.Broadcast(4);
    CHECK(total == 4);
    CHECK(handle.IsValid());

    signal.Remove(handle);
    signal.Broadcast(4);
    CHECK(total == 4);
    CHECK(!handle.IsValid());

    Delegates::TDelegate<int(int)> delegate;
    delegate.AddLambda([](const int value) { return value + 2; });
    CHECK(delegate.Execute(5) == 7);
}

template <typename Function>
int run(const std::string_view name, Function&& function)
{
    try
    {
        std::invoke(std::forward<Function>(function));
        std::cout << "[PASS] " << name << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << name << ": " << exception.what() << '\n';
        return 1;
    }
}
} // namespace

int main()
{
    int failures = 0;
    failures += run("connection_and_order", connection_and_order);
    failures += run("disconnect_before_turn_is_skipped", disconnect_before_turn_is_skipped);
    failures += run("add_during_dispatch_starts_next_dispatch", add_during_dispatch_starts_next_dispatch);
    failures += run("scoped_connection_is_raii", scoped_connection_is_raii);
    failures += run("lifetime_token_invalidates_subscription", lifetime_token_invalidates_subscription);
    failures += run("lifetime_token_eagerly_disconnects_and_renews",
                    lifetime_token_eagerly_disconnects_and_renews);
    failures += run("lifetime_reset_during_dispatch_skips_later_callback",
                    lifetime_reset_during_dispatch_skips_later_callback);
    failures += run("weak_and_shared_bindings", weak_and_shared_bindings);
    failures += run("value_payload_is_preserved_for_each_listener", value_payload_is_preserved_for_each_listener);
    failures += run("delegate_supports_arguments_and_const_members", delegate_supports_arguments_and_const_members);
    failures += run("collecting_signal_requires_explicit_policy", collecting_signal_requires_explicit_policy);
    failures += run("collecting_signal_supports_lifetime_and_shared_bindings",
                    collecting_signal_supports_lifetime_and_shared_bindings);
    failures += run("connection_expires_with_signal", connection_expires_with_signal);
    failures += run("legacy_facade_remains_usable", legacy_facade_remains_usable);
    return failures == 0 ? 0 : 1;
}
