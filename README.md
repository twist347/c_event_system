# C Event Bus

[![ci](https://github.com/twist347/c_event_bus/actions/workflows/ci.yml/badge.svg)](https://github.com/twist347/c_event_bus/actions/workflows/ci.yml)

A tiny synchronous event bus for **C23** (callable from **C++20**).  
FIFO delivery, safe subscribe/unsubscribe even from inside handlers.

## Features
- **Synchronous** `publish`: handlers run before the call returns.
- **FIFO order**: handlers are invoked in registration order.
- **Mutation-safe dispatch**: unsubscribing inside a handler takes effect immediately; subscribing never feeds the event in flight.
- **Zero-copy payloads**: pass a pointer + size; no allocations in the hot path.
- **C++ friendly**: functions are `extern "C"`.
- **No deps** beyond the standard library.

## Caveats
- **Unsubscribe is immediate, so the result depends on registration order.**
  A handler removed during a publish is skipped, unless the dispatch had already
  reached it. Freeing that handler's `ctx` on the spot is safe.
- **Capped at `EB_MAX_HANDLERS_PER_TYPE` (32) handlers per event type.**
  `eb_subscribe` returns `false` once a type is full.
- **Subscribe can fail during a publish** if the type is at capacity: slots
  freed earlier in the same dispatch are reclaimed only after it returns.
- **Single-threaded.** No internal locking — call from one thread only.
- **Bounded recursion.** Nested publishes are capped at
  `EB_MAX_DISPATCH_DEPTH` levels (default 32). Hitting the cap drops
  the event and asserts in debug builds.

## Complexity & memory
- `subscribe` / `unsubscribe` / `publish`: **O(n)** in handlers per type.
- Storage is preallocated: `event_type_count × EB_MAX_HANDLERS_PER_TYPE` slots — fixed cost, no reallocs.

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
