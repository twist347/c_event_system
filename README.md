# C Event Bus

[![ci](https://github.com/twist347/c_event_bus/actions/workflows/ci.yml/badge.svg)](https://github.com/twist347/c_event_bus/actions/workflows/ci.yml)

A tiny synchronous event bus for **C23** (callable from **C++20**).  
FIFO delivery, safe subscribe/unsubscribe even from inside handlers, and an
optional deferred queue for work that must wait until the frame settles.

## Features
- **Synchronous** `publish`: handlers run before the call returns.
- **Deferred** `post` + `drain`: queue an event now, dispatch it at a point you
  choose. Flattens nested publishes and defers destructive work out of an
  iteration.
- **FIFO order**: handlers are invoked in registration order.
- **Mutation-safe dispatch**: unsubscribing inside a handler takes effect immediately; subscribing never feeds the event in flight.
- **Zero-copy payloads**: pass a pointer + size; no allocations in the hot path.
- **No subscriber cap**: the per-type list grows on demand, with `eb_bus_reserve`
  to pre-allocate and `eb_bus_shrink_to_fit` to hand memory back.
- **C++ friendly**: functions are `extern "C"`.
- **No deps** beyond the standard library.

## Caveats
- **Unsubscribe is immediate, so the result depends on registration order.**
  A handler removed during a publish is skipped, unless the dispatch had already
  reached it. Freeing that handler's `ctx` on the spot is safe.
- **Subscribe allocates**, so it returns `false` if the subscriber list cannot
  grow. Growing from inside a handler is safe: the dispatch in flight follows
  the moved array.
- **Single-threaded.** No internal locking — call from one thread only.
- **Bounded recursion.** Nested publishes are capped at
  `EB_MAX_DISPATCH_DEPTH` levels (default 32). Hitting the cap drops
  the event and asserts in debug builds.
- **`publish` borrows the payload, `post` copies it.** A posted payload outlives
  the caller's frame, so it has to be copied, and it is capped at
  `EB_MAX_POST_PAYLOAD` bytes (default 64). This is the one asymmetry between
  the two paths.
- **`drain` handles the events queued as of entry**, never more. A post made
  from a handler waits for the next drain, so two handlers posting to each other
  cannot keep a drain alive. `drain` is not callable from a handler: it
  returns 0.

## Complexity & memory
- `subscribe` / `unsubscribe` / `publish`: **O(n)** in handlers per type.
- `subscribe` is amortized O(1) past the duplicate check: capacity doubles from 4.
- A bus costs `event_type_count` empty vectors up front; slots are allocated per
  type on first subscribe. Vectors never shrink — compaction and `eb_bus_reset`
  reclaim slots but keep the capacity.
- Dead slots are swept only once the outermost dispatch returns, so heavy
  subscribe/unsubscribe churn *inside* a single dispatch grows a vector by the
  churn rather than by the live count. `eb_bus_shrink_to_fit` is the way back.
- The post queue is `EB_POST_QUEUE_CAP` fixed-size slots (default 256 x 80 B
  = 20 KB), allocated on the first `post` — a bus that never posts never pays.
  `post` and `drain` never allocate. A full queue makes `post` return `false`;
  during a drain one slot is held by the event in flight.
- `eb_bus_reserve(bus, type, n)` up front makes the following `n` subscribes
  allocation-free, which is what you want if the bus must not allocate after
  init. It is not a speed knob — doubling already costs ~6 reallocs to reach 100
  subscribers, all of them at startup.

## Usage

### Immediate

```c
#include "eb/event_bus.h"
#include <stdio.h>

typedef enum : int32_t {
    EV_PLAYER_DAMAGED,
    EV_ENEMY_KILLED,
    EV_COUNT
} GameEvent;

typedef struct { int hp; } DamageEvt;

void on_player_damaged(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    (void) bus;
    (void) ctx;

    EB_EV_EXPECT(ev, DamageEvt);
    const DamageEvt *d = EB_EV_CPTR(ev, DamageEvt);
    printf("player lost %d hp\n", d->hp);
}

int main(void) {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);

    [[maybe_unused]] bool ok = eb_subscribe(bus, EV_PLAYER_DAMAGED, on_player_damaged, nullptr);
    assert(ok);

    DamageEvt d = {.hp = 5};
    EB_PUBLISH(bus, EV_PLAYER_DAMAGED, d);

    eb_bus_destroy(bus);
}
```

### Deferred

Same enum, same handler, same subscription — only the delivery changes:

```c
int main(void) {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);

    [[maybe_unused]] bool ok = eb_subscribe(bus, EV_PLAYER_DAMAGED, on_player_damaged, nullptr);
    assert(ok);

    while (running) {
        DamageEvt d = {.hp = 5};
        EB_POST(bus, EV_PLAYER_DAMAGED, d); // queued; d may die on the next line

        update_the_world();

        (void) eb_drain(bus); // the handler runs here, once the frame settled
    }

    eb_bus_destroy(bus);
}
```

Runnable versions of both live in [`examples/`](examples).

> Note: public API is marked `[[nodiscard]]` — don't silently drop return values.
> Never wrap the calls themselves in `assert()`: under `-DNDEBUG` the argument
> isn't evaluated and nothing runs. Assign, then assert (as above).

## Requirements

GCC or Clang with **C23** support (tested on Linux; macOS and the BSDs should
work as-is). MSVC is not supported.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Testing

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests and the examples are built only when this project is the top-level
one, so consuming it via `FetchContent` or `add_subdirectory` does not pull them
in.

## Examples

Two runnable programs, one per delivery model:

- `examples/immediate.c` — `eb_publish`. An enemy dies inside the damage
  dispatch and frees itself on the spot; the handler registered after it never
  sees the fatal hit.
- `examples/deferred.c` — `eb_post` + `eb_drain` in a frame loop. Deaths are
  queued so nothing is freed while the tick dispatch is still walking the
  subscribers, and the loot a reaper posts lands one drain later, which is the
  snapshot rule in plain sight.

## Integration

Linking against the target propagates the include path and `-std=c23`, so
nothing has to be repeated on your side.

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(c_event_bus
    GIT_REPOSITORY https://github.com/twist347/c_event_bus.git
    GIT_TAG main)
FetchContent_MakeAvailable(c_event_bus)

target_link_libraries(my_app PRIVATE eb::event_bus)
```

### Submodule

```cmake
add_subdirectory(third_party/c_event_bus)
target_link_libraries(my_app PRIVATE eb::event_bus)
```

### Vendoring

It is one `.c` file — dropping `src/event_bus.c` and `include/eb/` straight
into your own tree works too. Compile with `-std=c23`.
