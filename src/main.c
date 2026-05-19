// adsb-radar — SDL event loop, frame timer, and top-level quit handling
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_state.h"
#include "render.h"
#include "input.h"
#include "map_geometry.h"
#include "gps.h"

#define FRAME_INTERVAL_MS 100   /* 10 FPS — radar data updates at 500 ms anyway */

static void handle_event(AppState *app, const SDL_Event *ev,
                         bool *projection_dirty, bool *quit)
{
    if (ev->type == SDL_EVENT_QUIT) { *quit = true; return; }

    if (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        SDL_GetRenderOutputSize(app->renderer,
                                &app->proj.screen_w, &app->proj.screen_h);
        *projection_dirty = true;
        return;
    }

    if (ev->type == SDL_EVENT_KEY_DOWN) {
        switch (ev->key.key) {
        case SDLK_ESCAPE:
        case SDLK_Q:
            *quit = true;
            break;
        case SDLK_R:
            projection_reset(&app->proj);
            *projection_dirty = true;
            break;
        default:
            break;
        }
        return;
    }

    if (input_handle_event(&app->input, app, ev))
        *projection_dirty = true;
}

int main(int argc, char **argv)
{
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    AppState app;
    if (!app_state_init(&app, argc, argv))
        return 1;

    bool projection_dirty = false;
    bool quit             = false;
    Uint64 last_frame_ms  = 0;

    while (!quit) {
        /* Sleep until the next frame is due or an event wakes us early */
        Uint64 now     = SDL_GetTicks();
        Uint64 elapsed = now - last_frame_ms;
        Sint32 wait_ms = (Sint32)(elapsed < FRAME_INTERVAL_MS
                                  ? FRAME_INTERVAL_MS - elapsed : 1);

        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, wait_ms))
            handle_event(&app, &ev, &projection_dirty, &quit);

        /* Drain any further queued events without blocking */
        while (!quit && SDL_PollEvent(&ev))
            handle_event(&app, &ev, &projection_dirty, &quit);

        if (app.input.quit_requested) quit = true;

        /* Only render when the frame interval has elapsed */
        now = SDL_GetTicks();
        if (now - last_frame_ms < FRAME_INTERVAL_MS)
            continue;
        last_frame_ms = now;

        if (projection_dirty) {
            for (int i = 0; i < app.map_layer_count; i++)
                map_geometry_reproject(app.map_layers[i], &app.proj);
            projection_dirty = false;
        }

        /* GPS lock: re-centre map when position moves beyond threshold */
        if (app.config.gps_enabled && app.gps.lock_enabled) {
            double gps_lat, gps_lon;
            GPSFixStatus gps_fix;
            Uint64 fix_time;
            SDL_LockMutex(app.gps.gps_mutex);
            gps_lat  = app.gps.lat;
            gps_lon  = app.gps.lon;
            gps_fix  = app.gps.fix_status;
            fix_time = app.gps.fix_time_ms;
            SDL_UnlockMutex(app.gps.gps_mutex);

            if (gps_fix == GPS_STATUS_FIX &&
                (SDL_GetTicks() - fix_time) < 5000) {
                double dlat = gps_lat - app.gps.last_centred_lat;
                double dlon = gps_lon - app.gps.last_centred_lon;
                if (dlat * dlat + dlon * dlon > 0.0001 * 0.0001) {
                    app.proj.center_lat          = gps_lat;
                    app.proj.center_lon          = gps_lon;
                    app.gps.last_centred_lat     = gps_lat;
                    app.gps.last_centred_lon     = gps_lon;
                    for (int i = 0; i < app.map_layer_count; i++)
                        map_geometry_reproject(app.map_layers[i], &app.proj);
                }
            }
        }

        aircraft_list_update_ages(&app.aircraft_list,
                                   app.config.max_age_seconds,
                                   app.config.fade_start_seconds);
        render_frame(&app);
    }

    app_state_destroy(&app);
    return 0;
}
