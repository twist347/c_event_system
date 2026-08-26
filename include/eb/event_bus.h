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

typedef int32_t eb_EventType;
typedef struct eb_Event eb_Event;

/// The type this event was published with.
[[nodiscard]]
eb_EventType eb_ev_type(const eb_Event *ev);

/// The payload handed to eb_publish_data: borrowed, valid only for the duration
/// of the handler call. Copy it out with EB_EV_LOAD to keep it.
[[nodiscard]]
const void *eb_ev_data(const eb_Event *ev);

/// Payload size in bytes; 0 when the event carries none.
[[nodiscard]]
size_t eb_ev_data_size(const eb_Event *ev);

/// Asserts the payload really is a T. Compiled out under NDEBUG.
#define EB_EV_EXPECT(ev, T)                                                 \
do {                                                                        \
    [[maybe_unused]] const eb_Event *eb_ev_ = (ev);                         \
    assert(eb_ev_);                                                         \
    assert(eb_ev_data(eb_ev_));                                             \
    assert(((uintptr_t) eb_ev_data(eb_ev_) % (uintptr_t) alignof(T)) == 0); \
    assert(eb_ev_data_size(eb_ev_) == sizeof(T));                           \
} while (0)

/// Read the payload in place. Run EB_EV_EXPECT first.
#define EB_EV_VAL(ev, T)     (*(const T *)(eb_ev_data((ev))))
#define EB_EV_CPTR(ev, T)    ((const T *)(eb_ev_data((ev))))

/// Copy the payload into dst -- the only form that outlives the dispatch.
#define EB_EV_LOAD(ev, T, dst)                     \
do {                                               \
    const eb_Event *eb_ev_ = (ev);                 \
    EB_EV_EXPECT(eb_ev_, T);                       \
    memcpy(&(dst), eb_ev_data(eb_ev_), sizeof(T)); \
} while (0)

/* ========== event bus ========== */

typedef struct eb_EventBus eb_EventBus;

/// Valid event types are [0, event_type_count). nullptr on allocation failure.
[[nodiscard]]
eb_EventBus *eb_bus_create(size_t event_type_count);

/// Safe on nullptr.
void eb_bus_destroy(eb_EventBus *bus);

/// Drops every subscription, keeps the allocation.
void eb_bus_reset(eb_EventBus *bus);

/* ========== event bus subs ========== */

/// Hard cap per event type; eb_subscribe fails past it.
constexpr size_t EB_MAX_HANDLERS_PER_TYPE = 32;

typedef void (*eb_EventHandler)(const eb_Event *ev, eb_EventBus *bus, void *ctx);

/// Subscribing during dispatch does not affect the event in flight. It fails if
/// the type is at capacity, even when it holds slots freed earlier in the same
/// dispatch (those are reclaimed only after the outermost dispatch returns).
[[nodiscard]]
bool eb_subscribe(eb_EventBus *bus, eb_EventType type, eb_EventHandler handler, void *ctx);

/// Every removal below takes effect immediately: a handler removed during
/// dispatch is not called for the event in flight unless the dispatch already
/// reached it, which makes it safe to free its ctx from inside another handler.
[[nodiscard]]
bool eb_unsubscribe(eb_EventBus *bus, eb_EventType type, eb_EventHandler handler, void *ctx);

/// Removes every handler on `type`. Does nothing for an invalid one.
void eb_unsubscribe_by_type(eb_EventBus *bus, eb_EventType type);

/// Removes every subscription with this ctx. Returns how many were removed.
[[nodiscard]]
size_t eb_unsubscribe_by_ctx(eb_EventBus *bus, const void *ctx);

/// Same, matched by handler instead of ctx.
[[nodiscard]]
size_t eb_unsubscribe_by_handler(eb_EventBus *bus, eb_EventHandler handler);

/// Live handlers on `type`, 0 for an invalid one. Handlers removed earlier in an
/// ongoing dispatch are already excluded.
[[nodiscard]]
size_t eb_count_subscribers(const eb_EventBus *bus, eb_EventType type);

/// Asserts ctx is non-null and aligned for T. Compiled out under NDEBUG.
#define EB_CTX_EXPECT(ctx, T)                                    \
do {                                                             \
    [[maybe_unused]] const void *eb_ctx_ = (ctx);                \
    assert(eb_ctx_);                                             \
    assert(((uintptr_t) eb_ctx_ % (uintptr_t) alignof(T)) == 0); \
} while (0)

/// Read ctx as it was passed to eb_subscribe.
#define EB_CTX_VAL(ctx, T)     (*(const T *)(ctx))
#define EB_CTX_PTR(ctx, T)     ((T *)(ctx))
#define EB_CTX_CPTR(ctx, T)    ((const T *)(ctx))

/* ========== event bus publish ========== */

/// Nesting limit for publishes made from inside a handler. Past it the event is
/// dropped and eb_publish_data returns false.
constexpr size_t EB_MAX_DISPATCH_DEPTH = 32;

/// `data` is borrowed, never copied -- it must outlive the call. Returns false
/// on an invalid type or on exceeding the nesting limit; no subscribers is true.
[[nodiscard]]
bool eb_publish_data(eb_EventBus *bus, eb_EventType type, const void *data, size_t data_size);

/// Publishes with no payload.
[[nodiscard]]
bool eb_publish(eb_EventBus *bus, eb_EventType type);

#ifdef __cplusplus
#define EB_UNQUAL_(...) std::remove_cvref_t<decltype(__VA_ARGS__)>
#else
#define EB_UNQUAL_(...) typeof_unqual(__VA_ARGS__)
#endif

/// Publishes a copy of the expression as the payload, discarding the result.
/// Variadic so that a braced initializer with commas needs no extra parens.
#define EB_PUBLISH(bus, type, ...)                                    \
do {                                                                  \
    EB_UNQUAL_(__VA_ARGS__) eb_tmp_ = (__VA_ARGS__);                  \
    (void) eb_publish_data((bus), (type), &eb_tmp_, sizeof(eb_tmp_)); \
} while (0)

#ifdef __cplusplus
}
#endif
