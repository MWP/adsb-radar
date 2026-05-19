// adsb-radar — SDL render pipeline: map layers, aircraft dots, trails, labels, HUD, and buttons
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render.h"
#include "app_state.h"
#include "gps.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


void draw_filled_circle(SDL_Renderer *renderer, float cx, float cy, float r)
{
    int ir = (int)(r + 0.5f);
    for (int dy = -ir; dy <= ir; dy++) {
        int half = (int)sqrtf((float)(ir * ir - dy * dy));
        SDL_RenderLine(renderer, cx - half, cy + dy, cx + half, cy + dy);
    }
}

void draw_circle_approx(SDL_Renderer *renderer,
                        float cx, float cy, float r, int segments)
{
    if (segments < 8)   segments = 8;
    if (segments > 256) segments = 256;
    SDL_FPoint pts[257];   /* max 256 segments + closing point, on the stack */
    for (int i = 0; i <= segments; i++) {
        double angle = 2.0 * M_PI * i / segments;
        pts[i].x = cx + (float)(r * cos(angle));
        pts[i].y = cy + (float)(r * sin(angle));
    }
    SDL_RenderLines(renderer, pts, segments + 1);
}

#define KM_PER_NM 1.852f

/* scale = pixels/radian of longitude (correct after projection_init fix).
   In a Mercator projection the local scale factor at centre lat is 1/cos(lat),
   so pixels per radian of true arc = scale/cos(lat).
   1 nm = π/10800 radians of arc. */
static float nm_to_pixels(const ProjectionState *p, float nm)
{
    double cos_lat = cos(p->center_lat * M_PI / 180.0);
    if (cos_lat < 1e-6) cos_lat = 1e-6;
    return (float)(nm * p->scale * M_PI / (10800.0 * cos_lat));
}

static float km_to_pixels(const ProjectionState *p, float km)
{
    return nm_to_pixels(p, km / KM_PER_NM);
}

/* Draw a short text label using a transient TTF_Text. */
static void draw_text(AppState *app, const char *str,
                      float x, float y,
                      Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    TTF_Text *t = TTF_CreateText(app->text_engine, app->font, str, 0);
    if (!t) return;
    TTF_SetTextColor(t, r, g, b, a);
    TTF_DrawRendererText(t, x, y);
    TTF_DestroyText(t);
}

static void draw_text_f(AppState *app, TTF_Font *font, const char *str,
                        float x, float y,
                        Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    TTF_Text *t = TTF_CreateText(app->text_engine, font, str, 0);
    if (!t) return;
    TTF_SetTextColor(t, r, g, b, a);
    TTF_DrawRendererText(t, x, y);
    TTF_DestroyText(t);
}

static void draw_crosshair(AppState *app)
{
    float cx = (float)(app->proj.screen_w / 2);
    float cy = (float)(app->proj.screen_h / 2);
    const Uint8 *rc = app->config.col_range_circle;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    /* Slightly brighter than range circle colour */
    Uint8 r = (Uint8)SDL_min(255, (int)rc[0] + 80);
    Uint8 g = (Uint8)SDL_min(255, (int)rc[1] + 80);
    Uint8 b = (Uint8)SDL_min(255, (int)rc[2] + 80);
    SDL_SetRenderDrawColor(app->renderer, r, g, b, 220);

    const float arm = 20.0f, gap = 6.0f;
    SDL_RenderLine(app->renderer, cx - arm, cy, cx - gap, cy);
    SDL_RenderLine(app->renderer, cx + gap, cy, cx + arm, cy);
    SDL_RenderLine(app->renderer, cx, cy - arm, cx, cy - gap);
    SDL_RenderLine(app->renderer, cx, cy + gap, cx, cy + arm);
}

/* Map altitude (feet) to an RGB colour.
   Gradient: red (0 m) → green (~5000 m) → blue (~10000 m) → purple (15000 m).
   Uses HSV with hue sweeping 0°→270° across [0, 15000 m]. */
static void altitude_color(float alt_ft, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (isnan(alt_ft)) { *r = 0; *g = 220; *b = 0; return; }   /* default green */

    float alt_m = alt_ft * 0.3048f;
    float t = alt_m / 15000.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float hue = t * 270.0f;          /* 0 = red, 120 = green, 240 = blue, 270 = purple */
    float h   = hue / 60.0f;
    int   i   = (int)h;
    float f   = h - (float)i;
    float q   = 1.0f - f;            /* S=1, V=1 → p=0 */

    float rv, gv, bv;
    switch (i) {
    case 0:  rv=1; gv=f; bv=0; break;
    case 1:  rv=q; gv=1; bv=0; break;
    case 2:  rv=0; gv=1; bv=f; break;
    case 3:  rv=0; gv=q; bv=1; break;
    default: rv=f; gv=0; bv=1; break;   /* covers case 4 (blue→purple) */
    }
    *r = (Uint8)(rv * 255.0f);
    *g = (Uint8)(gv * 255.0f);
    *b = (Uint8)(bv * 255.0f);
}

