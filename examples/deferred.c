// Deferred delivery: eb_post queues an event, eb_drain dispatches it later.
//
// A frame loop is the natural home for this. The tick dispatch walks every
// enemy, so an enemy that dies must not free itself right there -- it posts
// EV_DIED instead, and the reaper runs at the end of the frame, once the walk
// is over. The reaper in turn posts EV_LOOT, which by the snapshot rule lands
// in the NEXT drain: a drain handles what was queued when it started, never
// what its own handlers add. That is what keeps two handlers posting to each
// other from spinning a drain forever.

#include "eb/event_bus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(x) (void) (x)

typedef enum : int32_t {
    EV_TICK = 0,
    EV_DIED,
    EV_LOOT,
    EV_COUNT
} GameEvent;

typedef struct {
    const char *name;
    int hp;
} Enemy;

typedef struct {
    int damage;
} Tick;

typedef struct {
    Enemy *who;
} Died;

typedef struct {
    const char *from;
    int gold;
} Loot;

static void on_tick(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    EB_EV_EXPECT(ev, Tick);
    EB_CTX_EXPECT(ctx, Enemy);

    Enemy *self = EB_CTX_PTR(ctx, Enemy);
    const Tick tick = EB_EV_VAL(ev, Tick);

    self->hp -= tick.damage;
    printf("   %s: %d hp\n", self->name, self->hp);

    if (self->hp <= 0) {
        // NOT eb_publish: unsubscribing and freeing here would happen while the
        // tick dispatch is still walking the subscriber list
        const Died died = {.who = self};
        [[maybe_unused]] const bool ok = eb_post_data(bus, EV_DIED, &died, sizeof(died));
        assert(ok);
    }
}

static void on_died(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(ctx);
    EB_EV_EXPECT(ev, Died);

    Enemy *who = EB_EV_VAL(ev, Died).who;
    printf("   reaped %s\n", who->name);

    [[maybe_unused]] const size_t dropped = eb_unsubscribe_by_ctx(bus, who);
    assert(dropped == 1);
    free(who);

    // posted from inside a drain, so it waits for the next one
    const Loot loot = {.from = "a corpse", .gold = 10};
    [[maybe_unused]] const bool ok = eb_post_data(bus, EV_LOOT, &loot, sizeof(loot));
    assert(ok);
}

static void on_loot(const eb_Event *ev, eb_EventBus *bus, void *ctx) {
    UNUSED(bus);
    UNUSED(ctx);
    EB_EV_EXPECT(ev, Loot);

    const Loot loot = EB_EV_VAL(ev, Loot);
    printf("   looted %d gold from %s\n", loot.gold, loot.from);
}

// ownership goes to the bus: the reaper frees the enemy when it dies
static void spawn(eb_EventBus *bus, const char *name, int hp) {
    Enemy *e = malloc(sizeof(Enemy));
    assert(e);
    *e = (Enemy){.name = name, .hp = hp};

    [[maybe_unused]] const bool ok = eb_subscribe(bus, EV_TICK, on_tick, e);
    assert(ok);
}

int main() {
    eb_EventBus *bus = eb_bus_create(EV_COUNT);
    assert(bus);

    [[maybe_unused]] bool ok = eb_subscribe(bus, EV_DIED, on_died, nullptr);
    assert(ok);
    ok = eb_subscribe(bus, EV_LOOT, on_loot, nullptr);
    assert(ok);

    spawn(bus, "goblin", 5);
    spawn(bus, "orc", 4);
    spawn(bus, "rat", 2);

    const Tick tick = {.damage = 2};

    for (int frame = 1; frame <= 3; ++frame) {
        printf("frame %d\n", frame);

        printf("  update\n");
        EB_PUBLISH(bus, EV_TICK, tick);

        printf("  drain (%zu queued)\n", eb_count_posted(bus));
        (void) eb_drain(bus);
    }

    // the last reaper's loot is still waiting: one drain behind, by design
    printf("teardown\n");
    printf("  drain (%zu queued)\n", eb_count_posted(bus));
    (void) eb_drain(bus);

    // nobody outlived the fight, so the reaper freed every enemy. Survivors
    // would have to be freed here instead -- this asserts we have none.
    assert(eb_count_subscribers(bus, EV_TICK) == 0);
    assert(eb_count_posted(bus) == 0);

    eb_bus_destroy(bus);
}
