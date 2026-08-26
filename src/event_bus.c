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
//   * len is the used-slot border, NOT the number of live handlers;
//   * dispatch_depth == 0  =>  total_dead == 0, i.e. the array is dense
//     outside a dispatch, so eb_subscribe may simply append;
//   * a dispatch loop fixes its upper bound before calling anything, so a
//     subscription appended during dispatch cannot see the event in flight;
//   * slots in [len, cap) are zeroed and never read.
//
// Growth: eb_subscribe may realloc from inside a handler, so any Subscription *
// taken before a call into user code is dangling afterwards. Index the vector
// instead of holding a pointer across such a call.

static bool grow(eb_EventBus *bus, eb_EventType type);

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

// Growable, never shrinks: compaction reclaims slots but keeps the capacity.
typedef struct {
    Subscription *data;
    size_t len;
    size_t cap;
    size_t dead;
} SubscriptionVec;

struct eb_EventBus {
    size_t event_type_count;
    SubscriptionVec *subs;
    size_t total_dead;
    size_t dispatch_depth;
};

eb_EventBus *eb_bus_create(size_t event_type_count) {
    assert(event_type_count > 0);

    eb_EventBus *bus = malloc(sizeof(eb_EventBus));
    if (!bus) {
        return nullptr;
    }

    // every vector starts empty; storage is allocated on first subscribe
    bus->event_type_count = event_type_count;
    bus->subs = calloc(event_type_count, sizeof(*bus->subs));
    bus->total_dead = 0;
    bus->dispatch_depth = 0;

    if (!bus->subs) {
        free(bus);
        return nullptr;
    }
    return bus;
}

void eb_bus_destroy(eb_EventBus *bus) {
    if (!bus) {
        return;
    }
    for (size_t t = 0; t < bus->event_type_count; ++t) {
        free(bus->subs[t].data);
    }
    free(bus->subs);
    free(bus);
}

void eb_bus_reset(eb_EventBus *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);

    for (size_t t = 0; t < bus->event_type_count; ++t) {
        SubscriptionVec *v = &bus->subs[t];
        if (v->len > 0) {
            memset(v->data, 0, v->len * sizeof(*v->data));
        }
        v->len = 0;
        v->dead = 0;
    }
    bus->total_dead = 0;
}

bool eb_bus_reserve(eb_EventBus *bus, eb_EventType type, size_t n) {
    assert(bus);

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    SubscriptionVec *v = &bus->subs[type];
    if (n <= v->cap) {
        return true;
    }

    if (n > SIZE_MAX / sizeof(*v->data)) {
        return false;
    }

    Subscription *data = realloc(v->data, n * sizeof(*data));
    if (!data) {
        return false;
    }

    memset(&data[v->cap], 0, (n - v->cap) * sizeof(*data));
    v->data = data;
    v->cap = n;

    return true;
}

void eb_bus_shrink_to_fit(eb_EventBus *bus) {
    assert(bus);
    assert(bus->dispatch_depth == 0);
    assert(bus->total_dead == 0); // dense outside a dispatch, so len == live

    for (size_t t = 0; t < bus->event_type_count; ++t) {
        SubscriptionVec *v = &bus->subs[t];
        if (v->len == v->cap) {
            continue;
        }

        if (v->len == 0) {
            free(v->data);
            v->data = nullptr;
            v->cap = 0;
            continue;
        }

        Subscription *data = realloc(v->data, v->len * sizeof(*data));
        if (data) {
            v->data = data;
            v->cap = v->len;
        }
    }
}

/* ========== event bus subs ========== */

bool eb_subscribe(eb_EventBus *bus, eb_EventType type, eb_EventHandler handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    SubscriptionVec *v = &bus->subs[type];

    // forbid duplicates; dead slots hold handler == nullptr and never match
    const size_t n = v->len;
    for (size_t i = 0; i < n; ++i) {
        if (v->data[i].handler == handler && v->data[i].ctx == ctx) {
            return false;
        }
    }

    // append only: reusing a dead slot would expose the new handler to a
    // dispatch already in flight
    if (n == v->cap && !grow(bus, type)) {
        return false;
    }

    // FIFO
    v->data[n].handler = handler;
    v->data[n].ctx = ctx;
    v->len = n + 1;

    return true;
}

bool eb_unsubscribe(eb_EventBus *bus, eb_EventType type, eb_EventHandler handler, void *ctx) {
    assert(bus);
    assert(handler);

    if (!TYPE_IS_VALID(bus, type)) {
        return false;
    }

    SubscriptionVec *v = &bus->subs[type];
    const size_t n = v->len;
    for (size_t i = 0; i < n; ++i) {
        if (v->data[i].handler == handler && v->data[i].ctx == ctx) {
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

    SubscriptionVec *v = &bus->subs[type];
    const size_t n = v->len;
    for (size_t i = 0; i < n; ++i) {
        if (v->data[i].handler) {
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
        SubscriptionVec *v = &bus->subs[t];
        const size_t n = v->len;
        for (size_t i = 0; i < n; ++i) {
            if (v->data[i].handler && v->data[i].ctx == ctx) {
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
        SubscriptionVec *v = &bus->subs[t];
        const size_t n = v->len;
        for (size_t i = 0; i < n; ++i) {
            if (v->data[i].handler == handler) {
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
    return bus->subs[type].len - bus->subs[type].dead;
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

    SubscriptionVec *v = &bus->subs[ev.type];

    // fixed before any handler runs: subscriptions appended during dispatch
    // stay out of this event
    const size_t n = v->len;
    if (n == 0) {
        return true;
    }

    ++bus->dispatch_depth;
    for (size_t i = 0; i < n; ++i) {
        // re-read the slot every iteration and copy it out: a handler may kill
        // itself or a peer, and a subscribe from inside one may have moved the
        // base pointer. The vector header itself never moves.
        const Subscription s = v->data[i];
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

static bool grow(eb_EventBus *bus, eb_EventType type) {
    SubscriptionVec *v = &bus->subs[type];
    assert(v->len == v->cap);

    const size_t old_cap = v->cap;
    const size_t cap = old_cap ? old_cap * 2 : 4;

    Subscription *data = realloc(v->data, cap * sizeof(*data));
    if (!data) {
        return false;
    }

    memset(&data[old_cap], 0, (cap - old_cap) * sizeof(*data));
    v->data = data;
    v->cap = cap;

    return true;
}

static void mark_dead(eb_EventBus *bus, eb_EventType type, size_t idx) {
    SubscriptionVec *v = &bus->subs[type];
    assert(v->data[idx].handler);

    v->data[idx].handler = nullptr;
    v->data[idx].ctx = nullptr;
    ++v->dead;
    ++bus->total_dead;
}

static void compact_type(eb_EventBus *bus, eb_EventType type) {
    assert(bus->dispatch_depth == 0);

    SubscriptionVec *v = &bus->subs[type];

    const size_t dead = v->dead;
    if (dead == 0) {
        return;
    }

    Subscription *arr = v->data;
    const size_t n = v->len;

    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        if (arr[r].handler) {
            arr[w++] = arr[r];
        }
    }
    assert(w + dead == n);

    // capacity is kept: the vector never shrinks
    memset(&arr[w], 0, dead * sizeof(arr[0]));
    v->len = w;
    v->dead = 0;
    bus->total_dead -= dead;
}

static void compact_all(eb_EventBus *bus) {
    for (size_t t = 0; t < bus->event_type_count && bus->total_dead > 0; ++t) {
        compact_type(bus, (eb_EventType) t);
    }
}
