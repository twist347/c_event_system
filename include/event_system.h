#pragma once

#include "event.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define ES_EV_EXPECT(ev, T)  do {                                   \
    assert((ev) != NULL);                                           \
    assert((ev)->data != NULL);                                     \
    assert((ev)->data_size == sizeof(T));                           \
} while (0)

#define ES_EV_VAL(ev, T)     (*(const T *)((ev)->data))
#define ES_EV_CPTR(ev, T)    ((const T *)((ev)->data))

#define ES_CTX_EXPECT(ctx, T) do {                                  \
    assert((ctx) != NULL);                                          \
    assert(((uintptr_t)(ctx) % (uintptr_t)_Alignof(T)) == 0);       \
} while (0)

#define ES_CTX_VAL(ctx, T)     (*(const T *)(ctx))
#define ES_CTX_PTR(ctx, T)     ((T *)(ctx))
#define ES_CTX_CPTR(ctx, T)    ((const T *)(ctx))

#define ES_MAX_HANDLERS_PER_TYPE 32

typedef struct {
    es_event_type_e type;
    const void *data;
    size_t data_size;
} es_event_t;

typedef struct es_event_bus_t es_event_bus_t;

typedef void (*es_event_handler_f)(const es_event_t *, es_event_bus_t *, void *);

typedef struct {
    es_event_handler_f handler;
    void *ctx;
} es_event_handler_with_ctx_t;

struct es_event_bus_t {
    es_event_handler_with_ctx_t handlers[EV_TYPE__COUNT][ES_MAX_HANDLERS_PER_TYPE];
    unsigned int num_handlers_by_event[EV_TYPE__COUNT];
};

es_event_bus_t *es_create(void);
void es_destroy(es_event_bus_t *bus);

bool es_subscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx);
bool es_unsubscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx);
void es_unsubscribe_all(es_event_bus_t *bus, es_event_type_e type);

bool es_publish_ev(es_event_bus_t *bus, const es_event_t *event);
bool es_publish(es_event_bus_t *bus, es_event_type_e type);
