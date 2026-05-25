// adsb-radar — AppState struct aggregating all subsystem handles
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "config.h"
#include "aircraft.h"
#include "projection.h"
#include "map_geometry.h"
#include "render_labels.h"
#include "input.h"
#include "gps.h"

typedef struct AppState {
    Config           config;

    SDL_Window      *window;
    SDL_Renderer    *renderer;
    TTF_TextEngine  *text_engine;
    TTF_Font        *font;
    TTF_Font        *list_font;  /* for aircraft list panel; may equal font */

    AircraftList     aircraft_list;
    MapGeometry     *map_layers[MAX_MAP_LAYERS];
    TTF_Font        *map_layer_fonts[MAX_MAP_LAYERS]; /* NULL or font for layer names */
    int              map_layer_count;
    ProjectionState  proj;
    LabelCache       label_cache;
    InputState       input;
    bool             ac_list_open;

    GPSState         gps;
    SDL_Thread      *gps_thread;
    double           max_range_km;   /* running max; main thread only */

    SDL_Thread      *net_thread;
    SDL_Condition   *wake_cond;
    SDL_Mutex       *quit_mutex;
    volatile bool    quit;
} AppState;

/* Initialise all subsystems.  Returns true on success. */
bool app_state_init(AppState *app, int argc, char **argv);

/* Shutdown and free all resources. */
void app_state_destroy(AppState *app);
