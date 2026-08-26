#include "es/event_system.h"

#include <assert.h>
#include <stdlib.h>

#define ES_VALID_TYPE(bus, t) \
    ((t) >= 0 && (size_t) (t) < (bus)->event_type_count)

static void compact_remove(es_EventBus *bus, es_EventType type, size_t idx);

struct es_Event {
    es_EventType type;
    const void *data;
    size_t data_size;
};

typedef struct {
    es_EventHandler handler;
    void *ctx;
} es_Subscription;

struct es_EventBus {
    size_t event_type_count;
    size_t *counts;
    es_Subscription (*handlers)[ES_MAX_HANDLERS_PER_TYPE];
    size_t dispatch_depth;
};

es_EventBus *es_bus_create(size_t event_type_count) {
    assert(event_type_count > 0);

    es_EventBus *bus = malloc(sizeof(es_EventBus));
    if (!bus) {
        return nullptr;
    }

    bus->event_type_count = event_type_count;
    bus->counts = calloc(event_type_count, sizeof(*bus->counts));
    bus->handlers = calloc(event_type_count, sizeof(*bus->handlers));
    bus->dispatch_depth = 0;

    if (!bus->counts || !bus->handlers) {
        free(bus->counts);
        free(bus->handlers);
        free(bus);
        return nullptr;
    }
    return bus;
}

void es_bus_destroy(es_EventBus *bus) {
    if (!bus) {
        return;
    }
    free(bus->counts);
    free(bus->handlers);
    free(bus);
}

void es_bus_reset(es_EventBus *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);

    memset(bus->counts, 0, sizeof(*bus->counts) * bus->event_type_count);
    memset(bus->handlers, 0, sizeof(*bus->handlers) * bus->event_type_count);
}

es_EventType es_ev_get_type(const es_Event *ev) {
    assert(ev);

    return ev->type;
}

const void *es_ev_get_data(const es_Event *ev) {
    assert(ev);

    return ev->data;
}

size_t es_ev_get_data_size(const es_Event *ev) {
    assert(ev);

    return ev->data_size;
}

bool es_subscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx) {
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

bool es_unsubscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx) {
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

void es_unsubscribe_by_type(es_EventBus *bus, es_EventType type) {
    assert(bus);

    if (!ES_VALID_TYPE(bus, type)) {
        return;
    }

    bus->counts[type] = 0;
}

size_t es_unsubscribe_by_ctx(es_EventBus *bus, const void *ctx) {
    assert(bus);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        for (size_t i = 0; i < bus->counts[t];) {
            if (bus->handlers[t][i].ctx == ctx) {
                compact_remove(bus, (es_EventType) t, i);
                ++removed;
            } else {
                ++i;
            }
        }
    }
    return removed;
}

size_t es_unsubscribe_by_handler(es_EventBus *bus, es_EventHandler handler) {
    assert(bus);
    assert(handler);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        for (size_t i = 0; i < bus->counts[t];) {
            if (bus->handlers[t][i].handler == handler) {
                compact_remove(bus, (es_EventType) t, i);
                ++removed;
            } else {
                ++i;
            }
        }
    }
    return removed;
}

bool es_publish_data(es_EventBus *bus, es_EventType type, const void *data, size_t data_size) {
    assert(bus);
    assert((data == nullptr) == (data_size == 0));

    if (!ES_VALID_TYPE(bus, type)) {
        return false;
    }

    if (bus->dispatch_depth >= ES_MAX_DISPATCH_DEPTH) {
        assert(0 && "es_publish_data: dispatch depth limit exceeded");
        return false;
    }

    const es_Event ev = {.type = type, .data = data, .data_size = data_size};

    const size_t n = bus->counts[ev.type];
    assert(n <= ES_MAX_HANDLERS_PER_TYPE);
    if (n == 0) {
        return true;
    }

    es_Subscription snap[ES_MAX_HANDLERS_PER_TYPE];
    memcpy(snap, bus->handlers[ev.type], n * sizeof(es_Subscription));

    ++bus->dispatch_depth;
    for (size_t i = 0; i < n; ++i) {
        snap[i].handler(&ev, bus, snap[i].ctx);
    }
    --bus->dispatch_depth;

    return true;
}

bool es_publish(es_EventBus *bus, es_EventType type) {
    assert(bus);

    return es_publish_data(bus, type, nullptr, 0);
}

static void compact_remove(es_EventBus *bus, es_EventType type, size_t idx) {
    es_Subscription *arr = bus->handlers[type];
    const size_t n = bus->counts[type];
    memmove(&arr[idx], &arr[idx + 1], (n - idx - 1) * sizeof(arr[0]));
    bus->counts[type] = n - 1;
}
