#include "event_system.h"

#include <stdlib.h>
#include <string.h>

#define EB_VALID_TYPE(t) ((unsigned)(t) < (unsigned)EV_TYPE__COUNT)

es_event_bus_t *es_create(void) {
    es_event_bus_t *bus = malloc(sizeof(es_event_bus_t));
    if (!bus) {
        return NULL;
    }

    for (unsigned int t = 0; t < EV_TYPE__COUNT; ++t) {
        bus->num_handlers_by_event[t] = 0;
        for (unsigned int i = 0; i < ES_MAX_HANDLERS_PER_TYPE; ++i) {
            bus->handlers[t][i].handler = NULL;
            bus->handlers[t][i].ctx = NULL;
        }
    }

    return bus;
}
void es_destroy(es_event_bus_t *bus) {
    assert(bus);

    free(bus);
}

bool es_subscribe(es_event_bus_t *bus, es_event_type_e type, es_event_handler_f handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!EB_VALID_TYPE(type)) {
        return false;
    }

    // forbid duplicates
    for (unsigned int i = 0; i < bus->num_handlers_by_event[type]; ++i) {
        if (bus->handlers[type][i].handler == handler && bus->handlers[type][i].ctx == ctx) {
            return true;
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

    if (!EB_VALID_TYPE(type)) {
        return false;
    }

    const unsigned int n = bus->num_handlers_by_event[type];
    for (unsigned int i = 0; i < n; ++i) {
        if (bus->handlers[type][i].handler == handler && bus->handlers[type][i].ctx == ctx) {
            memmove(&bus->handlers[type][i], &bus->handlers[type][i + 1], (n - i - 1) * sizeof(bus->handlers[type][0]));
            bus->handlers[type][n - 1].handler = NULL;
            bus->handlers[type][n - 1].ctx = NULL;
            bus->num_handlers_by_event[type] = n - 1;
            return true;
        }
    }

    return false;
}

void es_unsubscribe_all(es_event_bus_t *bus, es_event_type_e type) {
    assert(bus);

    if (!EB_VALID_TYPE(type)) {
        return;
    }

    const unsigned int n = bus->num_handlers_by_event[type];
    for (unsigned int i = 0; i < n; ++i) {
        bus->handlers[type][i].handler = NULL;
        bus->handlers[type][i].ctx = NULL;
    }
    bus->num_handlers_by_event[type] = 0;
}

bool es_publish_ev(es_event_bus_t *bus, const es_event_t *event) {
    assert(bus);
    assert(event);

    if (!EB_VALID_TYPE(event->type)) {
        return false;
    }

    const unsigned int n = bus->num_handlers_by_event[event->type];
    if (n == 0) {
        return true;
    }

    es_event_handler_with_ctx_t snap[ES_MAX_HANDLERS_PER_TYPE];
    memcpy(snap, bus->handlers[event->type], n * sizeof(es_event_handler_with_ctx_t));

    for (unsigned int i = 0; i < n; ++i) {
        if (snap[i].handler) {
            snap[i].handler(event, bus, snap[i].ctx);
        }
    }
    return true;
}

bool es_publish(es_event_bus_t *bus, es_event_type_e type) {
    const es_event_t event = {.type = type, .data = NULL, .data_size = 0};
    return es_publish_ev(bus, &event);
}
