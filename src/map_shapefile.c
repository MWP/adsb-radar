// adsb-radar — ESRI Shapefile (PolyLine/Polygon) loader → MapGeometry
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_shapefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

/* SHP shape types */
#define SHP_NULL     0
#define SHP_POINT    1
#define SHP_POLYLINE 3
#define SHP_POLYGON  5

static uint32_t swap32(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x000000FFu) << 24);
}

/* Read a little-endian double from a byte buffer */
static double read_le_double(const unsigned char *p)
{
    double v;
    memcpy(&v, p, 8);
    return v;
}

/* Read a little-endian uint32 from a byte buffer */
static uint32_t read_le_u32(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

MapGeometry *map_shapefile_load(const char *file_path)
{
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SHP: cannot open '%s'", file_path);
        return NULL;
    }

    /* Read 100-byte file header */
    unsigned char hdr[100];
    if (fread(hdr, 1, 100, f) != 100) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SHP: header too short");
        fclose(f); return NULL;
    }

    uint32_t file_code = swap32(read_le_u32(hdr));  /* actually big-endian */
    if (file_code != 9994) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SHP: invalid file code %u", file_code);
        fclose(f); return NULL;
    }

    /* File length in 16-bit words, big-endian at offset 24 */
    uint32_t file_len_words;
    memcpy(&file_len_words, hdr + 24, 4);
    file_len_words = swap32(file_len_words);

    MapGeometry *g = map_geometry_new();

    /* Read records until EOF */
    for (;;) {
        /* Record header: 8 bytes, big-endian */
        unsigned char rec_hdr[8];
        if (fread(rec_hdr, 1, 8, f) != 8) break;  /* EOF */

        uint32_t content_len_words;
        memcpy(&content_len_words, rec_hdr + 4, 4);
        content_len_words = swap32(content_len_words);
        long content_bytes = (long)content_len_words * 2;

        if (content_bytes < 4) {
            fseek(f, content_bytes, SEEK_CUR);
            continue;
        }

        /* Read content into heap buffer */
        unsigned char *rec = malloc(content_bytes);
        if (!rec) break;
        if (fread(rec, 1, content_bytes, f) != (size_t)content_bytes) {
            free(rec); break;
        }

        uint32_t shape_type = read_le_u32(rec);

        if (shape_type == SHP_NULL) {
            free(rec); continue;
        }

        if (shape_type != SHP_POLYLINE && shape_type != SHP_POLYGON) {
            free(rec); continue;
        }

        /* Offset 4: 32-byte bounding box (4 doubles) — skip */
        /* Offset 36: NumParts (4 bytes LE) */
        /* Offset 40: NumPoints (4 bytes LE) */
        if (content_bytes < 44) { free(rec); continue; }

        uint32_t num_parts  = read_le_u32(rec + 36);
        uint32_t num_points = read_le_u32(rec + 40);

        long parts_offset  = 44;
        long points_offset = 44 + (long)num_parts * 4;
        long expected      = points_offset + (long)num_points * 16;

        if (expected > content_bytes || num_parts == 0 || num_points == 0) {
            free(rec); continue;
        }

        /* Parts[] array: index of first point in each ring */
        uint32_t *parts = malloc(sizeof(uint32_t) * num_parts);
        for (uint32_t i = 0; i < num_parts; i++)
            parts[i] = read_le_u32(rec + parts_offset + i * 4);

        double *lons = malloc(sizeof(double) * num_points);
        double *lats = malloc(sizeof(double) * num_points);
        for (uint32_t i = 0; i < num_points; i++) {
            lons[i] = read_le_double(rec + points_offset + i * 16);      /* X = lon */
            lats[i] = read_le_double(rec + points_offset + i * 16 + 8);  /* Y = lat */
        }

        /* Emit each part as a separate polyline.
           For Polygon, only use the exterior ring (part 0). */
        uint32_t num_emit = (shape_type == SHP_POLYGON) ? 1 : num_parts;
        for (uint32_t p = 0; p < num_emit; p++) {
            uint32_t start = parts[p];
            uint32_t end   = (p + 1 < num_parts) ? parts[p + 1] : num_points;
            int len = (int)(end - start);
            if (len >= 2)
                map_geometry_add_line(g, lons + start, lats + start, len, NULL);
        }

        free(parts);
        free(lons);
        free(lats);
        free(rec);
    }

    fclose(f);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SHP: loaded %d polylines from '%s'", g->nlines, file_path);
    return g;
}
