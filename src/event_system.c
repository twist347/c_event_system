#include "es/event_system.h"

#include <assert.h>
#include <stdlib.h>

#define ES_VALID_TYPE(bus, t) \
    ((t) >= 0 && (size_t) (t) < (bus)->event_type_count)

// Removal model: tombstones.
//
// A removed subscription is marked dead (handler == nullptr) instead of being
// erased, so indices stay stable while a dispatch loop walks the array. Dead
// slots are swept once the outermost dispatch returns.
//
// Invariants:
//   * counts[t] is the used-slot border, NOT the number of live handlers;
//   * dispatch_depth == 0  =>  total_holes == 0, i.e. the array is dense
//     outside a dispatch, so es_subscribe may simply append;
//   * a dispatch loop fixes its upper bound before calling anything, so a
//     subscription appended during dispatch cannot see the event in flight.

static void mark_dead(es_EventBus *bus, es_EventType type, size_t idx);
static void compact_type(es_EventBus *bus, es_EventType type);
static void compact_all(es_EventBus *bus);

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
    size_t *holes;
    size_t total_holes;
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
    bus->holes = calloc(event_type_count, sizeof(*bus->holes));
    bus->handlers = calloc(event_type_count, sizeof(*bus->handlers));
    bus->total_holes = 0;
    bus->dispatch_depth = 0;

    if (!bus->counts || !bus->holes || !bus->handlers) {
        free(bus->counts);
        free(bus->holes);
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
    free(bus->holes);
    free(bus->handlers);
    free(bus);
}

void es_bus_reset(es_EventBus *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);

    memset(bus->counts, 0, sizeof(*bus->counts) * bus->event_type_count);
    memset(bus->holes, 0, sizeof(*bus->holes) * bus->event_type_count);
    memset(bus->handlers, 0, sizeof(*bus->handlers) * bus->event_type_count);
    bus->total_holes = 0;
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

    const size_t n = bus->counts[type];

    // forbid duplicates; dead slots hold handler == nullptr and never match
    for (size_t i = 0; i < n; ++i) {
        if (bus->handlers[type][i].handler == handler && bus->handlers[type][i].ctx == ctx) {
            return false;
        }
    }

    // append only: reusing a dead slot would expose the new handler to a
    // dispatch already in flight
    if (n >= ES_MAX_HANDLERS_PER_TYPE) {
        return false;
    }

    // FIFO
    bus->handlers[type][n].handler = handler;
    bus->handlers[type][n].ctx = ctx;
    bus->counts[type] = n + 1;

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
            mark_dead(bus, type, i);
            if (bus->dispatch_depth == 0) {
                compact_type(bus, type);
            }
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

    const size_t n = bus->counts[type];
    for (size_t i = 0; i < n; ++i) {
        if (bus->handlers[type][i].handler) {
            mark_dead(bus, type, i);
        }
    }

    if (bus->dispatch_depth == 0) {
        compact_type(bus, type);
    }
}

size_t es_unsubscribe_by_ctx(es_EventBus *bus, const void *ctx) {
    assert(bus);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        const size_t n = bus->counts[t];
        for (size_t i = 0; i < n; ++i) {
            const es_Subscription *s = &bus->handlers[t][i];
            if (s->handler && s->ctx == ctx) {
                mark_dead(bus, (es_EventType) t, i);
                ++removed;
            }
        }
    }

    if (bus->dispatch_depth == 0) {
        compact_all(bus);
    }
    return removed;
}

size_t es_unsubscribe_by_handler(es_EventBus *bus, es_EventHandler handler) {
    assert(bus);
    assert(handler);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        const size_t n = bus->counts[t];
        for (size_t i = 0; i < n; ++i) {
            if (bus->handlers[t][i].handler == handler) {
                mark_dead(bus, (es_EventType) t, i);
                ++removed;
            }
        }
    }

    if (bus->dispatch_depth == 0) {
        compact_all(bus);
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

    // fixed before any handler runs: subscriptions appended during dispatch
    // stay out of this event
    const size_t n = bus->counts[ev.type];
    assert(n <= ES_MAX_HANDLERS_PER_TYPE);
    if (n == 0) {
        return true;
    }

    ++bus->dispatch_depth;
    for (size_t i = 0; i < n; ++i) {
        // re-read the slot every iteration and copy it out: a handler may
        // tombstone itself or a peer, and the base pointer may move once the
        // array becomes growable
        const es_Subscription s = bus->handlers[ev.type][i];
        if (!s.handler) {
            continue;
        }
        s.handler(&ev, bus, s.ctx);
    }
    --bus->dispatch_depth;

    if (bus->dispatch_depth == 0 && bus->total_holes > 0) {
        compact_all(bus);
    }

    return true;
}

bool es_publish(es_EventBus *bus, es_EventType type) {
    assert(bus);

    return es_publish_data(bus, type, nullptr, 0);
}

static void mark_dead(es_EventBus *bus, es_EventType type, size_t idx) {
    assert(bus->handlers[type][idx].handler);

    bus->handlers[type][idx].handler = nullptr;
    bus->handlers[type][idx].ctx = nullptr;
    ++bus->holes[type];
    ++bus->total_holes;
}

static void compact_type(es_EventBus *bus, es_EventType type) {
    assert(bus->dispatch_depth == 0);

    const size_t holes = bus->holes[type];
    if (holes == 0) {
        return;
    }

    es_Subscription *arr = bus->handlers[type];
    const size_t n = bus->counts[type];

    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        if (arr[r].handler) {
            arr[w++] = arr[r];
        }
    }
    assert(w + holes == n);

    memset(&arr[w], 0, holes * sizeof(arr[0]));
    bus->counts[type] = w;
    bus->holes[type] = 0;
    bus->total_holes -= holes;
}

static void compact_all(es_EventBus *bus) {
    for (size_t t = 0; t < bus->event_type_count && bus->total_holes > 0; ++t) {
        compact_type(bus, (es_EventType) t);
    }
}
