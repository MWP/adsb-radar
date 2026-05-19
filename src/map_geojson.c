// adsb-radar — GeoJSON FeatureCollection loader → MapGeometry
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_geojson.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    return sz;
}

static void add_linestring(MapGeometry *g, cJSON *coords)
{
    int n = cJSON_GetArraySize(coords);
    if (n < 2) return;

    double *lons = malloc(sizeof(double) * n);
    double *lats = malloc(sizeof(double) * n);
    int count = 0;

    cJSON *pt;
    cJSON_ArrayForEach(pt, coords) {
        cJSON *lon_j = cJSON_GetArrayItem(pt, 0);
        cJSON *lat_j = cJSON_GetArrayItem(pt, 1);
        if (!lon_j || !lat_j) continue;
        lons[count] = lon_j->valuedouble;
        lats[count] = lat_j->valuedouble;
        count++;
    }
    if (count >= 2)
        map_geometry_add_line(g, lons, lats, count);

    free(lons);
    free(lats);
}

static void process_geometry(MapGeometry *g, cJSON *geom)
{
    if (!geom) return;
    cJSON *type_j   = cJSON_GetObjectItemCaseSensitive(geom, "type");
    cJSON *coords_j = cJSON_GetObjectItemCaseSensitive(geom, "coordinates");
    if (!type_j || !cJSON_IsString(type_j) || !coords_j) return;

    const char *type = type_j->valuestring;

    if (strcmp(type, "LineString") == 0) {
        add_linestring(g, coords_j);

    } else if (strcmp(type, "MultiLineString") == 0) {
        cJSON *ring;
        cJSON_ArrayForEach(ring, coords_j)
            add_linestring(g, ring);

    } else if (strcmp(type, "Polygon") == 0) {
        /* exterior ring is index 0 */
        cJSON *exterior = cJSON_GetArrayItem(coords_j, 0);
        if (exterior) add_linestring(g, exterior);

    } else if (strcmp(type, "MultiPolygon") == 0) {
        cJSON *polygon;
        cJSON_ArrayForEach(polygon, coords_j) {
            cJSON *exterior = cJSON_GetArrayItem(polygon, 0);
            if (exterior) add_linestring(g, exterior);
        }
    } else {
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                       "GeoJSON: skipping unsupported geometry type '%s'", type);
    }
}

MapGeometry *map_geojson_load(const char *file_path)
{
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GeoJSON: cannot open '%s'", file_path);
        return NULL;
    }

    long sz = file_size(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, sz, f) != sz) {
        free(buf); fclose(f); return NULL;
    }
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GeoJSON: JSON parse error in '%s'", file_path);
        return NULL;
    }

    MapGeometry *g = map_geometry_new();

    cJSON *type_j = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (type_j && cJSON_IsString(type_j) &&
        strcmp(type_j->valuestring, "FeatureCollection") == 0) {

        cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
        cJSON *feature;
        cJSON_ArrayForEach(feature, features) {
            cJSON *geom = cJSON_GetObjectItemCaseSensitive(feature, "geometry");
            process_geometry(g, geom);
        }
    } else {
        /* Might be a bare geometry object */
        process_geometry(g, root);
    }

    cJSON_Delete(root);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "GeoJSON: loaded %d polylines from '%s'", g->nlines, file_path);
    return g;
}
