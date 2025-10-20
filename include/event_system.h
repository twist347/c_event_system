#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "event.h"

#ifdef __cplusplus
extern "C" {


#endif

#ifdef __cplusplus
#define ES_ALIGNOF(T) alignof(T)
#else
#define ES_ALIGNOF(T) _Alignof(T)
#endif

#define ES_EV_EXPECT(ev, T)                                                           \
do {                                                                                  \
    assert((ev) != NULL);                                                             \
    assert(es_get_event_data((ev)) != NULL);                                          \
    assert(((uintptr_t)es_get_event_data((ev)) % (uintptr_t)ES_ALIGNOF(T)) == 0);     \
    assert(es_get_event_data_size((ev)) == sizeof(T));                                \
} while (0)

#define ES_EV_VAL(ev, T)     (*(const T *)(es_get_event_data((ev))))
#define ES_EV_CPTR(ev, T)    ((const T *)(es_get_event_data((ev))))

#define ES_CTX_EXPECT(ctx, T)                                       \
do {                                                                \
    assert((ctx) != NULL);                                          \
    assert(((uintptr_t)(ctx) % (uintptr_t)ES_ALIGNOF(T)) == 0);     \
} while (0)

#define ES_PUBLISH_LDATA(bus, type, val)                            \
do {                                                                \
    es_publish_data((bus), (type), &(val), sizeof(val));            \
} while (0)

#define ES_PUBLISH_RDATA(bus, type, T, expr)                        \
do {                                                                \
    T _es_tmp = (expr);                                             \
    es_publish_data((bus), (type), &_es_tmp, sizeof(T));            \
} while (0)

#define ES_CTX_VAL(ctx, T)     (*(const T *)(ctx))
#define ES_CTX_PTR(ctx, T)     ((T *)(ctx))
#define ES_CTX_CPTR(ctx, T)    ((const T *)(ctx))

#ifndef ES_MAX_HANDLERS_PER_TYPE
#define ES_MAX_HANDLERS_PER_TYPE 32
#endif

typedef struct es_event_t es_event_t;
typedef struct es_event_bus_t es_event_bus_t;
typedef void (*es_event_handler_f)(const es_event_t *, es_event_bus_t *, void *);

es_event_bus_t *es_bus_create(void);
void es_bus_destroy(es_event_bus_t *bus);

es_event_type_e es_get_event_type(const es_event_t *event);
const void *es_get_event_data(const es_event_t *event);
size_t es_get_event_data_size(const es_event_t *event);

bool es_subscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx);
bool es_unsubscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx);
void es_unsubscribe_all(es_event_bus_t *bus, es_event_type_e type);

bool es_publish_data(es_event_bus_t *bus, es_event_type_e type, const void *data, size_t data_size);
bool es_publish(es_event_bus_t *bus, es_event_type_e type);

#ifdef   __cplusplus
}
#endif
