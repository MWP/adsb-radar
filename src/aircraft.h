// adsb-radar — Aircraft, AircraftList, and TrailBuffer type definitions
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>

#define AIRCRAFT_MAX      32 
#define MAX_TRAIL_POINTS  1024

typedef struct {
    double lat, lon;
    float  alt_baro;    /* feet, NaN if unknown */
    Uint64 ts_ms;       /* SDL_GetTicks() when recorded */
} TrailPoint;

typedef struct {
    TrailPoint pts[MAX_TRAIL_POINTS];
    int        head;    /* next write slot (circular) */
    int        count;   /* valid points, 0..MAX_TRAIL_POINTS */
} TrailBuffer;

typedef struct {
    char    hex[7];           /* ICAO 24-bit address as 6 hex chars + NUL */
    char    flight[9];        /* callsign, 8 chars + NUL */
    double  lat;
    double  lon;
    float   alt_baro;         /* feet; NaN if unknown */
    float   gs;               /* knots; NaN if unknown */
    float   track;            /* 0–359°; NaN if unknown */
    float   rssi;             /* dBFS; NaN if unknown */
    float   seen;             /* seconds since last message */
    float   seen_pos;         /* seconds since last position */
    Uint64  last_updated_ms;  /* SDL_GetTicks() when last written */
    float   fade_alpha;       /* 1.0 = full, 0.0 = expired */
    bool    has_position;
    bool    active;
} Aircraft;

typedef struct {
    Aircraft     entries[AIRCRAFT_MAX];
    TrailBuffer *trails;       /* heap-allocated [AIRCRAFT_MAX], parallel to entries */
    int          count;
    int          total_seen;   /* lifetime count of unique ICAO addresses */
    SDL_Mutex   *mutex;
} AircraftList;

void aircraft_list_init(AircraftList *list);
void aircraft_list_destroy(AircraftList *list);

/* Insert or update an aircraft by hex.  Must be called with list->mutex held. */
void aircraft_list_upsert(AircraftList *list, const Aircraft *src);

/* Update fade_alpha and expire old entries.  Acquires the mutex internally. */
void aircraft_list_update_ages(AircraftList *list,
                                float max_age_sec,
                                float fade_start_sec);
