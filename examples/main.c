#include "eb/event_bus.h"

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

static void on_damage_apply(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    EB_EV_EXPECT(ev, Damage);
    EB_CTX_EXPECT(ctx, Enemy);

    Enemy *self = EB_CTX_PTR(ctx, Enemy);
    const Damage dmg = EB_EV_VAL(ev, Damage);

    self->hp -= dmg.amount;
    printf("%s takes %d damage\n", self->name, dmg.amount);

    if (self->hp <= 0) {
        [[maybe_unused]] const bool ok = eb_publish(bus, EV_DIED);
        assert(ok);
    }
}

static void on_damage_report(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    UNUSED(bus);
    EB_CTX_EXPECT(ctx, Enemy);

    const Enemy *self = EB_CTX_CPTR(ctx, Enemy);
    printf("  %s is at %d hp\n", self->name, self->hp);
}

static void on_died(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ev);
    EB_CTX_EXPECT(ctx, Enemy);

    Enemy *self = EB_CTX_PTR(ctx, Enemy);
    printf("  %s died\n", self->name);

    const size_t dropped = eb_unsubscribe_by_ctx(bus, self);
    printf("  dropped %zu subscriptions\n", dropped);

    free(self);
}

int main() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    assert(bus);

    Enemy *goblin = malloc(sizeof(Enemy));
    assert(goblin);
    *goblin = (Enemy){.name = "goblin", .hp = 5};

    [[maybe_unused]] bool ok = eb_subscribe(bus, EV_DAMAGE, on_damage_apply, goblin);
    assert(ok);

    ok = eb_subscribe(bus, EV_DAMAGE, on_damage_report, goblin);
    assert(ok);

    ok = eb_subscribe(bus, EV_DIED, on_died, goblin);
    assert(ok);

    Damage light = {.amount = 2};
    EB_PUBLISH(bus, EV_DAMAGE, light);

    Damage fatal = {.amount = 3};
    EB_PUBLISH(bus, EV_DAMAGE, fatal); // on_damage_report never runs

    EB_PUBLISH(bus, EV_DAMAGE, light); // nobody is listening any more

    eb_bus_destroy(bus);
}
