#include "eb/event_bus.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UNUSED(x) (void) (x)

#define CHECK(cond)                                                  \
do {                                                                 \
    ++checks_run;                                                    \
    if (!(cond)) {                                                   \
        ++checks_failed;                                             \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                \
} while (0)

static int checks_run = 0;
static int checks_failed = 0;

enum : int32_t {
    EV_X = 0,
    EV_Y,
    EV_COUNT
};

/* ========== handlers ========== */

// every handler appends one letter, so a dispatch leaves a readable trace
static char trace[64];
static size_t trace_len;

static void trace_reset() {
    trace_len = 0;
    trace[0] = '\0';
}

static void trace_put(char c) {
    trace[trace_len++] = c;
    trace[trace_len] = '\0';
}

static void h_a(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    trace_put('a');
}

static void h_b(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    trace_put('b');
}

static void h_c(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    trace_put('c');
}

static int payload_seen;

static void h_payload(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(bus);
    UNUSED(ctx);
    EB_EV_EXPECT(ev, int);
    payload_seen = EB_EV_VAL(ev, int);
}

// removes h_c, which is subscribed after it and has not been reached yet
static void h_kill_c(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(ctx);
    trace_put('k');
    CHECK(eb_unsubscribe(bus, EV_X, h_c, nullptr));
}

static void h_relay(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(ctx);
    trace_put('r');
    CHECK(eb_publish(bus, EV_Y));
}

static int calls;

static void h_count(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    ++calls;
}

// subscribes from inside a dispatch; called with the vector exactly full, so the
// append reallocs and moves the array the loop above is still walking
static void h_grow(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(ctx);
    trace_put('g');
    CHECK(eb_subscribe(bus, EV_X, h_count, nullptr));
}

/* ========== tests ========== */

static void test_empty_bus() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    CHECK(bus);

    CHECK(eb_count_subscribers(bus, EV_X) == 0);
    CHECK(eb_publish(bus, EV_X)); // no subscribers is not a failure
    CHECK(!eb_publish(bus, EV_COUNT)); // out of range
    CHECK(!eb_publish(bus, -1));

    eb_bus_destroy(bus);
}

static void test_subscribe_rules() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int c1 = 0;
    int c2 = 0;

    CHECK(eb_subscribe(bus, EV_X, h_a, &c1));
    CHECK(!eb_subscribe(bus, EV_X, h_a, &c1)); // exact duplicate
    CHECK(eb_subscribe(bus, EV_X, h_a, &c2)); // same handler, other ctx
    CHECK(!eb_subscribe(bus, EV_COUNT, h_a, nullptr));
    CHECK(eb_count_subscribers(bus, EV_X) == 2);

    eb_bus_destroy(bus);
}

static void test_fifo_order() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    trace_reset();

    CHECK(eb_subscribe(bus, EV_X, h_a, nullptr));
    CHECK(eb_subscribe(bus, EV_X, h_b, nullptr));
    CHECK(eb_subscribe(bus, EV_X, h_c, nullptr));
    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "abc") == 0);

    eb_bus_destroy(bus);
}

static void test_payload() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    payload_seen = 0;

    CHECK(eb_subscribe(bus, EV_X, h_payload, nullptr));
    const int x = 42;
    EB_PUBLISH(bus, EV_X, x);
    CHECK(payload_seen == 42);

    eb_bus_destroy(bus);
}

static void test_unsubscribe() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    trace_reset();

    CHECK(eb_subscribe(bus, EV_X, h_a, nullptr));
    CHECK(eb_subscribe(bus, EV_X, h_b, nullptr));

    CHECK(eb_unsubscribe(bus, EV_X, h_a, nullptr));
    CHECK(!eb_unsubscribe(bus, EV_X, h_a, nullptr)); // already gone
    CHECK(eb_count_subscribers(bus, EV_X) == 1);

    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "b") == 0);

    eb_bus_destroy(bus);
}

static void test_unsubscribe_during_dispatch() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    trace_reset();

    CHECK(eb_subscribe(bus, EV_X, h_kill_c, nullptr));
    CHECK(eb_subscribe(bus, EV_X, h_c, nullptr));

    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "k") == 0); // h_c was skipped, not called
    CHECK(eb_count_subscribers(bus, EV_X) == 1); // and swept once dispatch ended

    eb_bus_destroy(bus);
}

static void test_unsubscribe_by_ctx() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int obj = 0;

    CHECK(eb_subscribe(bus, EV_X, h_a, &obj));
    CHECK(eb_subscribe(bus, EV_Y, h_a, &obj));
    CHECK(eb_subscribe(bus, EV_X, h_b, nullptr));

    CHECK(eb_unsubscribe_by_ctx(bus, &obj) == 2);
    CHECK(eb_count_subscribers(bus, EV_X) == 1);
    CHECK(eb_count_subscribers(bus, EV_Y) == 0);

    eb_bus_destroy(bus);
}

static void test_nested_publish() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    trace_reset();

    CHECK(eb_subscribe(bus, EV_X, h_relay, nullptr));
    CHECK(eb_subscribe(bus, EV_Y, h_b, nullptr));

    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "rb") == 0);

    eb_bus_destroy(bus);
}

