#include "es/event_system.h"

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

static void h_a(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    trace_put('a');
}

static void h_b(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    trace_put('b');
}

static void h_c(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    UNUSED(ctx);
    trace_put('c');
}

static int payload_seen;

static void h_payload(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(bus);
    UNUSED(ctx);
    ES_EV_EXPECT(ev, int);
    payload_seen = ES_EV_VAL(ev, int);
}

// removes h_c, which is subscribed after it and has not been reached yet
static void h_kill_c(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(ctx);
    trace_put('k');
    CHECK(es_unsubscribe(bus, EV_X, h_c, nullptr));
}

static void h_relay(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(ctx);
    trace_put('r');
    CHECK(es_publish(bus, EV_Y));
}

/* ========== tests ========== */

static void test_empty_bus() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    CHECK(bus);

    CHECK(es_count_subscribers(bus, EV_X) == 0);
    CHECK(es_publish(bus, EV_X)); // no subscribers is not a failure
    CHECK(!es_publish(bus, EV_COUNT)); // out of range
    CHECK(!es_publish(bus, -1));

    es_bus_destroy(bus);
}

static void test_subscribe_rules() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    int c1 = 0;
    int c2 = 0;

    CHECK(es_subscribe(bus, EV_X, h_a, &c1));
    CHECK(!es_subscribe(bus, EV_X, h_a, &c1)); // exact duplicate
    CHECK(es_subscribe(bus, EV_X, h_a, &c2)); // same handler, other ctx
    CHECK(!es_subscribe(bus, EV_COUNT, h_a, nullptr));
    CHECK(es_count_subscribers(bus, EV_X) == 2);

    es_bus_destroy(bus);
}

static void test_fifo_order() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    trace_reset();

    CHECK(es_subscribe(bus, EV_X, h_a, nullptr));
    CHECK(es_subscribe(bus, EV_X, h_b, nullptr));
    CHECK(es_subscribe(bus, EV_X, h_c, nullptr));
    CHECK(es_publish(bus, EV_X));
    CHECK(strcmp(trace, "abc") == 0);

    es_bus_destroy(bus);
}

static void test_payload() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    payload_seen = 0;

    CHECK(es_subscribe(bus, EV_X, h_payload, nullptr));
    const int x = 42;
    ES_PUBLISH(bus, EV_X, x);
    CHECK(payload_seen == 42);

    es_bus_destroy(bus);
}

static void test_unsubscribe() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    trace_reset();

    CHECK(es_subscribe(bus, EV_X, h_a, nullptr));
    CHECK(es_subscribe(bus, EV_X, h_b, nullptr));

    CHECK(es_unsubscribe(bus, EV_X, h_a, nullptr));
    CHECK(!es_unsubscribe(bus, EV_X, h_a, nullptr)); // already gone
    CHECK(es_count_subscribers(bus, EV_X) == 1);

    CHECK(es_publish(bus, EV_X));
    CHECK(strcmp(trace, "b") == 0);

    es_bus_destroy(bus);
}

static void test_unsubscribe_during_dispatch() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    trace_reset();

    CHECK(es_subscribe(bus, EV_X, h_kill_c, nullptr));
    CHECK(es_subscribe(bus, EV_X, h_c, nullptr));

    CHECK(es_publish(bus, EV_X));
    CHECK(strcmp(trace, "k") == 0); // h_c was skipped, not called
    CHECK(es_count_subscribers(bus, EV_X) == 1); // and swept once dispatch ended

    es_bus_destroy(bus);
}

static void test_unsubscribe_by_ctx() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    int obj = 0;

    CHECK(es_subscribe(bus, EV_X, h_a, &obj));
    CHECK(es_subscribe(bus, EV_Y, h_a, &obj));
    CHECK(es_subscribe(bus, EV_X, h_b, nullptr));

    CHECK(es_unsubscribe_by_ctx(bus, &obj) == 2);
    CHECK(es_count_subscribers(bus, EV_X) == 1);
    CHECK(es_count_subscribers(bus, EV_Y) == 0);

    es_bus_destroy(bus);
}

static void test_nested_publish() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    trace_reset();

    CHECK(es_subscribe(bus, EV_X, h_relay, nullptr));
    CHECK(es_subscribe(bus, EV_Y, h_b, nullptr));

    CHECK(es_publish(bus, EV_X));
    CHECK(strcmp(trace, "rb") == 0);

    es_bus_destroy(bus);
}

static void test_capacity() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    int slots[ES_MAX_HANDLERS_PER_TYPE + 1] = {0};

    for (size_t i = 0; i < ES_MAX_HANDLERS_PER_TYPE; ++i) {
        CHECK(es_subscribe(bus, EV_X, h_a, &slots[i]));
    }
    CHECK(!es_subscribe(bus, EV_X, h_a, &slots[ES_MAX_HANDLERS_PER_TYPE]));
    CHECK(es_count_subscribers(bus, EV_X) == ES_MAX_HANDLERS_PER_TYPE);

    es_bus_destroy(bus);
}

static void test_reset() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    trace_reset();

    CHECK(es_subscribe(bus, EV_X, h_a, nullptr));
    CHECK(es_subscribe(bus, EV_Y, h_b, nullptr));

    es_bus_reset(bus);
    CHECK(es_count_subscribers(bus, EV_X) == 0);
    CHECK(es_count_subscribers(bus, EV_Y) == 0);

    CHECK(es_publish(bus, EV_X));
    CHECK(strcmp(trace, "") == 0);

    es_bus_destroy(bus);
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