static void draw_range_circles(AppState *app)
{
    float cx = (float)(app->proj.screen_w / 2);
    float cy = (float)(app->proj.screen_h / 2);
    const Config *cfg = &app->config;
    bool  metric      = cfg->units_metric;

    SDL_SetRenderDrawColor(app->renderer,
                           cfg->col_range_circle[0],
                           cfg->col_range_circle[1],
                           cfg->col_range_circle[2],
                           cfg->col_range_circle[3]);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    /* ── Dynamic ring spacing ───────────────────────────────────────
       Pick the smallest "nice" interval that spaces rings ~150 px apart. */
    static const float nm_nice[] = {
        0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f,
        100.0f, 200.0f, 500.0f, 1000.0f
    };
    static const float km_nice[] = {
        1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f,
        100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f
    };
    const float *nice      = metric ? km_nice : nm_nice;
    const int    nice_n    = metric ? 11 : 11;
    const char  *unit_str  = metric ? "km" : "nm";

    /* Pixels per one display unit (km or nm) */
    float px_per_unit = metric ? km_to_pixels(&app->proj, 1.0f)
                                : nm_to_pixels(&app->proj, 1.0f);

    float ideal = 150.0f / (px_per_unit > 0.001f ? px_per_unit : 0.001f);
    float interval = nice[nice_n - 1];
    for (int i = 0; i < nice_n; i++) {
        if (nice[i] >= ideal) { interval = nice[i]; break; }
    }

    /* Draw up to 5 rings */
    Uint8 lr = (Uint8)SDL_min(255, (int)cfg->col_range_circle[0] + 60);
    Uint8 lg = (Uint8)SDL_min(255, (int)cfg->col_range_circle[1] + 60);
    Uint8 lb = (Uint8)SDL_min(255, (int)cfg->col_range_circle[2] + 60);

    for (int i = 1; i <= 5; i++) {
        float dist = interval * (float)i;
        float r_px = metric ? km_to_pixels(&app->proj, dist)
                             : nm_to_pixels(&app->proj, dist);

        if (r_px < 20.0f) continue;
        if (r_px > (float)app->proj.screen_w * 2.0f) break;

        draw_circle_approx(app->renderer, cx, cy, r_px, 128);

        /* Distance label — integer if >= 2 units, one decimal below */
        char lbl[24];
        if (dist < 2.0f)
            snprintf(lbl, sizeof(lbl), "%.1f%s", dist, unit_str);
        else
            snprintf(lbl, sizeof(lbl), "%d%s", (int)dist, unit_str);

        /* Place label at top of circle, shifted slightly right */
        float ly = cy - r_px - 16.0f;
        if (ly < 4.0f) ly = cy - r_px + 4.0f;   /* flip below if off-screen */
        draw_text(app, lbl, cx + 4.0f, ly, lr, lg, lb, 220);
    }
}

/* Equirectangular distance between two lat/lon points in km.
   Accurate to ~0.5% for ranges up to 500 km — sufficient for display. */
static double dist_km(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = lat2 - lat1;
    double dlon = (lon2 - lon1) * cos((lat1 + lat2) * 0.5 * M_PI / 180.0);
    return sqrt(dlat * dlat + dlon * dlon) * 111.195;
}

/* Geographic reference point for max-range: GPS fix if enabled, else INI centre. */
static void max_range_ref(AppState *app, double *lat, double *lon)
{
    *lat = app->config.center_lat;
    *lon = app->config.center_lon;
    if (app->config.gps_enabled) {
        SDL_LockMutex(app->gps.gps_mutex);
        if (app->gps.fix_status == GPS_STATUS_FIX) {
            *lat = app->gps.lat;
            *lon = app->gps.lon;
        }
        SDL_UnlockMutex(app->gps.gps_mutex);
    }
}

/* Distance in km from (ref_lat, ref_lon) to the farthest viewport corner.
   When max_range_km exceeds this, the circle completely surrounds the viewport. */
static double max_viewport_km(AppState *app, double ref_lat, double ref_lon)
{
    float sw = (float)app->proj.screen_w;
    float sh = (float)app->proj.screen_h;
    float cx[4] = { 0,  sw, 0,  sw };
    float cy[4] = { 0,  0,  sh, sh };
    double best = 0.0;
    for (int i = 0; i < 4; i++) {
        double lat, lon;
        screen_to_geo(&app->proj, cx[i], cy[i], &lat, &lon);
        double d = dist_km(ref_lat, ref_lon, lat, lon);
        if (d > best) best = d;
    }
    return best;
}

/* Convert km to screen pixels at the given latitude. */
static float km_to_px_at(const ProjectionState *p, double km, double lat)
{
    double cos_lat = cos(lat * M_PI / 180.0);
    if (cos_lat < 1e-6) cos_lat = 1e-6;
    return (float)(km / KM_PER_NM * p->scale * M_PI / (10800.0 * cos_lat));
}

static void draw_max_range_fill(AppState *app)
{
    if (!app->config.max_range_circle || !app->config.max_range_fill
        || app->max_range_km <= 0.0) return;

    double ref_lat, ref_lon;
    max_range_ref(app, &ref_lat, &ref_lon);

    float r_px = km_to_px_at(&app->proj, app->max_range_km, ref_lat);
    if (r_px < 2.0f) return;

    float rx = (float)(app->proj.screen_w / 2);
    float ry = (float)(app->proj.screen_h / 2);
    geo_to_screen(&app->proj, ref_lat, ref_lon, &rx, &ry);

    const Uint8 *c = app->config.col_max_range_fill;
    SDL_SetRenderDrawColor(app->renderer, c[0], c[1], c[2], c[3]);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    if (app->max_range_km >= max_viewport_km(app, ref_lat, ref_lon)) {
        /* Circle surrounds the entire viewport: fill the screen. */
        SDL_FRect full = { 0, 0, (float)app->proj.screen_w, (float)app->proj.screen_h };
        SDL_RenderFillRect(app->renderer, &full);
    } else {
        draw_filled_circle(app->renderer, rx, ry, r_px);
    }
}

