#include "event_system.h"

#include <stdio.h>
#include <assert.h>

#define UNUSED(x) (void)(x)

typedef struct {
    int x;
} user_ctx_t;

void handle_event_type_a_and_b(const es_event_t *event, es_event_bus_t *bus, void *ctx) {
    assert(event);
    assert(bus);

    UNUSED(ctx);

    switch (es_get_event_type(event)) {
        case ES_EV_TYPE_A:
            printf("A\n");
            es_publish(bus, ES_EV_TYPE_C);
            break;
        case ES_EV_TYPE_B: {
            ES_EV_EXPECT(event, int);
            const int x = ES_EV_VAL(event, int);
            printf("B %d\n", x);
            break;
        }
        default:
            break;
    }
}

void handle_event_type_c_and_d(const es_event_t *event, es_event_bus_t *bus, void *ctx) {
    assert(event);
    assert(bus);

    switch (es_get_event_type(event)) {
        case ES_EV_TYPE_C: {
            ES_CTX_EXPECT(ctx, user_ctx_t);
            const user_ctx_t user_ctx = ES_CTX_VAL(ctx, user_ctx_t);
            printf("C %d\n", user_ctx.x);
            break;
        }
        case ES_EV_TYPE_D: {
            printf("D\n");
            const int x = 5;
            ES_PUBLISH_LDATA(bus, ES_EV_TYPE_B, x);
            // or
            // ES_PUBLISH_RDATA(bus, EV_TYPE_B, int, 5);
            break;
        }
        default:
            break;
    }
}

void register_events(es_event_bus_t *bus) {
    static user_ctx_t ctx = {.x = 10};
    es_subscribe(bus, ES_EV_TYPE_A, handle_event_type_a_and_b, NULL);
    es_subscribe(bus, ES_EV_TYPE_B, handle_event_type_a_and_b, NULL);
    es_subscribe(bus, ES_EV_TYPE_C, handle_event_type_c_and_d, &ctx);
    es_subscribe(bus, ES_EV_TYPE_D, handle_event_type_c_and_d, NULL);
}

int main(void) {
    es_event_bus_t *bus = es_bus_create();
    assert(bus);

    register_events(bus);

    es_publish(bus, ES_EV_TYPE_A);
    es_publish(bus, ES_EV_TYPE_C);
    es_publish(bus, ES_EV_TYPE_D);

    es_bus_destroy(bus);
}
