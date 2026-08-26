# C Event Bus

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
- **Subscribe can fail during a publish** if the type is at capacity: slots
  freed earlier in the same dispatch are reclaimed only after it returns.
- **Single-threaded.** No internal locking — call from one thread only.
- **Bounded recursion.** Nested publishes are capped at
  `ES_MAX_DISPATCH_DEPTH` levels (default 32). Hitting the cap drops
  the event and asserts in debug builds.

## Complexity & memory
- `subscribe` / `unsubscribe` / `publish`: **O(n)** in handlers per type.
- Storage is preallocated: `event_type_count × ES_MAX_HANDLERS_PER_TYPE` slots — fixed cost, no reallocs.

## Usage

```c
#include "es/event_system.h"
#include <stdio.h>

typedef enum : int32_t {
    EV_PLAYER_DAMAGED,
    EV_ENEMY_KILLED,
    EV_COUNT
} GameEvent;

typedef struct { int hp; } DamageEvt;

void on_player_damaged(const es_Event *ev, es_EventBus *bus, void *ctx) {
    (void) bus;
    (void) ctx;

    ES_EV_EXPECT(ev, DamageEvt);
    const DamageEvt *d = ES_EV_CPTR(ev, DamageEvt);
    printf("player lost %d hp\n", d->hp);
}

int main(void) {
    es_EventBus *bus = es_bus_create(EV_COUNT);

    [[maybe_unused]] bool ok = es_subscribe(bus, EV_PLAYER_DAMAGED, on_player_damaged, nullptr);
    assert(ok);

    DamageEvt d = {.hp = 5};
    ES_PUBLISH(bus, EV_PLAYER_DAMAGED, d);

    es_bus_destroy(bus);
}
```

> Note: public API is marked `[[nodiscard]]` — don't silently drop return values.
> Never wrap the calls themselves in `assert()`: under `-DNDEBUG` the argument
> isn't evaluated and nothing runs. Assign, then assert (as above).

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