static void draw_max_range_circle(AppState *app)
{
    if (!app->config.max_range_circle || app->max_range_km <= 0.0) return;

    double ref_lat, ref_lon;
    max_range_ref(app, &ref_lat, &ref_lon);

    float r_px = km_to_px_at(&app->proj, app->max_range_km, ref_lat);
    if (r_px < 2.0f) return;

    float rx = (float)(app->proj.screen_w / 2);
    float ry = (float)(app->proj.screen_h / 2);
    geo_to_screen(&app->proj, ref_lat, ref_lon, &rx, &ry);

    const Uint8 *c = app->config.col_max_range;
    SDL_SetRenderDrawColor(app->renderer, c[0], c[1], c[2], c[3]);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    /* Only draw if the circle doesn't completely surround the viewport —
       when it does, SDL's line clipper rejects every polygon segment. */
    if (app->max_range_km < max_viewport_km(app, ref_lat, ref_lon))
        draw_circle_approx(app->renderer, rx, ry, r_px, 128);
}

static void draw_trails(AppState *app)
{
    const float trail_s = app->config.trail_seconds;
    if (trail_s <= 0.0f || !app->aircraft_list.trails) return;

    Uint64 now_ms = SDL_GetTicks();
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    SDL_LockMutex(app->aircraft_list.mutex);

    for (int ai = 0; ai < AIRCRAFT_MAX; ai++) {
        const Aircraft *ac = &app->aircraft_list.entries[ai];
        if (!ac->active || !ac->has_position) continue;

        const TrailBuffer *tb = &app->aircraft_list.trails[ai];
        if (tb->count < 2) continue;

        bool ac_inactive = ((float)(now_ms - ac->last_updated_ms) / 1000.0f)
                           >= app->config.inactive_seconds;

        float prev_sx = 0.0f, prev_sy = 0.0f;
        bool  have_prev = false;

        for (int j = 0; j < tb->count; j++) {
            int idx = (tb->head - tb->count + j + MAX_TRAIL_POINTS) % MAX_TRAIL_POINTS;
            const TrailPoint *tp = &tb->pts[idx];

            float age_s = (float)(now_ms - tp->ts_ms) / 1000.0f;
            if (age_s > trail_s) { have_prev = false; continue; }

            float sx, sy;
            if (!geo_to_screen(&app->proj, tp->lat, tp->lon, &sx, &sy)) {
                have_prev = false;
                continue;
            }

            if (have_prev) {
                /* Fade: newest segment = 60% opacity, oldest = 0% */
                float t     = 1.0f - (age_s / trail_s);
                Uint8 alpha = (Uint8)(t * 155.0f);

                Uint8 r, g, b;
                if (ac_inactive) {
                    r = app->config.col_inactive[0];
                    g = app->config.col_inactive[1];
                    b = app->config.col_inactive[2];
                } else {
                    altitude_color(tp->alt_baro, &r, &g, &b);
                }
                SDL_SetRenderDrawColor(app->renderer, r, g, b, alpha);

                /* Dotted segment: one point every 5 px (20% duty) */
                float dx  = sx - prev_sx, dy = sy - prev_sy;
                float len = sqrtf(dx * dx + dy * dy);
                if (len >= 1.0f) {
                    float inv = 1.0f / len;
                    float nx  = dx * inv, ny = dy * inv;
                    SDL_FPoint pts[96];
                    int n = 0;
                    for (float d = 0.0f; d <= len && n < 96; d += 5.0f)
                        pts[n++] = (SDL_FPoint){ prev_sx + nx * d, prev_sy + ny * d };
                    if (n > 0)
                        SDL_RenderPoints(app->renderer, pts, n);
                }
            }

            prev_sx = sx; prev_sy = sy;
            have_prev = true;
        }
    }

    SDL_UnlockMutex(app->aircraft_list.mutex);
}

static void draw_map_layer(SDL_Renderer *renderer, const MapGeometry *geo,
                            const Uint8 color[4])
{
    SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], color[3]);
    SDL_SetRenderDrawBlendMode(renderer,
                               color[3] < 255 ? SDL_BLENDMODE_BLEND
                                              : SDL_BLENDMODE_NONE);

    /* Reusable point buffer — grows to the largest polyline seen, never shrinks.
       Avoids per-polyline malloc in the hot render path. */
    static SDL_FPoint *pt_buf     = NULL;
    static int         pt_buf_cap = 0;

    for (int i = 0; i < geo->nlines; i++) {
        const Polyline *pl = &geo->lines[i];
        if (pl->npoints < 2) continue;

        if (pl->npoints > pt_buf_cap) {
            SDL_FPoint *tmp = realloc(pt_buf,
                                      (size_t)pl->npoints * sizeof(SDL_FPoint));
            if (!tmp) continue;
            pt_buf     = tmp;
            pt_buf_cap = pl->npoints;
        }

        int seg_start = 0;
        for (int j = 0; j <= pl->npoints; j++) {
            bool end      = (j == pl->npoints);
            bool sentinel = (!end && (pl->xs[j] < -9000.0f || pl->ys[j] < -9000.0f));

            if (!end && !sentinel) {
                pt_buf[j - seg_start].x = pl->xs[j];
                pt_buf[j - seg_start].y = pl->ys[j];
            }
            if ((end || sentinel) && j - seg_start >= 2)
                SDL_RenderLines(renderer, pt_buf, j - seg_start);
            if (sentinel)
                seg_start = j + 1;
        }
    }
}

