#pragma once

typedef enum {
#define X(E) E,
#include "../events.def"
#undef X
    EV_TYPE__COUNT
} es_event_type_e;