// well past every doubling step, so the vector reallocs several times
static void test_growth() {
    constexpr size_t n = 200;

    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int slots[n];

    bool all_ok = true;
    for (size_t i = 0; i < n; ++i) {
        slots[i] = 0;
        if (!eb_subscribe(bus, EV_X, h_count, &slots[i])) {
            all_ok = false;
        }
    }
    CHECK(all_ok);
    CHECK(eb_count_subscribers(bus, EV_X) == n);
    CHECK(!eb_subscribe(bus, EV_X, h_count, &slots[0])); // duplicate, after growth

    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(calls == n);

    // compaction reclaims every slot but keeps the capacity, so the vector stays
    // usable and the next subscribe needs no realloc
    eb_unsubscribe_by_type(bus, EV_X);
    CHECK(eb_count_subscribers(bus, EV_X) == 0);
    CHECK(eb_subscribe(bus, EV_X, h_count, &slots[0]));

    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(calls == 1);

    eb_bus_destroy(bus);
}

// the realloc hazard: growing from inside a handler must not disturb the
// dispatch already walking the array
static void test_grow_during_dispatch() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int slots[3] = {0};
    trace_reset();

    // fill the first capacity step (4) exactly: h_grow then three more
    CHECK(eb_subscribe(bus, EV_X, h_grow, nullptr));
    CHECK(eb_subscribe(bus, EV_X, h_a, &slots[0]));
    CHECK(eb_subscribe(bus, EV_X, h_a, &slots[1]));
    CHECK(eb_subscribe(bus, EV_X, h_a, &slots[2]));

    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "gaaa") == 0); // the rest of the loop survived the move
    CHECK(calls == 0);                 // the new handler stayed out of this event
    CHECK(eb_count_subscribers(bus, EV_X) == 5);

    // it takes part in the next one
    CHECK(eb_unsubscribe(bus, EV_X, h_grow, nullptr));
    trace_reset();
    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "aaa") == 0);
    CHECK(calls == 1);

    eb_bus_destroy(bus);
}

static void test_reserve() {
    constexpr size_t n = 50;

    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int slots[n];

    CHECK(!eb_bus_reserve(bus, EV_COUNT, n)); // invalid type
    CHECK(!eb_bus_reserve(bus, -1, n));
    CHECK(!eb_bus_reserve(bus, EV_X, SIZE_MAX)); // byte count would overflow

    CHECK(eb_bus_reserve(bus, EV_X, n));
    CHECK(eb_bus_reserve(bus, EV_X, 1));         // below capacity: a no-op, not a failure
    CHECK(eb_count_subscribers(bus, EV_X) == 0); // reserving registers nobody

    bool all_ok = true;
    for (size_t i = 0; i < n; ++i) {
        slots[i] = 0;
        if (!eb_subscribe(bus, EV_X, h_count, &slots[i])) {
            all_ok = false;
        }
    }
    CHECK(all_ok);
    CHECK(eb_count_subscribers(bus, EV_X) == n);

    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(calls == n);

    eb_bus_destroy(bus);
}

static void test_shrink() {
    constexpr size_t n = 100;

    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int slots[n];

    bool all_ok = true;
    for (size_t i = 0; i < n; ++i) {
        slots[i] = 0;
        if (!eb_subscribe(bus, EV_X, h_count, &slots[i])) {
            all_ok = false;
        }
    }
    CHECK(all_ok);

    bool all_gone = true;
    for (size_t i = 3; i < n; ++i) {
        if (!eb_unsubscribe(bus, EV_X, h_count, &slots[i])) {
            all_gone = false;
        }
    }
    CHECK(all_gone);
    CHECK(eb_count_subscribers(bus, EV_X) == 3);

    eb_bus_shrink_to_fit(bus);
    CHECK(eb_count_subscribers(bus, EV_X) == 3); // subscriptions are untouched

    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(calls == 3);

    CHECK(eb_subscribe(bus, EV_X, h_count, &slots[10])); // still growable
    CHECK(eb_count_subscribers(bus, EV_X) == 4);

    // a type with nothing live frees its storage outright, and shrinking an
    // already tight bus twice changes nothing
    eb_unsubscribe_by_type(bus, EV_X);
    eb_bus_shrink_to_fit(bus);
    eb_bus_shrink_to_fit(bus);
    CHECK(eb_count_subscribers(bus, EV_X) == 0);
    CHECK(eb_publish(bus, EV_X)); // publishing on emptied storage is fine

    CHECK(eb_subscribe(bus, EV_X, h_count, &slots[0])); // and it regrows
    CHECK(eb_count_subscribers(bus, EV_X) == 1);
    calls = 0;
    CHECK(eb_publish(bus, EV_X));
    CHECK(calls == 1);

    eb_bus_destroy(bus);
}

static void test_reset() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    trace_reset();

    CHECK(eb_subscribe(bus, EV_X, h_a, nullptr));
    CHECK(eb_subscribe(bus, EV_Y, h_b, nullptr));

    eb_bus_reset(bus);
    CHECK(eb_count_subscribers(bus, EV_X) == 0);
    CHECK(eb_count_subscribers(bus, EV_Y) == 0);

    CHECK(eb_publish(bus, EV_X));
    CHECK(strcmp(trace, "") == 0);

    eb_bus_destroy(bus);
}

int main() {
    test_empty_bus();
    test_subscribe_rules();
    test_fifo_order();
    test_payload();
    test_unsubscribe();
    test_unsubscribe_during_dispatch();
    test_unsubscribe_by_ctx();
    test_nested_publish();
    test_growth();
    test_grow_during_dispatch();
    test_reserve();
    test_shrink();
    test_reset();

    printf("%d checks, %d failed\n", checks_run, checks_failed);
    return checks_failed != 0;
}
