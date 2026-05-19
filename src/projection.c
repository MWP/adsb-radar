// adsb-radar — Web Mercator projection: geo↔screen coordinate conversion, zoom and pan
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "projection.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void projection_init(ProjectionState *p,
                     double center_lat, double center_lon,
                     float zoom_level,
                     int screen_w, int screen_h)
{
    p->screen_w   = screen_w;
    p->screen_h   = screen_h;
    p->center_lat = center_lat;
    p->center_lon = center_lon;
    /* scale = pixels per radian of longitude: zoom_level [px/deg] × (deg/rad) */
    p->scale = (double)zoom_level * 180.0 / M_PI;
    p->init_lat   = center_lat;
    p->init_lon   = center_lon;
    p->init_scale = p->scale;
}

bool geo_to_screen(const ProjectionState *p,
                   double lat_deg, double lon_deg,
                   float *sx, float *sy)
{
    double lat_rad = lat_deg * M_PI / 180.0;
    double lon_rad = lon_deg * M_PI / 180.0;
    double cx_rad  = p->center_lon * M_PI / 180.0;
    double cy_rad  = p->center_lat * M_PI / 180.0;

    /* Guard against poles / extreme latitudes that blow up tan */
    if (lat_rad >  1.484 || lat_rad < -1.484) return false;  /* ~85° */
    if (cy_rad  >  1.484 || cy_rad  < -1.484) return false;

    double mx = lon_rad - cx_rad;
    double my = log(tan(M_PI / 4.0 + lat_rad / 2.0))
              - log(tan(M_PI / 4.0 + cy_rad  / 2.0));

    *sx = (float)(p->screen_w / 2.0 + mx * p->scale);
    *sy = (float)(p->screen_h / 2.0 - my * p->scale);

    /* Cull anything far outside the screen */
    float margin = (float)(p->screen_w > p->screen_h ? p->screen_w : p->screen_h) * 2.0f;
    if (*sx < -margin || *sx > p->screen_w + margin) return false;
    if (*sy < -margin || *sy > p->screen_h + margin) return false;

    return true;
}

void screen_to_geo(const ProjectionState *p,
                   float sx, float sy,
                   double *lat_deg, double *lon_deg)
{
    double cx_rad = p->center_lon * M_PI / 180.0;
    double cy_rad = p->center_lat * M_PI / 180.0;

    double mx = (sx - p->screen_w / 2.0) / p->scale;
    double my = (p->screen_h / 2.0 - sy) / p->scale;

    *lon_deg = (cx_rad + mx) * 180.0 / M_PI;
    double merc_y0 = log(tan(M_PI / 4.0 + cy_rad / 2.0));
    *lat_deg = (2.0 * atan(exp(merc_y0 + my)) - M_PI / 2.0) * 180.0 / M_PI;
}

void projection_pan(ProjectionState *p, float dx, float dy)
{
    double new_lon, new_lat;
    /* Find where screen centre ± delta maps to */
    screen_to_geo(p,
                  (float)(p->screen_w / 2) - dx,
                  (float)(p->screen_h / 2) - dy,
                  &new_lat, &new_lon);
    p->center_lat = new_lat;
    p->center_lon = new_lon;
}

void projection_zoom(ProjectionState *p, float factor)
{
    p->scale *= (double)factor;
}

void projection_reset(ProjectionState *p)
{
    p->center_lat = p->init_lat;
    p->center_lon = p->init_lon;
    p->scale      = p->init_scale;
}
