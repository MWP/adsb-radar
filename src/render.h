// adsb-radar — render_frame declaration, button geometry constants, and shared draw helpers
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>

/* Button geometry — shared with input and gps modules */
#define BTN_SZ     50.0f
#define BTN_GAP     8.0f
#define BTN_MARGIN 25.0f

struct AppState;

void render_frame(struct AppState *app);
void draw_filled_circle(SDL_Renderer *renderer, float cx, float cy, float r);
void draw_circle_approx(SDL_Renderer *renderer,
                        float cx, float cy, float r, int segments);

void render_zoom_button_rects(int screen_w, int screen_h,
                               SDL_FRect *btn_in, SDL_FRect *btn_out);
void render_pan_button_rects(int screen_w, int screen_h, SDL_FRect out[4]);
void render_home_button_rect(int screen_w, int screen_h, SDL_FRect *out);
void render_gps_lock_button_rect(int screen_w, int screen_h, SDL_FRect *out);
void render_list_toggle_rect(int screen_w, SDL_FRect *out);
void render_exit_button_rect(int screen_w, int screen_h, SDL_FRect *out);
