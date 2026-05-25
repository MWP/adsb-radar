// adsb-radar — top-level AppState: initialises and tears down all subsystems
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_state.h"
#include "network.h"
#include "gps.h"
#include "map_geojson.h"
#include "map_shapefile.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

static bool init_sdl(AppState *app)
{
    const Config *cfg = &app->config;

    /* Prevent SDL from synthesizing mouse events from touch — without this a single
       tap fires both FINGER_DOWN and a fake MOUSE_BUTTON_DOWN, double-toggling
       state buttons like the AC list. */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    /* Set video driver only if explicitly configured — otherwise let SDL auto-detect.
       On a bare TTY SDL picks kmsdrm automatically; on X11/Wayland it picks those. */
    if (cfg->video_driver[0]) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, cfg->video_driver);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Video driver override: %s", cfg->video_driver);
    }
    /* KMS/DRM tuning hints — harmless when using other backends */
    {
        char dev[4];
        snprintf(dev, sizeof(dev), "%d", cfg->kmsdrm_device_index);
        SDL_SetHint("SDL_KMSDRM_DEVICE_INDEX", dev);
        SDL_SetHint("SDL_KMSDRM_ATOMIC", "1");
        SDL_SetHint("SDL_KMSDRM_REQUIRE_DRM_MASTER", "1");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    /* Resolve screen dimensions */
    int w = cfg->screen_width;
    int h = cfg->screen_height;
    if (w == 0 || h == 0) {
        int count = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&count);
        if (count > 0) {
            SDL_Rect bounds;
            SDL_GetDisplayBounds(displays[0], &bounds);
            w = bounds.w;
            h = bounds.h;
        }
        SDL_free(displays);
        if (w == 0) w = 1920;
        if (h == 0) h = 1080;
    }

    SDL_WindowFlags wflags = cfg->fullscreen ? SDL_WINDOW_FULLSCREEN
                                              : SDL_WINDOW_RESIZABLE;
    app->window = SDL_CreateWindow("adsb-radar", w, h, wflags);
    if (!app->window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    /* Try best available renderer */
    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Auto renderer failed, trying software: %s", SDL_GetError());
        app->renderer = SDL_CreateRenderer(app->window, "software");
    }
    if (!app->renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    SDL_SetRenderVSync(app->renderer, 0);   /* frame rate managed in main loop */

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Renderer: %s, window: %dx%d",
                SDL_GetRendererName(app->renderer), w, h);
    return true;
}

static bool init_ttf(AppState *app)
{
    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TTF_Init failed: %s", SDL_GetError());
        return false;
    }

    app->font = TTF_OpenFont(app->config.font_path,
                              app->config.font_size_pt);
    if (!app->font) {
        /* Try system font on Linux */
        app->font = TTF_OpenFont(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            app->config.font_size_pt);
    }
    if (!app->font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Cannot open font '%s': %s",
                     app->config.font_path, SDL_GetError());
        return false;
    }

    app->text_engine = TTF_CreateRendererTextEngine(app->renderer);
    if (!app->text_engine) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TTF_CreateRendererTextEngine failed: %s", SDL_GetError());
        return false;
    }

    /* List font — separate path/size or fall back to main font */
    const char *lpath = app->config.list_font_path[0]
                        ? app->config.list_font_path
                        : app->config.font_path;
    float lsize = app->config.list_font_size_pt > 0.0f
                  ? app->config.list_font_size_pt
                  : app->config.font_size_pt;
    app->list_font = TTF_OpenFont(lpath, lsize);
    if (!app->list_font)
        app->list_font = app->font;   /* silent fallback */

    return true;
}

static bool load_map(AppState *app)
{
    int w, h;
    SDL_GetRenderOutputSize(app->renderer, &w, &h);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Render output size: %dx%d", w, h);

    {
        int nt = 0;
        SDL_TouchID *touches = SDL_GetTouchDevices(&nt);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Touch devices: %d", nt);
        for (int i = 0; i < nt; i++) {
            const char *name = SDL_GetTouchDeviceName(touches[i]);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "  Touch[%d]: %s", i, name ? name : "(unknown)");
        }
        SDL_free(touches);
    }

    projection_init(&app->proj,
                    app->config.center_lat, app->config.center_lon,
                    app->config.zoom_level, w, h);

    app->map_layer_count = 0;
    for (int i = 0; i < app->config.map_layer_count; i++) {
        const MapLayerConfig *lc = &app->config.map_layers[i];
        MapGeometry *geo = NULL;

        switch (lc->type) {
        case MAP_GEOJSON:
            geo = map_geojson_load(lc->path);
            if (!geo)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "GeoJSON load failed for '%s'", lc->path);
            break;
        case MAP_SHAPEFILE:
            geo = map_shapefile_load(lc->path);
            if (!geo)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Shapefile load failed for '%s'", lc->path);
            break;
        case MAP_NONE:
        default:
            break;
        }

        if (geo) {
            map_geometry_reproject(geo, &app->proj);
            int slot = app->map_layer_count;
            app->map_layers[slot] = geo;
            if (lc->show_names) {
                float sz = lc->name_font_size_pt > 0.0f
                           ? lc->name_font_size_pt
                           : app->config.font_size_pt;
                if (sz == app->config.font_size_pt) {
                    app->map_layer_fonts[slot] = app->font;
                } else {
                    TTF_Font *mf = TTF_OpenFont(app->config.font_path, sz);
                    if (!mf) mf = app->font;
                    app->map_layer_fonts[slot] = mf;
                }
            }
            app->map_layer_count++;
        }
    }

    return true;
}

