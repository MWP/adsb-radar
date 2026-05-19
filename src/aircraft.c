// adsb-radar — aircraft list management: upsert, age/fade updates, trail buffer
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "aircraft.h"
#include <string.h>
#include <stdlib.h>

static void trail_add(TrailBuffer *tb, double lat, double lon,
                       float alt_baro, Uint64 ts_ms)
{
    tb->pts[tb->head] = (TrailPoint){ lat, lon, alt_baro, ts_ms };
    tb->head = (tb->head + 1) % MAX_TRAIL_POINTS;
    if (tb->count < MAX_TRAIL_POINTS) tb->count++;
}

void aircraft_list_init(AircraftList *list)
{
    memset(list, 0, sizeof(*list));
    list->trails = calloc(AIRCRAFT_MAX, sizeof(TrailBuffer));
    list->mutex  = SDL_CreateMutex();
}

void aircraft_list_destroy(AircraftList *list)
{
    if (list->mutex) {
        SDL_DestroyMutex(list->mutex);
        list->mutex = NULL;
    }
    free(list->trails);
    list->trails = NULL;
}

void aircraft_list_upsert(AircraftList *list, const Aircraft *src)
{
    /* Caller must hold list->mutex */
    int free_slot = -1;

    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        Aircraft *ac = &list->entries[i];
        if (!ac->active) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (strcmp(ac->hex, src->hex) == 0) {
            *ac = *src;
            ac->active = true;
            if (src->has_position && list->trails)
                trail_add(&list->trails[i],
                          src->lat, src->lon, src->alt_baro, src->last_updated_ms);
            return;
        }
    }

    if (free_slot >= 0) {
        list->entries[free_slot] = *src;
        list->entries[free_slot].active = true;
        list->count++;
        list->total_seen++;
        if (list->trails) {
            memset(&list->trails[free_slot], 0, sizeof(TrailBuffer));
            if (src->has_position)
                trail_add(&list->trails[free_slot],
                          src->lat, src->lon, src->alt_baro, src->last_updated_ms);
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "aircraft_list full — dropping %s", src->hex);
    }
}

void aircraft_list_update_ages(AircraftList *list,
                                float max_age_sec,
                                float fade_start_sec)
{
    Uint64 now_ms = SDL_GetTicks();
    float  fade_window = max_age_sec - fade_start_sec;
    if (fade_window <= 0.0f) fade_window = 1.0f;

    SDL_LockMutex(list->mutex);
    int count = 0;
    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        Aircraft *ac = &list->entries[i];
        if (!ac->active) continue;

        float age = (float)(now_ms - ac->last_updated_ms) / 1000.0f;

        if (age >= max_age_sec) {
            ac->active = false;
            if (list->trails)
                memset(&list->trails[i], 0, sizeof(TrailBuffer));
            continue;
        }
        if (age >= fade_start_sec) {
            ac->fade_alpha = 1.0f - (age - fade_start_sec) / fade_window;
        } else {
            ac->fade_alpha = 1.0f;
        }
        count++;
    }
    list->count = count;
    SDL_UnlockMutex(list->mutex);
}
