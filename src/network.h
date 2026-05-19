// adsb-radar — network thread entry point and NetworkThreadArgs declaration
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "aircraft.h"
#include "config.h"

typedef struct {
    const Config  *config;
    AircraftList  *aircraft_list;
    SDL_Mutex     *quit_mutex;
    SDL_Condition *wake_cond;
    volatile bool *quit;
} NetworkThreadArgs;

/* Thread entry point — pass a heap-allocated NetworkThreadArgs *.
   Takes ownership and frees the args struct on exit. */
int network_thread_func(void *userdata);
