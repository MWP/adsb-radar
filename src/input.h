// adsb-radar — InputState struct and handle_input declaration
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>

struct AppState;

typedef struct {
    /* Mouse/single-touch pan */
    bool         dragging;
    float        drag_last_x, drag_last_y;
    bool         drag_on_button;   /* started on a UI button — suppress pan */

    bool         quit_requested;  /* set when the exit button is tapped */

    /* Multi-touch pinch */
    int          finger_count;
    SDL_FingerID finger_id[2];
    float        finger_x[2];      /* screen pixel coords */
    float        finger_y[2];
    float        pinch_last_dist;
} InputState;

void input_init(InputState *s);

/* Process one SDL event.  Returns true if the projection changed
   (caller should reproject map geometry). */
bool input_handle_event(InputState *s, struct AppState *app,
                        const SDL_Event *ev);
