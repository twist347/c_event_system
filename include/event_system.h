#pragma once

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

// === event payload access ===

#define ES_EV_EXPECT(ev, T)                                                     \
do {                                                                            \
    [[maybe_unused]] const es_Event *es_ev_ = (ev);                             \
    assert(es_ev_);                                                             \
    assert(es_ev_get_data(es_ev_));                                             \
    assert(((uintptr_t) es_ev_get_data(es_ev_) % (uintptr_t) alignof(T)) == 0); \
    assert(es_ev_get_data_size(es_ev_) == sizeof(T));                           \
} while (0)

#define ES_EV_VAL(ev, T)     (*(const T *)(es_ev_get_data((ev))))
#define ES_EV_CPTR(ev, T)    ((const T *)(es_ev_get_data((ev))))

#define ES_EV_LOAD(ev, T, dst)                         \
do {                                                   \
    const es_Event *es_ev_ = (ev);                     \
    ES_EV_EXPECT(es_ev_, T);                           \
    memcpy(&(dst), es_ev_get_data(es_ev_), sizeof(T)); \
} while (0)

// === handler ctx access ===

#define ES_CTX_EXPECT(ctx, T)                                    \
do {                                                             \
    [[maybe_unused]] const void *es_ctx_ = (ctx);                \
    assert(es_ctx_);                                             \
    assert(((uintptr_t) es_ctx_ % (uintptr_t) alignof(T)) == 0); \
} while (0)

#define ES_CTX_VAL(ctx, T)     (*(const T *)(ctx))
#define ES_CTX_PTR(ctx, T)     ((T *)(ctx))
#define ES_CTX_CPTR(ctx, T)    ((const T *)(ctx))

// === public helpers ===

#ifdef __cplusplus
#define ES_UNQUAL_(expr) std::remove_cvref_t<decltype(expr)>
#else
#define ES_UNQUAL_(expr) typeof_unqual(expr)
#endif

#define ES_PUBLISH(bus, type, expr)                                   \
do {                                                                  \
    ES_UNQUAL_(expr) es_tmp_ = (expr);                                \
    (void) es_publish_data((bus), (type), &es_tmp_, sizeof(es_tmp_)); \
} while (0)

// Declares a handler. In the body the event is `ev`, the bus is `bus`, the user ctx is `ctx`.
#define ES_HANDLER(name) \
    void name(const es_Event *ev, es_EventBus *bus, void *ctx)

constexpr size_t ES_MAX_HANDLERS_PER_TYPE = 32;
constexpr size_t ES_MAX_DISPATCH_DEPTH = 32;

typedef int32_t es_EventType;

typedef struct es_Event es_Event;
typedef struct es_EventBus es_EventBus;
typedef void (*es_EventHandler)(const es_Event *, es_EventBus *, void *);

[[nodiscard]] es_EventBus *es_bus_create(size_t event_type_count);
void es_bus_destroy(es_EventBus *bus);
void es_bus_reset(es_EventBus *bus);

[[nodiscard]] es_EventType es_ev_get_type(const es_Event *ev);
[[nodiscard]] const void *es_ev_get_data(const es_Event *ev);
[[nodiscard]] size_t es_ev_get_data_size(const es_Event *ev);

[[nodiscard]] bool es_subscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx);
[[nodiscard]] bool es_unsubscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx);
void es_unsubscribe_by_type(es_EventBus *bus, es_EventType type);
[[nodiscard]] size_t es_unsubscribe_by_ctx(es_EventBus *bus, const void *ctx);
[[nodiscard]] size_t es_unsubscribe_by_handler(es_EventBus *bus, es_EventHandler handler);

[[nodiscard]] bool es_publish_data(es_EventBus *bus, es_EventType type, const void *data, size_t data_size);
[[nodiscard]] bool es_publish(es_EventBus *bus, es_EventType type);

#ifdef __cplusplus
}
#endif
