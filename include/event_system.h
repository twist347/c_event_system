#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#define ES_ALIGNOF(T) alignof(T)
#else
#define ES_ALIGNOF(T) _Alignof(T)
#endif

// === event payload access ===

#define ES_EV_EXPECT(ev, T)                                                           \
do {                                                                                  \
    const es_event_t *es_ev_ = (ev);                                                  \
    assert(es_ev_ != NULL);                                                           \
    assert(es_event_get_data(es_ev_) != NULL);                                        \
    assert(((uintptr_t) es_event_get_data(es_ev_) % (uintptr_t) ES_ALIGNOF(T)) == 0); \
    assert(es_event_get_data_size(es_ev_) == sizeof(T));                              \
} while (0)

#define ES_EV_VAL(ev, T)     (*(const T *)(es_event_get_data((ev))))
#define ES_EV_CPTR(ev, T)    ((const T *)(es_event_get_data((ev))))

#define ES_EV_LOAD(ev, T, dst)                            \
do {                                                      \
    const es_event_t *es_ev_ = (ev);                      \
    ES_EV_EXPECT(es_ev_, T);                              \
    memcpy(&(dst), es_event_get_data(es_ev_), sizeof(T)); \
} while (0)

// === handler ctx access ===

#define ES_CTX_EXPECT(ctx, T)                                       \
do {                                                                \
    const void *es_ctx_ = (ctx);                                    \
    assert(es_ctx_ != NULL);                                        \
    assert(((uintptr_t) es_ctx_ % (uintptr_t) ES_ALIGNOF(T)) == 0); \
} while (0)

#define ES_CTX_VAL(ctx, T)     (*(const T *)(ctx))
#define ES_CTX_PTR(ctx, T)     ((T *)(ctx))
#define ES_CTX_CPTR(ctx, T)    ((const T *)(ctx))

// === publish helpers ===

#define ES_PUBLISH(bus, type, T, expr)                   \
do {                                                     \
    T es_tmp_ = (expr);                                  \
    es_publish_data((bus), (type), &es_tmp_, sizeof(T)); \
} while (0)

#define ES_HANDLER(name) \
    void name(const es_event_t *event, es_event_bus_t *bus, void *ctx)

#ifndef ES_MAX_HANDLERS_PER_TYPE
#define ES_MAX_HANDLERS_PER_TYPE 32
#endif

#ifndef ES_MAX_DISPATCH_DEPTH
#define ES_MAX_DISPATCH_DEPTH 32
#endif

typedef int es_event_type_t;

typedef struct es_event_t es_event_t;
typedef struct es_event_bus_t es_event_bus_t;
typedef void (*es_event_handler_fn)(const es_event_t *, es_event_bus_t *, void *);

es_event_bus_t *es_bus_create(size_t event_type_count);
void es_bus_destroy(es_event_bus_t *bus);
void es_bus_reset(es_event_bus_t *bus);

es_event_type_t es_event_get_type(const es_event_t *event);
const void *es_event_get_data(const es_event_t *event);
size_t es_event_get_data_size(const es_event_t *event);

bool es_subscribe(es_event_bus_t *bus, es_event_type_t type, es_event_handler_fn handler, void *ctx);
bool es_unsubscribe(es_event_bus_t *bus, es_event_type_t type, es_event_handler_fn handler, void *ctx);
void es_unsubscribe_by_type(es_event_bus_t *bus, es_event_type_t type);
size_t es_unsubscribe_by_ctx(es_event_bus_t *bus, void *ctx);
size_t es_unsubscribe_by_handler(es_event_bus_t *bus, es_event_handler_fn handler);

bool es_publish_data(es_event_bus_t *bus, es_event_type_t type, const void *data, size_t data_size);
bool es_publish(es_event_bus_t *bus, es_event_type_t type);

#ifdef   __cplusplus
}
#endif
