# C Event Bus

A tiny synchronous event bus for **C23** (callable from **C++20**).  
FIFO delivery, safe subscribe/unsubscribe even from inside handlers (via snapshotting).

## Features
- **Synchronous** `publish`: handlers run before the call returns.
- **FIFO order**: handlers are invoked in registration order.
- **Stable iteration**: subscribing/unsubscribing inside a handler doesn't affect the current dispatch (snapshot).
- **Zero-copy payloads**: pass a pointer + size; no allocations in the hot path.
- **C++ friendly**: functions are `extern "C"`.
- **No deps** beyond the standard library.

## Caveats
- **Snapshot is taken at the start of dispatch.** If a handler unsubscribes
  itself or another handler during a publish, the already-snapshotted entries
  will still be invoked for that publish. Do not free a handler's `ctx`
  before the publish call returns.
- **Single-threaded.** No internal locking — call from one thread only.
- **Bounded recursion.** Nested publishes are capped at
  `ES_MAX_DISPATCH_DEPTH` levels (default 32). Hitting the cap drops
  the event and asserts in debug builds.

## Complexity & memory
- `subscribe` / `unsubscribe` / `publish`: **O(n)** in handlers per type.
- Storage is preallocated: `event_type_count × ES_MAX_HANDLERS_PER_TYPE` slots — fixed cost, no reallocs.

## Usage

```c
#include "event_system.h"
#include <stdio.h>

typedef enum : int32_t {
    EV_PLAYER_DAMAGED,
    EV_ENEMY_KILLED,
    EV_COUNT
} GameEvent;

typedef struct { int hp; } DamageEvt;

ES_HANDLER(on_player_damaged) {
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
