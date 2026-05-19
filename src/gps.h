// adsb-radar — GPSState struct, GPS thread entry point, and GPSFixStatus enum
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "config.h"
#include <stdbool.h>
#include <SDL3/SDL.h>

typedef enum {
    GPS_STATUS_NO_DEVICE = 0,
    GPS_STATUS_NO_FIX,
    GPS_STATUS_FIX,
} GPSFixStatus;

typedef struct {
    /* Written by GPS thread, read by main — protected by gps_mutex */
    double        lat;
    double        lon;
    GPSFixStatus  fix_status;
    Uint64        fix_time_ms;
    SDL_Mutex    *gps_mutex;

    /* Main thread only — no lock needed */
    bool          lock_enabled;
    double        last_centred_lat;  /* sentinel 1000.0 = force first re-centre */
    double        last_centred_lon;
} GPSState;

typedef struct {
    const Config  *config;
    GPSState      *gps;
    SDL_Mutex     *quit_mutex;
    SDL_Condition *wake_cond;
    volatile bool *quit;
} GPSThreadArgs;

int gps_thread_func(void *arg);
