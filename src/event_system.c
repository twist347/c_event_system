#include "event_system.h"

#include <stdlib.h>

#define ES_VALID_TYPE(bus, t) \
    ((t) >= 0 && (size_t) (t) < (bus)->event_type_count)

static void compact_remove(es_event_bus_t *bus, es_event_type_t t, size_t idx);

struct es_event_t {
    es_event_type_t type;
    const void *data;
    size_t data_size;
};

typedef struct {
    es_event_handler_fn handler;
    void *ctx;
} es_subscription_t;

struct es_event_bus_t {
    size_t event_type_count;
    size_t *counts;
    es_subscription_t (*handlers)[ES_MAX_HANDLERS_PER_TYPE];
    size_t dispatch_depth;
};

es_event_bus_t *es_bus_create(size_t event_type_count) {
    assert(event_type_count > 0);

    es_event_bus_t *bus = malloc(sizeof(es_event_bus_t));
    if (!bus) {
        return NULL;
    }

    bus->event_type_count = event_type_count;
    bus->counts = calloc(event_type_count, sizeof(*bus->counts));
    bus->handlers = calloc(event_type_count, sizeof(*bus->handlers));
    bus->dispatch_depth = 0;

    if (!bus->counts || !bus->handlers) {
        free(bus->counts);
        free(bus->handlers);
        free(bus);
        return NULL;
    }
    return bus;
}

void es_bus_destroy(es_event_bus_t *bus) {
    if (!bus) {
        return;
    }
    free(bus->counts);
    free(bus->handlers);
    free(bus);
}

void es_bus_reset(es_event_bus_t *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);
    memset(bus->counts, 0, sizeof(*bus->counts) * bus->event_type_count);
    memset(bus->handlers, 0, sizeof(*bus->handlers) * bus->event_type_count);
}

es_event_type_t es_event_get_type(const es_event_t *event) {
    assert(event);

    return event->type;
}

const void *es_event_get_data(const es_event_t *event) {
    assert(event);

    return event->data;
}

size_t es_event_get_data_size(const es_event_t *event) {
    assert(event);

    return event->data_size;
}

bool es_subscribe(es_event_bus_t *bus, es_event_type_t type, es_event_handler_fn handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!ES_VALID_TYPE(bus, type)) {
        return false;
    }

    // forbid duplicates
    for (size_t i = 0; i < bus->counts[type]; ++i) {
        if (bus->handlers[type][i].handler == handler && bus->handlers[type][i].ctx == ctx) {
            return false;
        }
    }

    if (bus->counts[type] >= ES_MAX_HANDLERS_PER_TYPE) {
        return false;
    }

    // FIFO
    bus->handlers[type][bus->counts[type]].handler = handler;
    bus->handlers[type][bus->counts[type]].ctx = ctx;
    ++bus->counts[type];

    return true;
}

bool es_unsubscribe(es_event_bus_t *bus, es_event_type_t type, es_event_handler_fn handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!ES_VALID_TYPE(bus, type)) {
        return false;
    }

    const size_t n = bus->counts[type];
    for (size_t i = 0; i < n; ++i) {
        if (bus->handlers[type][i].handler == handler && bus->handlers[type][i].ctx == ctx) {
            compact_remove(bus, type, i);
            return true;
        }
    }

    return false;
}

void es_unsubscribe_by_type(es_event_bus_t *bus, es_event_type_t type) {
    assert(bus);

    if (!ES_VALID_TYPE(bus, type)) {
        return;
    }

    bus->counts[type] = 0;
}

size_t es_unsubscribe_by_ctx(es_event_bus_t *bus, void *ctx) {
    assert(bus);
    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        for (size_t i = 0; i < bus->counts[t];) {
            if (bus->handlers[t][i].ctx == ctx) {
                compact_remove(bus, (es_event_type_t) t, i);
                ++removed;
            } else {
                ++i;
            }
        }
    }
    return removed;
}

size_t es_unsubscribe_by_handler(es_event_bus_t *bus, es_event_handler_fn handler) {
    assert(bus);
    assert(handler);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        for (size_t i = 0; i < bus->counts[t];) {
            if (bus->handlers[t][i].handler == handler) {
                compact_remove(bus, (es_event_type_t) t, i);
                ++removed;
            } else {
                ++i;
            }
        }
    }
    return removed;
}

bool es_publish_data(es_event_bus_t *bus, es_event_type_t type, const void *data, size_t data_size) {
    assert(bus);
    assert((data == NULL) == (data_size == 0));

    if (!ES_VALID_TYPE(bus, type)) {
        return false;
    }

    if (bus->dispatch_depth >= ES_MAX_DISPATCH_DEPTH) {
        assert(0 && "es_publish_data: dispatch depth limit exceeded");
        return false;
    }

    const es_event_t event = {.type = type, .data = data, .data_size = data_size};

    const size_t n = bus->counts[event.type];
    assert(n <= ES_MAX_HANDLERS_PER_TYPE);
    if (n == 0) {
        return true;
    }

    es_subscription_t snap[ES_MAX_HANDLERS_PER_TYPE];
    memcpy(snap, bus->handlers[event.type], n * sizeof(es_subscription_t));

    ++bus->dispatch_depth;
    for (size_t i = 0; i < n; ++i) {
        if (snap[i].handler) {
            snap[i].handler(&event, bus, snap[i].ctx);
        }
    }
    --bus->dispatch_depth;

    return true;
}

bool es_publish(es_event_bus_t *bus, es_event_type_t type) {
    return es_publish_data(bus, type, NULL, 0);
}

static void compact_remove(es_event_bus_t *bus, es_event_type_t t, size_t idx) {
    es_subscription_t *arr = bus->handlers[t];
    const size_t n = bus->counts[t];
    memmove(&arr[idx], &arr[idx + 1], (n - idx - 1) * sizeof(arr[0]));
    bus->counts[t] = n - 1;
}