static void draw_map(AppState *app)
{
    if (app->map_layer_count == 0) return;
    const Config *cfg = &app->config;

    for (int li = 0; li < app->map_layer_count; li++) {
        const MapGeometry    *geo = app->map_layers[li];
        const MapLayerConfig *lc  = &cfg->map_layers[li];
        const Uint8          *col = lc->has_color ? lc->color : cfg->col_map;
        draw_map_layer(app->renderer, geo, col);
    }
}

void render_zoom_button_rects(int screen_w, int screen_h,
                               SDL_FRect *btn_in, SDL_FRect *btn_out)
{
    float x = (float)screen_w - BTN_MARGIN - BTN_SZ;
    float y = (float)screen_h - BTN_MARGIN - BTN_SZ * 2.0f - BTN_GAP;
    *btn_in  = (SDL_FRect){ x, y,                      BTN_SZ, BTN_SZ };
    *btn_out = (SDL_FRect){ x, y + BTN_SZ + BTN_GAP,   BTN_SZ, BTN_SZ };
}

void render_pan_button_rects(int screen_w, int screen_h, SDL_FRect out[4])
{
    float zoom_x  = (float)screen_w - BTN_MARGIN - BTN_SZ;
    float dpad_cx = zoom_x - BTN_GAP - BTN_SZ - BTN_GAP - BTN_SZ * 0.5f;
    float dpad_cy = (float)screen_h - BTN_MARGIN - BTN_SZ - BTN_GAP * 0.5f;

    out[0] = (SDL_FRect){ dpad_cx - BTN_SZ * 0.5f,            dpad_cy - BTN_GAP * 0.5f - BTN_SZ, BTN_SZ, BTN_SZ };
    out[1] = (SDL_FRect){ dpad_cx - BTN_SZ * 0.5f,            dpad_cy + BTN_GAP * 0.5f,           BTN_SZ, BTN_SZ };
    out[2] = (SDL_FRect){ dpad_cx - BTN_SZ * 1.5f - BTN_GAP,  dpad_cy - BTN_SZ * 0.5f,            BTN_SZ, BTN_SZ };
    out[3] = (SDL_FRect){ dpad_cx + BTN_SZ * 0.5f + BTN_GAP,  dpad_cy - BTN_SZ * 0.5f,            BTN_SZ, BTN_SZ };
}

void render_home_button_rect(int screen_w, int screen_h, SDL_FRect *out)
{
    out->x = (float)screen_w - BTN_MARGIN - BTN_SZ;
    out->y = (float)screen_h - BTN_MARGIN - BTN_SZ * 3.0f - BTN_GAP * 2.0f;
    out->w = BTN_SZ;
    out->h = BTN_SZ;
}

void render_gps_lock_button_rect(int screen_w, int screen_h, SDL_FRect *out)
{
    out->x = (float)screen_w - BTN_MARGIN - BTN_SZ;
    out->y = (float)screen_h - BTN_MARGIN - BTN_SZ * 4.0f - BTN_GAP * 3.0f;
    out->w = BTN_SZ;
    out->h = BTN_SZ;
}

void render_list_toggle_rect(int screen_w, SDL_FRect *out)
{
    out->x = (float)screen_w - BTN_MARGIN - BTN_SZ;
    out->y = BTN_MARGIN;
    out->w = BTN_SZ;
    out->h = BTN_SZ;
}

void render_exit_button_rect(int screen_w, int screen_h, SDL_FRect *out)
{
    (void)screen_w;
    out->x = BTN_MARGIN;
    out->y = (float)screen_h - BTN_MARGIN - BTN_SZ;
    out->w = BTN_SZ;
    out->h = BTN_SZ;
}

static void draw_button(AppState *app, const SDL_FRect *r, const char *label)
{
    SDL_SetRenderDrawColor(app->renderer, 20, 40, 20, 200);
    SDL_RenderFillRect(app->renderer, r);
    SDL_SetRenderDrawColor(app->renderer, 0, 180, 80, 220);
    SDL_RenderRect(app->renderer, r);
    int tw = 0, th = 0;
    TTF_GetStringSize(app->font, label, 0, &tw, &th);
    float lx = r->x + (r->w - (float)tw) * 0.5f;
    float ly = r->y + (r->h - (float)th) * 0.5f;
    draw_text(app, label, lx, ly, 0, 220, 100, 255);
}

