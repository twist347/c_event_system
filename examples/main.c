#include "event_system.h"

#include <stdio.h>
#include <assert.h>

#define UNUSED(x) (void) (x)

typedef enum {
    MY_EV_TYPE_A = 0,
    MY_EV_TYPE_B,
    MY_EV_TYPE_C,
    MY_EV_TYPE_D,
    MY_EV_TYPE__COUNT
} my_event_type_e;

typedef struct {
    int x;
} user_ctx_t;

ES_HANDLER(handle_event_type_a_and_b) {
    assert(event);
    assert(bus);

    UNUSED(ctx);

    switch (es_event_get_type(event)) {
        case MY_EV_TYPE_A:
            printf("A\n");
            es_publish(bus, MY_EV_TYPE_C);
            break;
        case MY_EV_TYPE_B: {
            ES_EV_EXPECT(event, int);
            const int x = ES_EV_VAL(event, int);
            printf("B %d\n", x);
            break;
        }
        default:
            break;
    }
}

ES_HANDLER(handle_event_type_c_and_d) {
    assert(event);
    assert(bus);

    switch (es_event_get_type(event)) {
        case MY_EV_TYPE_C: {
            ES_CTX_EXPECT(ctx, user_ctx_t);
            const user_ctx_t user_ctx = ES_CTX_VAL(ctx, user_ctx_t);
            printf("C %d\n", user_ctx.x);
            break;
        }
        case MY_EV_TYPE_D: {
            printf("D\n");
            const int x = 5;
            ES_PUBLISH(bus, MY_EV_TYPE_B, int, x);
            break;
        }
        default:
            break;
    }
}

void register_events(es_event_bus_t *bus) {
    static user_ctx_t ctx = {.x = 10};
    es_subscribe(bus, MY_EV_TYPE_A, handle_event_type_a_and_b, NULL);
    es_subscribe(bus, MY_EV_TYPE_B, handle_event_type_a_and_b, NULL);
    es_subscribe(bus, MY_EV_TYPE_C, handle_event_type_c_and_d, &ctx);
    es_subscribe(bus, MY_EV_TYPE_D, handle_event_type_c_and_d, NULL);
}

int main(void) {
    es_event_bus_t *bus = es_bus_create(MY_EV_TYPE__COUNT);
    assert(bus);

    register_events(bus);

    es_publish(bus, MY_EV_TYPE_A);
    es_publish(bus, MY_EV_TYPE_C);
    es_publish(bus, MY_EV_TYPE_D);

    es_bus_destroy(bus);
}
