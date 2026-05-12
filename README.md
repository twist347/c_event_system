# C Event Bus

A tiny synchronous event bus for C11.  
FIFO delivery, safe subscribe/unsubscribe even from inside handlers (via snapshotting).

## Features
- **Synchronous** `publish`: handlers run before the call returns.
- **FIFO order**: handlers are invoked in registration order.
- **Stable iteration**: subscribing/unsubscribing inside a handler doesn’t affect the current dispatch (snapshot).
- **Zero-copy payloads**: pass a pointer + size; no allocations in the hot path.
- **No deps** beyond the C standard library

## Caveats
- **Snapshot is taken at the start of dispatch.** If a handler unsubscribes
  itself or another handler during a publish, the already-snapshotted entries
  will still be invoked for that publish. Do not free a handler's `ctx`
  before the publish call returns.
- **Single-threaded.** No internal locking — call from one thread only.
- **Bounded recursion.** Nested publishes are capped at
`ES_MAX_DISPATCH_DEPTH` levels (default 32). Hitting the cap drops
  the event and asserts in debug builds.

## Usage

  ```c
  #include "event_system.h"

  typedef enum {
      EV_PLAYER_DAMAGED,
      EV_ENEMY_KILLED, 
      EV_COUNT
  } game_event_e;

  typedef struct { int hp; } damage_evt_t;

  ES_HANDLER(on_player_damaged) {
      ES_EV_EXPECT(event, damage_evt_t);
      const damage_evt_t *d = ES_EV_CPTR(event, damage_evt_t);
      printf("player lost %d hp\n", d->hp);
  }
  
  int main(void) {
      es_event_bus_t *bus = es_bus_create(EV_COUNT);
  
      es_subscribe(bus, EV_PLAYER_DAMAGED, on_player_damaged, NULL);
  
      damage_evt_t d = {.hp = 5};
      ES_PUBLISH(bus, EV_PLAYER_DAMAGED, damage_evt_t, d);
  
      es_bus_destroy(bus); 
  }
  ```

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
