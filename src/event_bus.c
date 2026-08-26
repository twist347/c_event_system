#include "eb/event_bus.h"

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
//     outside a dispatch, so eb_subscribe may simply append;
//   * a dispatch loop fixes its upper bound before calling anything, so a
//     subscription appended during dispatch cannot see the event in flight.

static void mark_dead(eb_EventBus *bus, eb_EventType type, size_t idx);

static void compact_type(eb_EventBus *bus, eb_EventType type);

static void compact_all(eb_EventBus *bus);

/* ========== event ========== */

struct eb_Event {
    eb_EventType type;
    const void *data;
    size_t data_size;
};

eb_EventType eb_ev_type(const eb_Event *ev) {
    assert(ev);

    return ev->type;
}

const void *eb_ev_data(const eb_Event *ev) {
    assert(ev);

    return ev->data;
}

size_t eb_ev_data_size(const eb_Event *ev) {
    assert(ev);

    return ev->data_size;
}

/* ========== event bus ========== */

typedef struct {
    eb_EventHandler handler;
    void *ctx;
} Subscription;

struct eb_EventBus {
    size_t event_type_count;
    size_t *len;
    size_t *dead;
    size_t total_dead;
    Subscription (*subs)[EB_MAX_HANDLERS_PER_TYPE];
    size_t dispatch_depth;
};

eb_EventBus *eb_bus_create(size_t event_type_count) {
    assert(event_type_count > 0);

    eb_EventBus *bus = malloc(sizeof(eb_EventBus));
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

void eb_bus_destroy(eb_EventBus *bus) {
    if (!bus) {
        return;
    }
    free(bus->len);
    free(bus->dead);
    free(bus->subs);
    free(bus);
}

void eb_bus_reset(eb_EventBus *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);

    memset(bus->len, 0, sizeof(*bus->len) * bus->event_type_count);
    memset(bus->dead, 0, sizeof(*bus->dead) * bus->event_type_count);
    memset(bus->subs, 0, sizeof(*bus->subs) * bus->event_type_count);
    bus->total_dead = 0;
}

/* ========== event bus subs ========== */

bool eb_subscribe(eb_EventBus *bus, eb_EventType type, eb_EventHandler handler, void *ctx) {
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
    if (n >= EB_MAX_HANDLERS_PER_TYPE) {
        return false;
    }

    // FIFO
    bus->subs[type][n].handler = handler;
    bus->subs[type][n].ctx = ctx;
    bus->len[type] = n + 1;

    return true;
}

bool eb_unsubscribe(eb_EventBus *bus, eb_EventType type, eb_EventHandler handler, void *ctx) {
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

void eb_unsubscribe_by_type(eb_EventBus *bus, eb_EventType type) {
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

size_t eb_unsubscribe_by_ctx(eb_EventBus *bus, const void *ctx) {
    assert(bus);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        const size_t n = bus->len[t];
        for (size_t i = 0; i < n; ++i) {
            const Subscription *s = &bus->subs[t][i];
            if (s->handler && s->ctx == ctx) {
                mark_dead(bus, (eb_EventType) t, i);
                ++removed;
            }
        }
    }

    if (bus->dispatch_depth == 0) {
        compact_all(bus);
    }
    return removed;
}

size_t eb_unsubscribe_by_handler(eb_EventBus *bus, eb_EventHandler handler) {
    assert(bus);
    assert(handler);

    size_t removed = 0;
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        const size_t n = bus->len[t];
        for (size_t i = 0; i < n; ++i) {
            if (bus->subs[t][i].handler == handler) {
                mark_dead(bus, (eb_EventType) t, i);
                ++removed;
            }
        }
    }

    if (bus->dispatch_depth == 0) {
        compact_all(bus);
    }
    return removed;
}

size_t eb_count_subscribers(const eb_EventBus *bus, eb_EventType type) {
    assert(bus);

    if (!TYPE_IS_VALID(bus, type)) {
        return 0;
    }

    // dead slots sit inside the border, so the difference is exact and O(1)
    return bus->len[type] - bus->dead[type];
}

/* ========== event bus publish ========== */

bool eb_publish_data(eb_EventBus *bus, eb_EventType type, const void *data, size_t data_size) {
    assert(bus);
    assert((data == nullptr) == (data_size == 0));

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    if (bus->dispatch_depth >= EB_MAX_DISPATCH_DEPTH) {
        assert(0 && "eb_publish_data: dispatch depth limit exceeded");
        return false;
    }

    const eb_Event ev = {.type = type, .data = data, .data_size = data_size};

    // fixed before any handler runs: subscriptions appended during dispatch
    // stay out of this event
    const size_t n = bus->len[ev.type];
    assert(n <= EB_MAX_HANDLERS_PER_TYPE);
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

bool eb_publish(eb_EventBus *bus, eb_EventType type) {
    assert(bus);

    return eb_publish_data(bus, type, nullptr, 0);
}

/* ========== internal ========== */

static void mark_dead(eb_EventBus *bus, eb_EventType type, size_t idx) {
    assert(bus->subs[type][idx].handler);

    bus->subs[type][idx].handler = nullptr;
    bus->subs[type][idx].ctx = nullptr;
    ++bus->dead[type];
    ++bus->total_dead;
}

static void compact_type(eb_EventBus *bus, eb_EventType type) {
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

static void compact_all(eb_EventBus *bus) {
    for (size_t t = 0; t < bus->event_type_count && bus->total_dead > 0; ++t) {
        compact_type(bus, (eb_EventType) t);
    }
}