static void draw_nav_buttons(AppState *app)
{
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    if (app->config.show_exit_button) {
        SDL_FRect ex;
        render_exit_button_rect(app->proj.screen_w, app->proj.screen_h, &ex);
        SDL_SetRenderDrawColor(app->renderer, 60, 10, 10, 200);
        SDL_RenderFillRect(app->renderer, &ex);
        SDL_SetRenderDrawColor(app->renderer, 200, 40, 40, 220);
        SDL_RenderRect(app->renderer, &ex);
        int etw = 0, eth = 0;
        TTF_GetStringSize(app->font, "EXIT", 0, &etw, &eth);
        float lx = ex.x + (ex.w - (float)etw) * 0.5f;
        float ly = ex.y + (ex.h - (float)eth) * 0.5f;
        draw_text(app, "EXIT", lx, ly, 220, 60, 60, 255);
    }

    if (!app->config.show_buttons) return;

    int sw = app->proj.screen_w, sh = app->proj.screen_h;

    SDL_FRect btn_in, btn_out;
    render_zoom_button_rects(sw, sh, &btn_in, &btn_out);
    draw_button(app, &btn_in,  "+");
    draw_button(app, &btn_out, "\xe2\x80\x93");  /* en-dash − */

    SDL_FRect pan[4];
    render_pan_button_rects(sw, sh, pan);
    const char *pan_labels[4] = {
        "\xe2\x86\x91", "\xe2\x86\x93",   /* ↑ ↓ */
        "\xe2\x86\x90", "\xe2\x86\x92",   /* ← → */
    };
    for (int i = 0; i < 4; i++)
        draw_button(app, &pan[i], pan_labels[i]);

    SDL_FRect home;
    render_home_button_rect(sw, sh, &home);
    draw_button(app, &home, "CTR");

    if (app->config.gps_enabled) {
        SDL_FRect gps_r;
        render_gps_lock_button_rect(sw, sh, &gps_r);

        GPSFixStatus fix;
        SDL_LockMutex(app->gps.gps_mutex);
        fix = app->gps.fix_status;
        SDL_UnlockMutex(app->gps.gps_mutex);

        if (app->gps.lock_enabled && fix == GPS_STATUS_FIX)
            SDL_SetRenderDrawColor(app->renderer, 0, 60, 0, 200);
        else if (app->gps.lock_enabled)
            SDL_SetRenderDrawColor(app->renderer, 60, 50, 0, 200);
        else
            SDL_SetRenderDrawColor(app->renderer, 20, 20, 40, 200);
        SDL_RenderFillRect(app->renderer, &gps_r);
        SDL_SetRenderDrawColor(app->renderer, 180, 180, 0, 220);
        SDL_RenderRect(app->renderer, &gps_r);

        float lx = gps_r.x + gps_r.w * 0.5f - 12.0f;
        float ly = gps_r.y + gps_r.h * 0.5f - 10.0f;
        draw_text(app, "GPS", lx, ly, 220, 220, 0, 255);
    }
}

static void draw_list_toggle(AppState *app)
{
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect r;
    render_list_toggle_rect(app->proj.screen_w, &r);

    SDL_SetRenderDrawColor(app->renderer, 20, 40, 20, 200);
    SDL_RenderFillRect(app->renderer, &r);
    SDL_SetRenderDrawColor(app->renderer, 0, 180, 80, 220);
    SDL_RenderRect(app->renderer, &r);

    const char *lbl = app->ac_list_open ? "X" : "AC";
    int tw = 0, th = 0;
    TTF_GetStringSize(app->font, lbl, 0, &tw, &th);
    float lx = r.x + (r.w - (float)tw) * 0.5f;
    float ly = r.y + (r.h - (float)th) * 0.5f;
    draw_text(app, lbl, lx, ly, 0, 220, 100, 255);
}

static int cmp_ac_time(const void *a, const void *b)
{
    Uint64 ta = ((const Aircraft *)a)->last_updated_ms;
    Uint64 tb = ((const Aircraft *)b)->last_updated_ms;
    return (ta > tb) ? -1 : (ta < tb) ? 1 : 0;
}

