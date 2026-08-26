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

static void test_capacity() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    int slots[EB_MAX_HANDLERS_PER_TYPE + 1] = {0};

    for (size_t i = 0; i < EB_MAX_HANDLERS_PER_TYPE; ++i) {
        CHECK(eb_subscribe(bus, EV_X, h_a, &slots[i]));
    }
    CHECK(!eb_subscribe(bus, EV_X, h_a, &slots[EB_MAX_HANDLERS_PER_TYPE]));
    CHECK(eb_count_subscribers(bus, EV_X) == EB_MAX_HANDLERS_PER_TYPE);

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
    test_capacity();
    test_reset();

    printf("%d checks, %d failed\n", checks_run, checks_failed);
    return checks_failed != 0;
}