bool app_state_init(AppState *app, int argc, char **argv)
{
    memset(app, 0, sizeof(*app));

    /* Config: optional path from argv[1] */
    config_load(&app->config, argc > 1 ? argv[1] : NULL);

    if (!init_sdl(app))  return false;
    if (!init_ttf(app))  return false;

    aircraft_list_init(&app->aircraft_list);
    label_cache_init(&app->label_cache);
    input_init(&app->input);
    app->max_range_km = 0;

    /* GPS state */
    memset(&app->gps, 0, sizeof(app->gps));
    app->gps.gps_mutex        = SDL_CreateMutex();
    app->gps.fix_status       = GPS_STATUS_NO_DEVICE;
    app->gps.lock_enabled     = app->config.gps_lock_on_start;
    app->gps.last_centred_lat = 1000.0;
    app->gps.last_centred_lon = 1000.0;
    app->gps_thread           = NULL;

    load_map(app);

    /* Threading primitives */
    app->quit_mutex = SDL_CreateMutex();
    app->wake_cond  = SDL_CreateCondition();
    app->quit       = false;

    /* Start GPS thread if enabled */
    if (app->config.gps_enabled) {
        GPSThreadArgs *gps_args = malloc(sizeof(GPSThreadArgs));
        gps_args->config     = &app->config;
        gps_args->gps        = &app->gps;
        gps_args->quit_mutex = app->quit_mutex;
        gps_args->wake_cond  = app->wake_cond;
        gps_args->quit       = &app->quit;
        app->gps_thread = SDL_CreateThread(gps_thread_func, "gps", gps_args);
        if (!app->gps_thread) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "GPS thread failed: %s", SDL_GetError());
            free(gps_args);
        }
    }

    /* Start network thread */
    NetworkThreadArgs *args = malloc(sizeof(NetworkThreadArgs));
    args->config        = &app->config;
    args->aircraft_list = &app->aircraft_list;
    args->quit_mutex    = app->quit_mutex;
    args->wake_cond     = app->wake_cond;
    args->quit          = &app->quit;

    app->net_thread = SDL_CreateThread(network_thread_func,
                                        "network", args);
    if (!app->net_thread) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateThread failed: %s", SDL_GetError());
        free(args);
        return false;
    }

    return true;
}

void app_state_destroy(AppState *app)
{
    /* Signal all background threads to quit */
    if (app->net_thread || app->gps_thread) {
        SDL_LockMutex(app->quit_mutex);
        app->quit = true;
        SDL_BroadcastCondition(app->wake_cond);   /* wakes both net and gps threads */
        SDL_UnlockMutex(app->quit_mutex);
        if (app->gps_thread) {
            SDL_WaitThread(app->gps_thread, NULL);
            app->gps_thread = NULL;
        }
        if (app->net_thread) {
            SDL_WaitThread(app->net_thread, NULL);
            app->net_thread = NULL;
        }
    }

    if (app->gps.gps_mutex) {
        SDL_DestroyMutex(app->gps.gps_mutex);
        app->gps.gps_mutex = NULL;
    }

    if (app->wake_cond)  { SDL_DestroyCondition(app->wake_cond);  app->wake_cond  = NULL; }
    if (app->quit_mutex) { SDL_DestroyMutex(app->quit_mutex);     app->quit_mutex = NULL; }

    label_cache_destroy(&app->label_cache);
    aircraft_list_destroy(&app->aircraft_list);

    for (int i = 0; i < app->map_layer_count; i++) {
        map_geometry_free(app->map_layers[i]);
        app->map_layers[i] = NULL;
    }
    app->map_layer_count = 0;

    if (app->text_engine) {
        TTF_DestroyRendererTextEngine(app->text_engine);
        app->text_engine = NULL;
    }
    for (int i = 0; i < app->map_layer_count; i++) {
        TTF_Font *mf = app->map_layer_fonts[i];
        if (mf && mf != app->font && mf != app->list_font)
            TTF_CloseFont(mf);
        app->map_layer_fonts[i] = NULL;
    }
    if (app->list_font && app->list_font != app->font) {
        TTF_CloseFont(app->list_font);
        app->list_font = NULL;
    }
    if (app->font) {
        TTF_CloseFont(app->font);
        app->font = NULL;
    }
    TTF_Quit();

    if (app->renderer) { SDL_DestroyRenderer(app->renderer); app->renderer = NULL; }
    if (app->window)   { SDL_DestroyWindow(app->window);     app->window   = NULL; }
    SDL_Quit();
}