static void draw_aircraft_list(AppState *app, Aircraft *snap, int count)
{
    if (!app->ac_list_open) {
        draw_list_toggle(app);
        return;
    }

    /* Sort most-recently-updated first */
    qsort(snap, count, sizeof(Aircraft), cmp_ac_time);

    const float PTOP   = BTN_MARGIN + BTN_SZ + BTN_GAP;
    float max_ph = (float)app->proj.screen_h;
    float line_h = app->config.font_size_pt * 1.75f;
    bool  metric = app->config.units_metric;
    const float PAD = 6.0f;

    /* Derive column widths from actual font metrics so they scale with font size
       and DPI.  char_w is an average for DejaVuSans; numeric columns are sized to
       their widest realistic value ("10000m" / "1000kt") measured directly. */
    TTF_Font *lf = app->list_font;
    int fh = TTF_GetFontHeight(lf);
    float char_w  = (float)fh * 0.58f;
    float col_gap = (float)fh * 0.35f;

    /* Callsign: up to 8 chars (ICAO hex is 6, IATA callsign up to 8) */
    float col_call = char_w * 8.0f;

    /* Altitude / speed / RSSI: measure representative wide strings */
    int alt_tw = 0, spd_tw = 0, rssi_tw = 0;
    TTF_GetStringSize(lf, metric ? "10000m"  : "39000", 0, &alt_tw,  NULL);
    TTF_GetStringSize(lf, metric ? "1000km/h" : "999kt", 0, &spd_tw,  NULL);
    TTF_GetStringSize(lf, "-50dB", 0, &rssi_tw, NULL);
    float col_alt  = (float)alt_tw;
    float col_spd  = (float)spd_tw;
    float col_rssi = (float)rssi_tw;

    float PW = PAD + col_call + col_gap + col_alt + col_gap + col_spd + col_gap + col_rssi + PAD;
    float px = (float)app->proj.screen_w - PW;

    /* Column X positions */
    float x_call = px + PAD;
    float x_alt  = x_call + col_call + col_gap;
    float x_spd  = x_alt  + col_alt  + col_gap;
    float x_rssi = x_spd  + col_spd  + col_gap;

    float panel_h = PAD + line_h * (float)(count + 1) + PAD;
    if (panel_h > max_ph - PTOP) panel_h = max_ph - PTOP;

    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(app->renderer, 5, 10, 18, 215);
    SDL_FRect panel = { px, PTOP, PW, panel_h };
    SDL_RenderFillRect(app->renderer, &panel);
    SDL_SetRenderDrawColor(app->renderer, 0, 100, 60, 180);
    SDL_RenderRect(app->renderer, &panel);

    float ty = PTOP + PAD;

    /* Header — numeric headers right-aligned to match data columns */
    draw_text_f(app, lf, "Call", x_call, ty, 60, 160, 80, 255);
    int hw = 0;
    TTF_GetStringSize(lf, "Alt", 0, &hw, NULL);
    draw_text_f(app, lf, "Alt", x_alt + col_alt - (float)hw, ty, 60, 160, 80, 255);
    TTF_GetStringSize(lf, "Spd", 0, &hw, NULL);
    draw_text_f(app, lf, "Spd", x_spd + col_spd - (float)hw, ty, 60, 160, 80, 255);
    TTF_GetStringSize(lf, "RSSI", 0, &hw, NULL);
    draw_text_f(app, lf, "RSSI", x_rssi + col_rssi - (float)hw, ty, 60, 160, 80, 255);
    ty += line_h;
    SDL_SetRenderDrawColor(app->renderer, 0, 100, 60, 120);
    SDL_RenderLine(app->renderer, px + 2, ty - 2.0f, px + PW - 2, ty - 2.0f);

    for (int i = 0; i < count && ty + line_h <= PTOP + panel_h - 4.0f; i++) {
        const Aircraft *ac = &snap[i];
        const char *id = ac->flight[0] ? ac->flight : ac->hex;

        char alt_s[12] = "---";
        if (!isnan(ac->alt_baro) && ac->alt_baro > -2000.0f) {
            if (metric) snprintf(alt_s, sizeof(alt_s), "%dm",   (int)(ac->alt_baro * 0.3048f));
            else        snprintf(alt_s, sizeof(alt_s), "%d",    (int)ac->alt_baro);
        }
        char spd_s[12] = "---";
        if (!isnan(ac->gs)) {
            if (metric) snprintf(spd_s, sizeof(spd_s), "%dkm/h", (int)(ac->gs * 1.852f));
            else        snprintf(spd_s, sizeof(spd_s), "%dkt",   (int)ac->gs);
        }
        char rssi_s[10] = "---";
        if (!isnan(ac->rssi))
            snprintf(rssi_s, sizeof(rssi_s), "%ddB", (int)ac->rssi);

        Uint8 a = (Uint8)SDL_max(80, (int)(ac->fade_alpha * 220.0f));

        /* Callsign: left-aligned */
        draw_text_f(app, lf, id, x_call, ty, 180, 220, 180, a);

        /* Altitude, speed, RSSI: right-aligned within their columns */
        int tw = 0;
        TTF_GetStringSize(lf, alt_s, 0, &tw, NULL);
        draw_text_f(app, lf, alt_s, x_alt + col_alt - (float)tw, ty, 180, 220, 180, a);

        TTF_GetStringSize(lf, spd_s, 0, &tw, NULL);
        draw_text_f(app, lf, spd_s, x_spd + col_spd - (float)tw, ty, 180, 220, 180, a);

        TTF_GetStringSize(lf, rssi_s, 0, &tw, NULL);
        draw_text_f(app, lf, rssi_s, x_rssi + col_rssi - (float)tw, ty, 180, 220, 180, a);

        ty += line_h;
    }

    /* Toggle button drawn last so it sits on top of the panel */
    draw_list_toggle(app);
}

/* Small yellow crosshair at the max-range reference point (INI centre or GPS fix). */
static void draw_center_marker(AppState *app)
{
    double ref_lat = app->config.center_lat;
    double ref_lon = app->config.center_lon;
    if (app->config.gps_enabled) {
        SDL_LockMutex(app->gps.gps_mutex);
        if (app->gps.fix_status == GPS_STATUS_FIX) {
            ref_lat = app->gps.lat;
            ref_lon = app->gps.lon;
        }
        SDL_UnlockMutex(app->gps.gps_mutex);
    }

    float sx, sy;
    if (!geo_to_screen(&app->proj, ref_lat, ref_lon, &sx, &sy)) return;
    float sw = (float)app->proj.screen_w;
    float sh = (float)app->proj.screen_h;
    if (sx < 0 || sx > sw || sy < 0 || sy > sh) return;

    SDL_SetRenderDrawColor(app->renderer, 255, 220, 0, 200);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    const float arm = 8.0f;
    SDL_RenderLine(app->renderer, sx - arm, sy, sx + arm, sy);
    SDL_RenderLine(app->renderer, sx, sy - arm, sx, sy + arm);
}

