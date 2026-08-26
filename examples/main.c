#include "es/event_system.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(x) (void) (x)

typedef enum : int32_t {
    EV_DAMAGE = 0,
    EV_DIED,
    EV_COUNT
} GameEvent;

typedef struct {
    const char *name;
    int hp;
} Enemy;

typedef struct {
    int amount;
} Damage;

static void on_damage_apply(const es_Event *ev, es_EventBus *bus, void *ctx) {
    ES_EV_EXPECT(ev, Damage);
    ES_CTX_EXPECT(ctx, Enemy);

    Enemy *self = ES_CTX_PTR(ctx, Enemy);
    const Damage dmg = ES_EV_VAL(ev, Damage);

    self->hp -= dmg.amount;
    printf("%s takes %d damage\n", self->name, dmg.amount);

    if (self->hp <= 0) {
        [[maybe_unused]] const bool ok = es_publish(bus, EV_DIED);
        assert(ok);
    }
}

static void on_damage_report(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    ES_CTX_EXPECT(ctx, Enemy);

    const Enemy *self = ES_CTX_CPTR(ctx, Enemy);
    printf("  %s is at %d hp\n", self->name, self->hp);
}

static void on_died(const es_Event *ev, es_EventBus *bus, void *ctx) {
    UNUSED(ev);
    ES_CTX_EXPECT(ctx, Enemy);

    Enemy *self = ES_CTX_PTR(ctx, Enemy);
    printf("  %s died\n", self->name);

    const size_t dropped = es_unsubscribe_by_ctx(bus, self);
    printf("  dropped %zu subscriptions\n", dropped);

    free(self);
}

int main() {
    es_EventBus *bus = es_bus_create(EV_COUNT);
    assert(bus);

    Enemy *goblin = malloc(sizeof(Enemy));
    assert(goblin);
    *goblin = (Enemy){.name = "goblin", .hp = 5};

    [[maybe_unused]] bool ok = es_subscribe(bus, EV_DAMAGE, on_damage_apply, goblin);
    assert(ok);

    ok = es_subscribe(bus, EV_DAMAGE, on_damage_report, goblin);
    assert(ok);

    ok = es_subscribe(bus, EV_DIED, on_died, goblin);
    assert(ok);

    Damage light = {.amount = 2};
    ES_PUBLISH(bus, EV_DAMAGE, light);

    Damage fatal = {.amount = 3};
    ES_PUBLISH(bus, EV_DAMAGE, fatal); // on_damage_report never runs

    ES_PUBLISH(bus, EV_DAMAGE, light); // nobody is listening any more

    es_bus_destroy(bus);
}
