// adsb-radar — map_geometry_load_geojson declaration
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "map_geometry.h"

/* Load a GeoJSON FeatureCollection from file_path into a new MapGeometry.
   Returns NULL on error.  Caller must map_geometry_free() the result. */
MapGeometry *map_geojson_load(const char *file_path);
