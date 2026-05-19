// adsb-radar — ProjectionState struct and geo↔screen projection function declarations
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>

typedef struct {
    double center_lat;   /* degrees */
    double center_lon;   /* degrees */
    double scale;        /* pixels per radian */
    double init_lat;     /* saved defaults for reset */
    double init_lon;
    double init_scale;
    int    screen_w;
    int    screen_h;
} ProjectionState;

/* Initialise from config values.  zoom_level is pixels per degree longitude. */
void projection_init(ProjectionState *p,
                     double center_lat, double center_lon,
                     float zoom_level,
                     int screen_w, int screen_h);

/* Forward: geographic degrees → screen pixel (float).
   Returns false if the point maps outside a generous clip region. */
bool geo_to_screen(const ProjectionState *p,
                   double lat_deg, double lon_deg,
                   float *sx, float *sy);

/* Inverse: screen pixel → geographic degrees. */
void screen_to_geo(const ProjectionState *p,
                   float sx, float sy,
                   double *lat_deg, double *lon_deg);

/* Pan by delta_x/delta_y screen pixels (updates center_lat/lon). */
void projection_pan(ProjectionState *p, float dx, float dy);

/* Zoom in (factor > 1) or out (factor < 1) keeping screen centre fixed. */
void projection_zoom(ProjectionState *p, float factor);

/* Reset to initial values. */
void projection_reset(ProjectionState *p);