static void draw_stats_overlay(AppState *app, int active, int total_seen)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d  %H:%M:%S", tm_info);

    char line1[64], line2[64], line3[64], line4[64], line5[64];
    snprintf(line1, sizeof(line1), "%s", time_str);
    snprintf(line2, sizeof(line2), "%.4f \xc2\xb0%c  %.4f \xc2\xb0%c",
             fabs(app->proj.center_lat),
             app->proj.center_lat >= 0 ? 'N' : 'S',
             fabs(app->proj.center_lon),
             app->proj.center_lon >= 0 ? 'E' : 'W');
    snprintf(line3, sizeof(line3), "Active: %d", active);
    snprintf(line4, sizeof(line4), "Total seen: %d", total_seen);

    bool show_max = app->config.max_range_circle && app->max_range_km > 0.0;
    if (show_max) {
        if (app->config.units_metric)
            snprintf(line5, sizeof(line5), "Max range: %.0f km", app->max_range_km);
        else
            snprintf(line5, sizeof(line5), "Max range: %.0f nm", app->max_range_km / 1.852f);
    }

    float x = 12.0f;
    float y = 12.0f;
    float line_h = app->config.font_size_pt * 1.6f;
    int   n_lines = 4 + (show_max ? 1 : 0);

    /* GPS status line */
    char  gps_line[48] = "";
    Uint8 gps_r = 140, gps_g = 140, gps_b = 140;
    bool  show_gps = app->config.gps_enabled;
    if (show_gps) {
        n_lines++;
        GPSFixStatus fix;
        SDL_LockMutex(app->gps.gps_mutex);
        fix = app->gps.fix_status;
        SDL_UnlockMutex(app->gps.gps_mutex);
        switch (fix) {
        case GPS_STATUS_FIX:
            snprintf(gps_line, sizeof(gps_line),
                     app->gps.lock_enabled ? "GPS: Fix (locked)" : "GPS: Fix");
            gps_r = 0; gps_g = 220; gps_b = 80;
            break;
        case GPS_STATUS_NO_FIX:
            snprintf(gps_line, sizeof(gps_line), "GPS: No fix");
            gps_r = 220; gps_g = 180; gps_b = 0;
            break;
        default:
            snprintf(gps_line, sizeof(gps_line), "GPS: No device");
            break;
        }
    }

    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 140);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect box = { x - 6, y - 4, 300, line_h * (float)n_lines + 12 };
    SDL_RenderFillRect(app->renderer, &box);

    int row = 0;
    draw_text(app, line1, x, y + line_h * row++, 200, 220, 200, 240);
    draw_text(app, line2, x, y + line_h * row++, 180, 210, 255, 240);
    draw_text(app, line3, x, y + line_h * row++, 200, 220, 200, 240);
    draw_text(app, line4, x, y + line_h * row++, 160, 200, 160, 220);
    if (show_max)
        draw_text(app, line5, x, y + line_h * row++,
                  app->config.col_max_range[0],
                  app->config.col_max_range[1],
                  app->config.col_max_range[2], 220);
    if (show_gps)
        draw_text(app, gps_line, x, y + line_h * row, gps_r, gps_g, gps_b, 240);
}

