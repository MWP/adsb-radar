// adsb-radar — MapGeometry allocation, raw coordinate storage, and screen reprojection
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_geometry.h"
#include <stdlib.h>
#include <string.h>

MapGeometry *map_geometry_new(void)
{
    MapGeometry *g = calloc(1, sizeof(MapGeometry));
    return g;
}

void map_geometry_free(MapGeometry *g)
{
    if (!g) return;
    for (int i = 0; i < g->nlines; i++) {
        free(g->lines[i].xs);
        free(g->lines[i].ys);
    }
    free(g->lines);
    free(g->raw_lons);
    free(g->raw_lats);
    free(g->line_start);
    free(g->line_len);
    free(g);
}

void map_geometry_add_line(MapGeometry *g,
                            const double *lons, const double *lats,
                            int npoints)
{
    if (npoints < 2) return;

    /* Grow lines array */
    if (g->nlines >= g->nlines_cap) {
        int new_cap = g->nlines_cap ? g->nlines_cap * 2 : 256;
        g->lines      = realloc(g->lines,      sizeof(Polyline) * new_cap);
        g->line_start = realloc(g->line_start, sizeof(int)      * new_cap);
        g->line_len   = realloc(g->line_len,   sizeof(int)      * new_cap);
        g->nlines_cap = new_cap;
    }

    /* Grow raw coord arrays */
    int needed = g->raw_count + npoints;
    if (needed > g->raw_cap) {
        int new_cap = g->raw_cap ? g->raw_cap * 2 : 4096;
        while (new_cap < needed) new_cap *= 2;
        g->raw_lons = realloc(g->raw_lons, sizeof(double) * new_cap);
        g->raw_lats = realloc(g->raw_lats, sizeof(double) * new_cap);
        g->raw_cap  = new_cap;
    }

    /* Record line metadata */
    int idx = g->nlines;
    g->line_start[idx] = g->raw_count;
    g->line_len[idx]   = npoints;

    /* Allocate screen-space arrays (populated later by reproject) */
    g->lines[idx].xs      = malloc(sizeof(float) * npoints);
    g->lines[idx].ys      = malloc(sizeof(float) * npoints);
    g->lines[idx].npoints = npoints;

    /* Copy raw coords */
    memcpy(g->raw_lons + g->raw_count, lons, sizeof(double) * npoints);
    memcpy(g->raw_lats + g->raw_count, lats, sizeof(double) * npoints);
    g->raw_count += npoints;
    g->nlines++;
}

void map_geometry_reproject(MapGeometry *g, const ProjectionState *p)
{
    for (int i = 0; i < g->nlines; i++) {
        int start = g->line_start[i];
        int len   = g->line_len[i];
        Polyline *pl = &g->lines[i];
        for (int j = 0; j < len; j++) {
            float sx, sy;
            if (!geo_to_screen(p, g->raw_lats[start + j],
                                   g->raw_lons[start + j], &sx, &sy)) {
                /* Out of range — set to a sentinel outside the screen */
                sx = -99999.0f;
                sy = -99999.0f;
            }
            pl->xs[j] = sx;
            pl->ys[j] = sy;
        }
    }
}
