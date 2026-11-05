# Listen to an event

The core raises events (the frame tick, entering a map, a key press, a model load...); the full list,
with the args of each, is [The events](events.md) (`wxl/events/Events.def` and `wxl/events/*Args.hpp`).
An extension listens in two ways, which mix freely:

- an **override**: the virtual of the event's name in your `Extension` class;
- a **handler**: `On<Tag>(lambda)`, with a priority, once only, or on a signal.

Example: `examples/events` (and `examples/hello` for the smallest form).

## Overrides

`Extension` (`wxl/Extension.hpp`) has one virtual per event, `OnTag`, with the event's args unpacked;
the list is `wxl/Overrides.def`. Override the ones you need:

<!-- quote: examples/events/src/Events.cpp -->
```cpp
    void OnWorldEnter(MapId map) override
    {
        Log::Info("entered map {}", map);
    }
```

Only the events you override are connected, when the extension starts: the others cost nothing.
`OnStart` runs then, and `OnStop` when the extension stops.

## Handlers

`On<Tag>(handler)` (`wxl/Listener.hpp`) connects a lambda to an event; its argument is the event's
args struct, with named fields (`auto&` works). The tag is the event's name without `On`:
`On<Update>`, `On<WorldEnter>`, `On<UnitDamage>`. The connection belongs to the extension and ends when
it stops; the `Listener` it answers can `Stop()` it earlier.

<!-- quote: examples/events/src/Events.cpp -->
```cpp
        // Before every other OnUpdate handler, and only once.
        Once<Update>([](auto& e)
        {
            Log::Info("first frame at {} ms", e.timeMs);
        }, priority::First);
```

- **Priority**: the last argument; lower runs first (`priority::First`, `Default`, `Last`, or any
  number between); equal ones in connection order. Overrides run at `Default`.
- **Once**: `Once<Tag>` ends the connection after the first call.
- **Captures**: a handler keeps its captures in a fixed slot of 32 bytes (a `this`, a few
  references or values); a larger capture does not compile: capture a pointer to the state instead.
- **Failures**: `On` logs why it could not connect; `TryOn<Tag>` answers a `Result<Listener>` instead.

## Signals

A signal is an event refined before it reaches the handler: about one object, filtered, or reshaped.
The object API hands them out (`unit.HealthChanged()`, `unit.Died()`, see `examples/observe`), as do
`Game()` and `Settings::Changed()`; `SignalOf<Tag>()` makes one from an event. `On(signal, handler)`
connects it for the extension:

<!-- quote: examples/events/src/Events.cpp -->
```cpp
        auto f12 = SignalOf<Input>().Filter([](const InputArgs& e)
        {
            return e.message == kKeyDown && e.wparam == kF12;
        });
```

- **Filter**: the handler runs only for args the predicate accepts; filters add up.
- **About one object**: `SignalOf<Tag>(handle)` only hears events about that object, and ends when it
  leaves view.
- In a coroutine, `co_await signal.Next(timeout)` waits for the next one (`wxl/Task.hpp`,
  `examples/settings`).

## Answers

An event can ask a question: `OnInput` (`*handled`: keep the message from the game). Handlers answer
through the pointer field, in priority order; each sees what the ones before it left, and the last word
stands.

<!-- quote: examples/events/src/Events.cpp -->
```cpp
        On(f12, [](const InputArgs& e)
        {
            *e.handled = true;
        });
```

In an override, the same answer is the `bool& handled` parameter of `OnInput`.

A handler that throws or faults disables the extension; the answer it was writing is undone.
