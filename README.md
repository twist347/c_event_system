# C Event Bus

A tiny synchronous event bus for C11.  
FIFO delivery, safe subscribe/unsubscribe even from inside handlers (via snapshotting).

## Features
- **Synchronous** `publish`: handlers run before the call returns.
- **FIFO order**: handlers are invoked in registration order.
- **Stable iteration**: subscribing/unsubscribing inside a handler doesn’t affect the current dispatch (snapshot).
- **Zero-copy payloads**: pass a pointer + size; no allocations in the hot path.
- **No deps** beyond the C standard library

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