void render_frame(AppState *app)
{
    /* Keep projection dimensions in sync with the actual renderer output.
       SDL_GetRenderOutputSize accounts for HiDPI and KMS mode-switches. */
    SDL_GetRenderOutputSize(app->renderer,
                            &app->proj.screen_w, &app->proj.screen_h);

    const Config *cfg = &app->config;
    
    /* snapshot aircraft list under minimal lock */
    Aircraft snapshot[AIRCRAFT_MAX];
    int snap_count = 0;
    SDL_LockMutex(app->aircraft_list.mutex);
    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        if (app->aircraft_list.entries[i].active)
            snapshot[snap_count++] = app->aircraft_list.entries[i];
    }
    SDL_UnlockMutex(app->aircraft_list.mutex);
    
    /* Update running max-seen-distance (km) from every aircraft with a position. */
    if (cfg->max_range_circle) {
        double ref_lat, ref_lon;
        max_range_ref(app, &ref_lat, &ref_lon);
        for (int i = 0; i < snap_count; i++) {
            const Aircraft *ac = &snapshot[i];
            if (!ac->has_position) continue;
            double d = dist_km(ref_lat, ref_lon, ac->lat, ac->lon);
            if ((d > 0) && (d < 10000) && (d > app->max_range_km))
            {
                printf("maxrange update: %f km by %i (%f,%f to %f,%f)\n", d, i, ref_lat, ref_lon, ac->lat, ac->lon);
                app->max_range_km = d;
            }
        }
    }
    
    /* 1. Clear */
    SDL_SetRenderDrawColor(app->renderer,
                           cfg->col_background[0], cfg->col_background[1],
                           cfg->col_background[2], cfg->col_background[3]);
    SDL_RenderClear(app->renderer);

    /* 1b. Max-range fill — behind everything */
    draw_max_range_fill(app);

    /* 2. Map geometry */
    draw_map(app);

    /* 3. Crosshair + range circles + reference marker */
    draw_crosshair(app);
    draw_range_circles(app);
    draw_max_range_circle(app);
    draw_center_marker(app);

    /* 4. Trails (drawn under their own lock, before dots) */
    draw_trails(app);

    int with_pos = 0;
    float sw_f = (float)app->proj.screen_w;
    float sh_f = (float)app->proj.screen_h;
    
    /* 5. Aircraft dots + heading indicators */
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < snap_count; i++) {
        const Aircraft *ac = &snapshot[i];
        if (!ac->has_position) continue;

        float sx, sy;
        if (!geo_to_screen(&app->proj, ac->lat, ac->lon, &sx, &sy)) continue;
        if (sx < 0 || sx > sw_f || sy < 0 || sy > sh_f) continue;
        with_pos++;

        Uint8 alpha = (Uint8)(ac->fade_alpha * 255.0f);

        float ac_age_s = (float)(SDL_GetTicks() - ac->last_updated_ms) / 1000.0f;
        bool  inactive = (ac_age_s >= cfg->inactive_seconds);

        /* Dot colour: grey when inactive, altitude-gradient otherwise */
        Uint8 dr, dg, db;
        if (inactive) {
            dr = cfg->col_inactive[0];
            dg = cfg->col_inactive[1];
            db = cfg->col_inactive[2];
        } else {
            altitude_color(ac->alt_baro, &dr, &dg, &db);
        }
        SDL_SetRenderDrawColor(app->renderer, dr, dg, db, alpha);
        draw_filled_circle(app->renderer, sx, sy, cfg->aircraft_dot_radius);

        if (!isnan(ac->track)) {
            /* Line length proportional to ground speed;
               heading_line_length is the reference length at 500 kt.
               Falls back to full length when speed is unknown. */
            float line_len = (isnan(ac->gs) || ac->gs == 0.0f)
                           ? 0.0f
                           : cfg->heading_line_length * ac->gs / 500.0f;

            if (line_len >= 2.0f) {
                double track_rad = ac->track * M_PI / 180.0;
                float  ex = sx + (float)(sin(track_rad) * line_len);
                float  ey = sy - (float)(cos(track_rad) * line_len);
                if (inactive) {
                    SDL_SetRenderDrawColor(app->renderer,
                                           cfg->col_inactive[0], cfg->col_inactive[1],
                                           cfg->col_inactive[2], alpha);
                } else {
                    SDL_SetRenderDrawColor(app->renderer,
                                           cfg->col_heading[0], cfg->col_heading[1],
                                           cfg->col_heading[2], alpha);
                }
                SDL_RenderLine(app->renderer, sx, sy, ex, ey);
            }
        }
    }

    /* 6. Own-position GPS marker */
    if (app->config.gps_enabled) {
        double gps_lat, gps_lon;
        GPSFixStatus gps_fix;
        SDL_LockMutex(app->gps.gps_mutex);
        gps_lat  = app->gps.lat;
        gps_lon  = app->gps.lon;
        gps_fix  = app->gps.fix_status;
        SDL_UnlockMutex(app->gps.gps_mutex);

        if (gps_fix == GPS_STATUS_FIX) {
            float sx, sy;
            if (geo_to_screen(&app->proj, gps_lat, gps_lon, &sx, &sy) &&
                sx >= 0 && sx <= sw_f && sy >= 0 && sy <= sh_f) {
                const Uint8 *c = cfg->col_gps_marker;
                const float  R = cfg->aircraft_dot_radius + 3.0f;
                SDL_SetRenderDrawColor(app->renderer, c[0], c[1], c[2], c[3]);
                SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
                draw_filled_circle(app->renderer, sx, sy, R);
                /* Black cross overlay for visibility */
                SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 220);
                float arm = R + 4.0f;
                SDL_RenderLine(app->renderer, sx - arm, sy, sx + arm, sy);
                SDL_RenderLine(app->renderer, sx, sy - arm, sx, sy + arm);
            }
        }
    }

    /* 7. Labels */
    for (int i = 0; i < snap_count; i++) {
        const Aircraft *ac = &snapshot[i];
        if (!ac->has_position) continue;

        float ac_age_s = (float)(SDL_GetTicks() - ac->last_updated_ms) / 1000.0f;
        if (ac_age_s >= cfg->inactive_seconds) continue;

        float sx, sy;
        if (!geo_to_screen(&app->proj, ac->lat, ac->lon, &sx, &sy)) continue;

        render_label(&app->label_cache, app->font, app->renderer,
                     app->text_engine, ac,
                     sx, sy - cfg->aircraft_dot_radius - 2.0f,
                     cfg->units_metric);
    }

    label_cache_prune(&app->label_cache, snapshot, snap_count);

    /* 7. Stats overlay — active = only aircraft drawn on map */
    draw_stats_overlay(app, with_pos, app->aircraft_list.total_seen);

    /* 8. Aircraft list panel — only aircraft visible within the viewport */
    int draw_count = 0;
    for (int i = 0; i < snap_count; i++) {
        if (!snapshot[i].has_position) continue;
        float sx, sy;
        if (!geo_to_screen(&app->proj, snapshot[i].lat, snapshot[i].lon, &sx, &sy)) continue;
        if (sx < 0 || sx > sw_f || sy < 0 || sy > sh_f) continue;
        snapshot[draw_count++] = snapshot[i];
    }
    draw_aircraft_list(app, snapshot, draw_count);

    /* 9. Buttons (drawn on top of list panel) */
    draw_nav_buttons(app);

    /* 9. Present */
    SDL_RenderPresent(app->renderer);
}
