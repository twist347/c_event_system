# C Event Bus

[![ci](https://github.com/twist347/c_event_bus/actions/workflows/ci.yml/badge.svg)](https://github.com/twist347/c_event_bus/actions/workflows/ci.yml)

A tiny synchronous event bus for **C23** (callable from **C++20**).  
FIFO delivery, safe subscribe/unsubscribe even from inside handlers.

## Features
- **Synchronous** `publish`: handlers run before the call returns.
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

## Complexity & memory
- `subscribe` / `unsubscribe` / `publish`: **O(n)** in handlers per type.
- `subscribe` is amortized O(1) past the duplicate check: capacity doubles from 4.
- A bus costs `event_type_count` empty vectors up front; slots are allocated per
  type on first subscribe. Vectors never shrink — compaction and `eb_bus_reset`
  reclaim slots but keep the capacity.
- Dead slots are swept only once the outermost dispatch returns, so heavy
  subscribe/unsubscribe churn *inside* a single dispatch grows a vector by the
  churn rather than by the live count. `eb_bus_shrink_to_fit` is the way back.
- `eb_bus_reserve(bus, type, n)` up front makes the following `n` subscribes
  allocation-free, which is what you want if the bus must not allocate after
  init. It is not a speed knob — doubling already costs ~6 reallocs to reach 100
  subscribers, all of them at startup.

## Usage

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

The tests and the example are built only when this project is the top-level one,
so consuming it via `FetchContent` or `add_subdirectory` does not pull them in.

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

It is one `.c` file — dropping `src/event_bus.c` and `include/es/` straight
into your own tree works too. Compile with `-std=c23`.
