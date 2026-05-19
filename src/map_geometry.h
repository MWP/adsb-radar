// adsb-radar — MapGeometry and Polyline structs shared by all map loaders
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "projection.h"

typedef struct {
    float *xs;       /* screen-space x coords (projected) */
    float *ys;       /* screen-space y coords (projected) */
    int    npoints;
} Polyline;

typedef struct {
    Polyline *lines;
    int       nlines;
    int       nlines_cap;
    /* raw geographic coords for reprojection on zoom/pan */
    double   *raw_lons;
    double   *raw_lats;
    int       raw_count;
    int       raw_cap;
    /* per-line index into raw arrays */
    int      *line_start;
    int      *line_len;
} MapGeometry;

MapGeometry *map_geometry_new(void);
void         map_geometry_free(MapGeometry *g);

/* Re-project all raw lon/lat into screen coords using current projection. */
void map_geometry_reproject(MapGeometry *g, const ProjectionState *p);

/* Append a polyline described by arrays of lon/lat pairs. */
void map_geometry_add_line(MapGeometry *g,
                            const double *lons, const double *lats,
                            int npoints);
