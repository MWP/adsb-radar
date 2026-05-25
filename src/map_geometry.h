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
    char    **line_names;  /* heap-allocated name strings or NULL */
    /* point features (Point / MultiPoint) */
    double   *pt_lons;     /* raw geographic */
    double   *pt_lats;
    int       npts;
    int       npts_cap;
    float    *pt_xs;       /* screen-space (reprojected) */
    float    *pt_ys;
    char    **pt_names;    /* heap-allocated name strings or NULL */
} MapGeometry;

MapGeometry *map_geometry_new(void);
void         map_geometry_free(MapGeometry *g);

/* Re-project all raw lon/lat into screen coords using current projection. */
void map_geometry_reproject(MapGeometry *g, const ProjectionState *p);

/* Append a polyline described by arrays of lon/lat pairs.  name may be NULL. */
void map_geometry_add_line(MapGeometry *g,
                            const double *lons, const double *lats,
                            int npoints, const char *name);

/* Append a single point feature.  name may be NULL. */
void map_geometry_add_point(MapGeometry *g, double lon, double lat,
                             const char *name);
