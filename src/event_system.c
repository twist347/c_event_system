#include "event_system.h"

#include <stdlib.h>
#include <string.h>

#define ES_VALID_TYPE(t) ((size_t)(t) < (size_t)EV_TYPE__COUNT)

struct es_event_t {
    es_event_type_e type;
    const void *data;
    size_t data_size;
};

struct es_event_handler_with_ctx_t {
    es_event_handler_f handler;
    void *ctx;
};

typedef struct es_event_handler_with_ctx_t es_event_handler_with_ctx_t;

struct es_event_bus_t {
    es_event_handler_with_ctx_t handlers[EV_TYPE__COUNT][ES_MAX_HANDLERS_PER_TYPE];
    size_t num_handlers_by_event[EV_TYPE__COUNT];
};

es_event_bus_t *es_bus_create(void) {
    es_event_bus_t *bus = malloc(sizeof(es_event_bus_t));
    if (!bus) {
        return NULL;
    }

    for (size_t t = 0; t < EV_TYPE__COUNT; ++t) {
        bus->num_handlers_by_event[t] = 0;
        for (size_t i = 0; i < ES_MAX_HANDLERS_PER_TYPE; ++i) {
            bus->handlers[t][i].handler = NULL;
            bus->handlers[t][i].ctx = NULL;
        }
    }

    return bus;
}

void es_bus_destroy(es_event_bus_t *bus) {
    free(bus);
}

es_event_type_e es_get_event_type(const es_event_t *event) {
    assert(event);

    return event->type;
}

const void *es_get_event_data(const es_event_t *event) {
    assert(event);

    return event->data;
}

size_t es_get_event_data_size(const es_event_t *event) {
    assert(event);

    return event->data_size;
}

bool es_subscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!ES_VALID_TYPE(type)) {
        return false;
    }

    // forbid duplicates
    for (size_t i = 0; i < bus->num_handlers_by_event[type]; ++i) {
        if (bus->handlers[type][i].handler == handler && bus->handlers[type][i].ctx == ctx) {
            return false;
        }
    }

    if (bus->num_handlers_by_event[type] >= ES_MAX_HANDLERS_PER_TYPE) {
        return false;
    }

    // FIFO
    bus->handlers[type][bus->num_handlers_by_event[type]].handler = handler;
    bus->handlers[type][bus->num_handlers_by_event[type]].ctx = ctx;
    ++bus->num_handlers_by_event[type];

    return true;
}

bool es_unsubscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx) {
    assert(bus);

    if (!ES_VALID_TYPE(type)) {
        return false;
    }

    const size_t n = bus->num_handlers_by_event[type];
    es_event_handler_with_ctx_t *arr = bus->handlers[type];
    for (size_t i = 0; i < n; ++i) {
        if (arr[i].handler == handler && arr[i].ctx == ctx) {
            memmove(&arr[i], &arr[i + 1], (n - i - 1) * sizeof(arr[0]));
            arr[n - 1].handler = NULL;
            arr[n - 1].ctx = NULL;
            bus->num_handlers_by_event[type] = n - 1;
            return true;
        }
    }

    return false;
}

void es_unsubscribe_all(es_event_bus_t *bus, es_event_type_e type) {
    assert(bus);

    if (!ES_VALID_TYPE(type)) {
        return;
    }

    const size_t n = bus->num_handlers_by_event[type];
    for (size_t i = 0; i < n; ++i) {
        bus->handlers[type][i].handler = NULL;
        bus->handlers[type][i].ctx = NULL;
    }
    bus->num_handlers_by_event[type] = 0;
}

bool es_publish_data(es_event_bus_t *bus, es_event_type_e type, const void *data, size_t data_size) {
    assert(bus);

    if (!ES_VALID_TYPE(type)) {
        return false;
    }

    const es_event_t event = {.type = type, .data = data, .data_size = data_size};

    const size_t n = bus->num_handlers_by_event[event.type];
    assert(n <= ES_MAX_HANDLERS_PER_TYPE);
    if (n == 0) {
        return true;
    }

    es_event_handler_with_ctx_t snap[ES_MAX_HANDLERS_PER_TYPE];
    memcpy(snap, bus->handlers[event.type], n * sizeof(es_event_handler_with_ctx_t));

    for (size_t i = 0; i < n; ++i) {
        if (snap[i].handler) {
            snap[i].handler(&event, bus, snap[i].ctx);
        }
    }
    return true;
}

bool es_publish(es_event_bus_t *bus, es_event_type_e type) {
    return es_publish_data(bus, type, NULL, 0);
}
