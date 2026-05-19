// adsb-radar — map_geometry_load_shapefile declaration
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "map_geometry.h"

/* Load ESRI Shapefile (PolyLine type 3 or Polygon type 5) from file_path.
   Returns NULL on error.  Caller must map_geometry_free() the result. */
MapGeometry *map_shapefile_load(const char *file_path);
