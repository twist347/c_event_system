#pragma once

// Synchronous event bus: publish runs every handler before it returns.
// Single-threaded.

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#ifdef __cplusplus
#include <type_traits>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========== event ========== */

typedef int32_t es_EventType;
typedef struct es_Event es_Event;

/// The type this event was published with.
[[nodiscard]]
es_EventType es_ev_type(const es_Event *ev);

/// The payload handed to es_publish_data: borrowed, valid only for the duration
/// of the handler call. Copy it out with ES_EV_LOAD to keep it.
[[nodiscard]]
const void *es_ev_data(const es_Event *ev);

/// Payload size in bytes; 0 when the event carries none.
[[nodiscard]]
size_t es_ev_data_size(const es_Event *ev);

/// Asserts the payload really is a T. Compiled out under NDEBUG.
#define ES_EV_EXPECT(ev, T)                                                 \
do {                                                                        \
    [[maybe_unused]] const es_Event *es_ev_ = (ev);                         \
    assert(es_ev_);                                                         \
    assert(es_ev_data(es_ev_));                                             \
    assert(((uintptr_t) es_ev_data(es_ev_) % (uintptr_t) alignof(T)) == 0); \
    assert(es_ev_data_size(es_ev_) == sizeof(T));                           \
} while (0)

/// Read the payload in place. Run ES_EV_EXPECT first.
#define ES_EV_VAL(ev, T)     (*(const T *)(es_ev_data((ev))))
#define ES_EV_CPTR(ev, T)    ((const T *)(es_ev_data((ev))))

/// Copy the payload into dst -- the only form that outlives the dispatch.
#define ES_EV_LOAD(ev, T, dst)                     \
do {                                               \
    const es_Event *es_ev_ = (ev);                 \
    ES_EV_EXPECT(es_ev_, T);                       \
    memcpy(&(dst), es_ev_data(es_ev_), sizeof(T)); \
} while (0)

/* ========== event bus ========== */

typedef struct es_EventBus es_EventBus;

/// Valid event types are [0, event_type_count). nullptr on allocation failure.
[[nodiscard]]
es_EventBus *es_bus_create(size_t event_type_count);

/// Safe on nullptr.
void es_bus_destroy(es_EventBus *bus);

/// Drops every subscription, keeps the allocation.
void es_bus_reset(es_EventBus *bus);

/* ========== event bus subs ========== */

/// Hard cap per event type; es_subscribe fails past it.
constexpr size_t ES_MAX_HANDLERS_PER_TYPE = 32;

typedef void (*es_EventHandler)(const es_Event *ev, es_EventBus *bus, void *ctx);

/// Subscribing during dispatch does not affect the event in flight. It fails if
/// the type is at capacity, even when it holds slots freed earlier in the same
/// dispatch (those are reclaimed only after the outermost dispatch returns).
[[nodiscard]]
bool es_subscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx);

/// Every removal below takes effect immediately: a handler removed during
/// dispatch is not called for the event in flight unless the dispatch already
/// reached it, which makes it safe to free its ctx from inside another handler.
[[nodiscard]]
bool es_unsubscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx);

/// Removes every handler on `type`. Does nothing for an invalid one.
void es_unsubscribe_by_type(es_EventBus *bus, es_EventType type);

/// Removes every subscription with this ctx. Returns how many were removed.
[[nodiscard]]
size_t es_unsubscribe_by_ctx(es_EventBus *bus, const void *ctx);

/// Same, matched by handler instead of ctx.
[[nodiscard]]
size_t es_unsubscribe_by_handler(es_EventBus *bus, es_EventHandler handler);

/// Live handlers on `type`, 0 for an invalid one. Handlers removed earlier in an
/// ongoing dispatch are already excluded.
[[nodiscard]]
size_t es_count_subscribers(const es_EventBus *bus, es_EventType type);

/// Asserts ctx is non-null and aligned for T. Compiled out under NDEBUG.
#define ES_CTX_EXPECT(ctx, T)                                    \
do {                                                             \
    [[maybe_unused]] const void *es_ctx_ = (ctx);                \
    assert(es_ctx_);                                             \
    assert(((uintptr_t) es_ctx_ % (uintptr_t) alignof(T)) == 0); \
} while (0)

/// Read ctx as it was passed to es_subscribe.
#define ES_CTX_VAL(ctx, T)     (*(const T *)(ctx))
#define ES_CTX_PTR(ctx, T)     ((T *)(ctx))
#define ES_CTX_CPTR(ctx, T)    ((const T *)(ctx))

/* ========== event bus publish ========== */

/// Nesting limit for publishes made from inside a handler. Past it the event is
/// dropped and es_publish_data returns false.
constexpr size_t ES_MAX_DISPATCH_DEPTH = 32;

/// `data` is borrowed, never copied -- it must outlive the call. Returns false
/// on an invalid type or on exceeding the nesting limit; no subscribers is true.
[[nodiscard]]
bool es_publish_data(es_EventBus *bus, es_EventType type, const void *data, size_t data_size);

/// Publishes with no payload.
[[nodiscard]]
bool es_publish(es_EventBus *bus, es_EventType type);

#ifdef __cplusplus
#define ES_UNQUAL_(...) std::remove_cvref_t<decltype(__VA_ARGS__)>
#else
#define ES_UNQUAL_(...) typeof_unqual(__VA_ARGS__)
#endif

/// Publishes a copy of the expression as the payload, discarding the result.
/// Variadic so that a braced initializer with commas needs no extra parens.
#define ES_PUBLISH(bus, type, ...)                                    \
do {                                                                  \
    ES_UNQUAL_(__VA_ARGS__) es_tmp_ = (__VA_ARGS__);                  \
    (void) es_publish_data((bus), (type), &es_tmp_, sizeof(es_tmp_)); \
} while (0)

#ifdef __cplusplus
}
#endif
