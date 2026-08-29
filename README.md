# ObserverMeta

ObserverMeta is a small C++23 library for deterministic, thread-confined
delegates and signals. It is header-only and has no third-party dependencies.

```cmake
add_subdirectory(Libraries/ObserverMeta)
target_link_libraries(MyTarget PRIVATE ObserverMeta::ObserverMeta)
```

```cpp
#include <observer_meta/observer_meta.hpp>

observer_meta::Signal<void(int)> changed;
observer_meta::ScopedConnection subscription{changed.connect(
    [](int value) { /* consume value */ })};

changed.emit(42);
```

Bind callbacks owned by a module, view, or plugin to a `LifetimeToken`.
Resetting the token eagerly disconnects every tracked slot and destroys its
captured callable; `renew()` starts a new generation without reviving stale
connections.

```cpp
observer_meta::LifetimeToken owner;
changed.connect(owner, [](int value) { /* owner-scoped work */ });
owner.reset(); // disconnected now, before owner code may be unloaded
```

Signals are deliberately not internally synchronized. Create, connect,
disconnect, and emit a signal on the same owning thread, or provide external
synchronization. A callback disconnected before its turn is skipped. A callback
added while an emission is in progress starts with the next (including nested)
emission.

Use `observer_meta::CollectingSignal` with an explicit collector for non-void
multicast callbacks. This prevents callback return values from being silently
discarded.

The legacy `AddRaw` facade is deprecated because it cannot verify object
lifetime. Prefer `AddWeak`, `connect_weak`, or a lifetime-bound callable.
