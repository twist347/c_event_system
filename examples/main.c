#include "es/event_system.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#define UNUSED(x) (void) (x)

typedef enum : int32_t {
    MY_EV_TYPE_A = 0,
    MY_EV_TYPE_B,
    MY_EV_TYPE_C,
    MY_EV_TYPE_D,
    MY_EV_TYPE_COUNT
} MyEventType;

typedef struct {
    int x;
} MyCtx;

static void handle_event_type_a_and_b(const es_Event *ev, es_EventBus *bus, void *ctx) {
    assert(ev);
    assert(bus);

    UNUSED(ctx);

    switch (es_ev_type(ev)) {
        case MY_EV_TYPE_A:
            printf("A\n");
            [[maybe_unused]] bool ok = es_publish(bus, MY_EV_TYPE_C);
            assert(ok);
            break;
        case MY_EV_TYPE_B: {
            ES_EV_EXPECT(ev, int);
            const int x = ES_EV_VAL(ev, int);
            printf("B %d\n", x);
            break;
        }
        default:
            break;
    }
}

static void handle_event_type_c_and_d(const es_Event *ev, es_EventBus *bus, void *ctx) {
    assert(ev);
    assert(bus);

    switch (es_ev_type(ev)) {
        case MY_EV_TYPE_C: {
            ES_CTX_EXPECT(ctx, MyCtx);
            const MyCtx user_ctx = ES_CTX_VAL(ctx, MyCtx);
            printf("C %d\n", user_ctx.x);
            break;
        }
        case MY_EV_TYPE_D: {
            printf("D\n");
            const int x = 5;
            ES_PUBLISH(bus, MY_EV_TYPE_B, x);
            break;
        }
        default:
            break;
    }
}

static void register_events(es_EventBus *bus) {
    static MyCtx ctx = {.x = 10};

    [[maybe_unused]] bool ok = es_subscribe(bus, MY_EV_TYPE_A, handle_event_type_a_and_b, nullptr);
    assert(ok);

    ok = es_subscribe(bus, MY_EV_TYPE_B, handle_event_type_a_and_b, nullptr);
    assert(ok);

    ok = es_subscribe(bus, MY_EV_TYPE_C, handle_event_type_c_and_d, &ctx);
    assert(ok);

    ok = es_subscribe(bus, MY_EV_TYPE_D, handle_event_type_c_and_d, nullptr);
    assert(ok);
}

int main() {
    es_EventBus *bus = es_bus_create(MY_EV_TYPE_COUNT);
    assert(bus);

    register_events(bus);

    [[maybe_unused]] bool ok = es_publish(bus, MY_EV_TYPE_A);
    assert(ok);

    ok = es_publish(bus, MY_EV_TYPE_C);
    assert(ok);

    ok = es_publish(bus, MY_EV_TYPE_D);
    assert(ok);

    es_bus_destroy(bus);
}
