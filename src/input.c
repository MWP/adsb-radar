// adsb-radar — SDL mouse, touch, and keyboard event handler; button hit testing and pan/zoom gestures
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "input.h"
#include "app_state.h"
#include "render.h"
#include "projection.h"
#include <math.h>
#include <string.h>

#define ZOOM_FACTOR_IN  1.15f
#define ZOOM_FACTOR_OUT (1.0f / 1.15f)
#define PAN_STEP        80.0f

void input_init(InputState *s)
{
    memset(s, 0, sizeof(*s));
}

static bool point_in_rect(float x, float y, const SDL_FRect *r)
{
    return x >= r->x && x <= r->x + r->w &&
           y >= r->y && y <= r->y + r->h;
}

static float finger_screen_x(const SDL_TouchFingerEvent *tf, int screen_w)
{
    return tf->x * (float)screen_w;
}

static float finger_screen_y(const SDL_TouchFingerEvent *tf, int screen_h)
{
    return tf->y * (float)screen_h;
}

static float pinch_dist(const InputState *s)
{
    float dx = s->finger_x[0] - s->finger_x[1];
    float dy = s->finger_y[0] - s->finger_y[1];
    return sqrtf(dx * dx + dy * dy);
}

/* Find slot index for a fingerID, or -1. */
static int find_finger(const InputState *s, SDL_FingerID id)
{
    for (int i = 0; i < 2; i++)
        if (i < s->finger_count && s->finger_id[i] == id)
            return i;
    return -1;
}

/* Returns true if (x,y) hit a button and the action was applied. */
/* Returns true if a button was hit. projection_changed is set to true only
   when the hit requires the map geometry to be reprojected. */
static bool check_buttons(AppState *app, float x, float y,
                           bool *projection_changed)
{
    int sw = app->proj.screen_w, sh = app->proj.screen_h;

    /* List toggle — always active regardless of show_buttons */
    SDL_FRect list_r;
    render_list_toggle_rect(sw, &list_r);
    if (point_in_rect(x, y, &list_r)) {
        app->ac_list_open = !app->ac_list_open;
        return true;
    }

    /* Exit button — active when enabled, independent of show_buttons */
    if (app->config.show_exit_button) {
        SDL_FRect ex;
        render_exit_button_rect(sw, sh, &ex);
        if (point_in_rect(x, y, &ex)) {
            app->input.quit_requested = true;
            return true;
        }
    }

    if (!app->config.show_buttons) return false;

    SDL_FRect btn_in, btn_out;
    render_zoom_button_rects(sw, sh, &btn_in, &btn_out);
    if (point_in_rect(x, y, &btn_in))  { projection_zoom(&app->proj, ZOOM_FACTOR_IN);  *projection_changed = true; return true; }
    if (point_in_rect(x, y, &btn_out)) { projection_zoom(&app->proj, ZOOM_FACTOR_OUT); *projection_changed = true; return true; }

    SDL_FRect pan[4];
    render_pan_button_rects(sw, sh, pan);
    if (point_in_rect(x, y, &pan[0])) { projection_pan(&app->proj,  0.0f,     PAN_STEP); *projection_changed = true; return true; }
    if (point_in_rect(x, y, &pan[1])) { projection_pan(&app->proj,  0.0f,    -PAN_STEP); *projection_changed = true; return true; }
    if (point_in_rect(x, y, &pan[2])) { projection_pan(&app->proj,  PAN_STEP,  0.0f);    *projection_changed = true; return true; }
    if (point_in_rect(x, y, &pan[3])) { projection_pan(&app->proj, -PAN_STEP,  0.0f);    *projection_changed = true; return true; }

    SDL_FRect home;
    render_home_button_rect(sw, sh, &home);
    if (point_in_rect(x, y, &home)) { projection_reset(&app->proj); *projection_changed = true; return true; }

    if (app->config.gps_enabled) {
        SDL_FRect gps_r;
        render_gps_lock_button_rect(sw, sh, &gps_r);
        if (point_in_rect(x, y, &gps_r)) {
            app->gps.lock_enabled     = !app->gps.lock_enabled;
            app->gps.last_centred_lat = 1000.0;   /* force immediate re-centre */
            app->gps.last_centred_lon = 1000.0;
            return true;
        }
    }

    return false;
}

