#include "es/event_system.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ========== internal ========== */

#define TYPE_IS_VALID(bus, t) \
    ((t) >= 0 && (size_t) (t) < (bus)->event_type_count)

// Removal model: tombstones.
//
// A removed subscription is marked dead (handler == nullptr) instead of being
// erased, so indices stay stable while a dispatch loop walks the array. Dead
// slots are swept once the outermost dispatch returns.
//
// Invariants:
//   * len[t] is the used-slot border, NOT the number of live handlers;
//   * dispatch_depth == 0  =>  total_dead == 0, i.e. the array is dense
//     outside a dispatch, so es_subscribe may simply append;
//   * a dispatch loop fixes its upper bound before calling anything, so a
//     subscription appended during dispatch cannot see the event in flight.

static void mark_dead(es_EventBus *bus, es_EventType type, size_t idx);

static void compact_type(es_EventBus *bus, es_EventType type);

static void compact_all(es_EventBus *bus);

/* ========== event ========== */

struct es_Event {
    es_EventType type;
    const void *data;
    size_t data_size;
};

es_EventType es_ev_type(const es_Event *ev) {
    assert(ev);

    return ev->type;
}

const void *es_ev_data(const es_Event *ev) {
    assert(ev);

    return ev->data;
}

size_t es_ev_data_size(const es_Event *ev) {
    assert(ev);

    return ev->data_size;
}

/* ========== event bus ========== */

typedef struct {
    es_EventHandler handler;
    void *ctx;
} Subscription;

struct es_EventBus {
    size_t event_type_count;
    size_t *len;
    size_t *dead;
    size_t total_dead;
    Subscription (*subs)[ES_MAX_HANDLERS_PER_TYPE];
    size_t dispatch_depth;
};

es_EventBus *es_bus_create(size_t event_type_count) {
    assert(event_type_count > 0);

    es_EventBus *bus = malloc(sizeof(es_EventBus));
    if (!bus) {
        return nullptr;
    }

    bus->event_type_count = event_type_count;
    bus->len = calloc(event_type_count, sizeof(*bus->len));
    bus->dead = calloc(event_type_count, sizeof(*bus->dead));
    bus->subs = calloc(event_type_count, sizeof(*bus->subs));
    bus->total_dead = 0;
    bus->dispatch_depth = 0;

    if (!bus->len || !bus->dead || !bus->subs) {
        free(bus->len);
        free(bus->dead);
        free(bus->subs);
        free(bus);
        return nullptr;
    }
    return bus;
}

void es_bus_destroy(es_EventBus *bus) {
    if (!bus) {
        return;
    }
    free(bus->len);
    free(bus->dead);
    free(bus->subs);
    free(bus);
}

void es_bus_reset(es_EventBus *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);

    memset(bus->len, 0, sizeof(*bus->len) * bus->event_type_count);
    memset(bus->dead, 0, sizeof(*bus->dead) * bus->event_type_count);
    memset(bus->subs, 0, sizeof(*bus->subs) * bus->event_type_count);
    bus->total_dead = 0;
}

/* ========== event bus subs ========== */

bool es_subscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    const size_t n = bus->len[type];

    // forbid duplicates; dead slots hold handler == nullptr and never match
    for (size_t i = 0; i < n; ++i) {
        if (bus->subs[type][i].handler == handler && bus->subs[type][i].ctx == ctx) {
            return false;
        }
    }

    // append only: reusing a dead slot would expose the new handler to a
    // dispatch already in flight
    if (n >= ES_MAX_HANDLERS_PER_TYPE) {
        return false;
    }

    // FIFO
    bus->subs[type][n].handler = handler;
    bus->subs[type][n].ctx = ctx;
    bus->len[type] = n + 1;

    return true;
}

bool es_unsubscribe(es_EventBus *bus, es_EventType type, es_EventHandler handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    const size_t n = bus->len[type];
    for (size_t i = 0; i < n; ++i) {
        if (bus->subs[type][i].handler == handler && bus->subs[type][i].ctx == ctx) {
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

    if (!TYPE_IS_VALID(bus, type)) {
        return;
    }

    const size_t n = bus->len[type];
    for (size_t i = 0; i < n; ++i) {
        if (bus->subs[type][i].handler) {
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
        const size_t n = bus->len[t];
        for (size_t i = 0; i < n; ++i) {
            const Subscription *s = &bus->subs[t][i];
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
        const size_t n = bus->len[t];
        for (size_t i = 0; i < n; ++i) {
            if (bus->subs[t][i].handler == handler) {
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

/* ========== event bus publish ========== */

bool es_publish_data(es_EventBus *bus, es_EventType type, const void *data, size_t data_size) {
    assert(bus);
    assert((data == nullptr) == (data_size == 0));

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    if (bus->dispatch_depth >= ES_MAX_DISPATCH_DEPTH) {
        assert(0 && "es_publish_data: dispatch depth limit exceeded");
        return false;
    }

    const es_Event ev = {.type = type, .data = data, .data_size = data_size};

    // fixed before any handler runs: subscriptions appended during dispatch
    // stay out of this event
    const size_t n = bus->len[ev.type];
    assert(n <= ES_MAX_HANDLERS_PER_TYPE);
    if (n == 0) {
        return true;
    }

    ++bus->dispatch_depth;
    for (size_t i = 0; i < n; ++i) {
        // re-read the slot every iteration and copy it out: a handler may
        // kill itself or a peer, and the base pointer may move once the
        // array becomes growable
        const Subscription s = bus->subs[ev.type][i];
        if (!s.handler) {
            continue;
        }
        s.handler(&ev, bus, s.ctx);
    }
    --bus->dispatch_depth;

    if (bus->dispatch_depth == 0 && bus->total_dead > 0) {
        compact_all(bus);
    }

    return true;
}

bool es_publish(es_EventBus *bus, es_EventType type) {
    assert(bus);

    return es_publish_data(bus, type, nullptr, 0);
}

/* ========== internal ========== */

static void mark_dead(es_EventBus *bus, es_EventType type, size_t idx) {
    assert(bus->subs[type][idx].handler);

    bus->subs[type][idx].handler = nullptr;
    bus->subs[type][idx].ctx = nullptr;
    ++bus->dead[type];
    ++bus->total_dead;
}

static void compact_type(es_EventBus *bus, es_EventType type) {
    assert(bus->dispatch_depth == 0);

    const size_t dead = bus->dead[type];
    if (dead == 0) {
        return;
    }

    Subscription *arr = bus->subs[type];
    const size_t n = bus->len[type];

    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        if (arr[r].handler) {
            arr[w++] = arr[r];
        }
    }
    assert(w + dead == n);

    memset(&arr[w], 0, dead * sizeof(arr[0]));
    bus->len[type] = w;
    bus->dead[type] = 0;
    bus->total_dead -= dead;
}

static void compact_all(es_EventBus *bus) {
    for (size_t t = 0; t < bus->event_type_count && bus->total_dead > 0; ++t) {
        compact_type(bus, (es_EventType) t);
    }
}