bool input_handle_event(InputState *s, AppState *app, const SDL_Event *ev)
{
    bool dirty = false;

    switch (ev->type) {

    /* ── Mouse button ─────────────────────────────────────────────── */
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (ev->button.button == SDL_BUTTON_LEFT &&
            ev->button.which != SDL_TOUCH_MOUSEID) {
            float mx = ev->button.x, my = ev->button.y;
            bool proj_changed = false;
            if (check_buttons(app, mx, my, &proj_changed)) {
                dirty = proj_changed;
                s->drag_on_button = true;
            } else {
                s->dragging     = true;
                s->drag_on_button = false;
                s->drag_last_x  = mx;
                s->drag_last_y  = my;
            }
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev->button.button == SDL_BUTTON_LEFT &&
            ev->button.which != SDL_TOUCH_MOUSEID) {
            s->dragging     = false;
            s->drag_on_button = false;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (s->dragging && !s->drag_on_button &&
            ev->motion.which != SDL_TOUCH_MOUSEID) {
            float dx = ev->motion.x - s->drag_last_x;
            float dy = ev->motion.y - s->drag_last_y;
            projection_pan(&app->proj, dx, dy);
            s->drag_last_x = ev->motion.x;
            s->drag_last_y = ev->motion.y;
            dirty = true;
        }
        break;

    /* ── Mouse wheel ──────────────────────────────────────────────── */
    case SDL_EVENT_MOUSE_WHEEL:
        if (ev->wheel.y > 0)
            projection_zoom(&app->proj, ZOOM_FACTOR_IN);
        else if (ev->wheel.y < 0)
            projection_zoom(&app->proj, ZOOM_FACTOR_OUT);
        dirty = (ev->wheel.y != 0);
        break;

    /* ── Touch finger down ────────────────────────────────────────── */
    case SDL_EVENT_FINGER_DOWN: {
        int sw = app->proj.screen_w, sh = app->proj.screen_h;
        float fx = finger_screen_x(&ev->tfinger, sw);
        float fy = finger_screen_y(&ev->tfinger, sh);

        if (s->finger_count == 0) {
            /* First finger: check buttons, then start pan */
            bool proj_changed = false;
            if (check_buttons(app, fx, fy, &proj_changed)) {
                dirty = proj_changed;
                s->drag_on_button = true;
            } else {
                s->drag_on_button = false;
            }
            s->finger_id[0] = ev->tfinger.fingerID;
            s->finger_x[0]  = fx;
            s->finger_y[0]  = fy;
            s->finger_count = 1;
            s->dragging     = !s->drag_on_button;

        } else if (s->finger_count == 1) {
            /* Second finger: switch to pinch */
            s->finger_id[1] = ev->tfinger.fingerID;
            s->finger_x[1]  = fx;
            s->finger_y[1]  = fy;
            s->finger_count = 2;
            s->dragging     = false;
            s->pinch_last_dist = pinch_dist(s);
        }
        break;
    }

    /* ── Touch finger motion ──────────────────────────────────────── */
    case SDL_EVENT_FINGER_MOTION: {
        int sw = app->proj.screen_w, sh = app->proj.screen_h;
        int idx = find_finger(s, ev->tfinger.fingerID);
        if (idx < 0) break;

        float fx = finger_screen_x(&ev->tfinger, sw);
        float fy = finger_screen_y(&ev->tfinger, sh);

        if (s->finger_count == 1 && s->dragging) {
            float dx = fx - s->finger_x[idx];
            float dy = fy - s->finger_y[idx];
            projection_pan(&app->proj, dx, dy);
            dirty = true;
        } else if (s->finger_count == 2) {
            /* Update this finger's position */
            s->finger_x[idx] = fx;
            s->finger_y[idx] = fy;

            /* Pinch zoom */
            float new_dist = pinch_dist(s);
            if (s->pinch_last_dist > 1.0f) {
                float factor = new_dist / s->pinch_last_dist;
                if (factor > 0.1f && factor < 10.0f) {
                    projection_zoom(&app->proj, factor);
                    dirty = true;
                }
            }
            s->pinch_last_dist = new_dist;

            /* Centroid pan */
            float cx_old = (s->finger_x[0] + s->finger_x[1]) / 2.0f;
            float cy_old = (s->finger_y[0] + s->finger_y[1]) / 2.0f;
            /* finger positions updated above for idx; compute other finger centroid */
            int other = 1 - idx;
            float cx_new = (fx + s->finger_x[other]) / 2.0f;
            float cy_new = (fy + s->finger_y[other]) / 2.0f;
            float pdx = cx_new - cx_old;
            float pdy = cy_new - cy_old;
            if (fabsf(pdx) > 0.5f || fabsf(pdy) > 0.5f) {
                projection_pan(&app->proj, pdx, pdy);
                dirty = true;
            }
        }

        s->finger_x[idx] = fx;
        s->finger_y[idx] = fy;
        break;
    }

    /* ── Touch finger up ──────────────────────────────────────────── */
    case SDL_EVENT_FINGER_UP: {
        int idx = find_finger(s, ev->tfinger.fingerID);
        if (idx < 0) break;

        if (s->finger_count == 2) {
            /* Keep the other finger, resume single-touch pan */
            int keep = 1 - idx;
            s->finger_id[0] = s->finger_id[keep];
            s->finger_x[0]  = s->finger_x[keep];
            s->finger_y[0]  = s->finger_y[keep];
            s->finger_count = 1;
            s->dragging     = true;
            s->drag_on_button = false;
        } else {
            s->finger_count   = 0;
            s->dragging       = false;
            s->drag_on_button = false;
        }
        break;
    }

    default:
        break;
    }

    return dirty;
}
