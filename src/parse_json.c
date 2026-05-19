// adsb-radar — parses dump1090-fa aircraft.json response bodies into AircraftList
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "parse_json.h"
#include <cJSON.h>
#include <string.h>
#include <math.h>
#include <SDL3/SDL.h>

static float get_float(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsNumber(item))
        return (float)item->valuedouble;
    return (float)NAN;
}

static double get_double(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item && cJSON_IsNumber(item))
        return item->valuedouble;
    return NAN;
}

void parse_aircraft_json(const char *json_body, AircraftList *list)
{
    cJSON *root = cJSON_Parse(json_body);
    if (!root) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "JSON parse failed");
        return;
    }

    cJSON *aircraft_arr = cJSON_GetObjectItemCaseSensitive(root, "aircraft");
    if (!cJSON_IsArray(aircraft_arr)) {
        cJSON_Delete(root);
        return;
    }

    /* Parse into a local buffer to minimise time spent under the mutex */
    Aircraft local[AIRCRAFT_MAX];
    int n = 0;

    cJSON *ac_j;
    cJSON_ArrayForEach(ac_j, aircraft_arr) {
        if (n >= AIRCRAFT_MAX) break;

        cJSON *hex_j = cJSON_GetObjectItemCaseSensitive(ac_j, "hex");
        if (!hex_j || !cJSON_IsString(hex_j)) continue;

        Aircraft ac;
        memset(&ac, 0, sizeof(ac));

        strncpy(ac.hex, hex_j->valuestring, sizeof(ac.hex) - 1);

        cJSON *flight_j = cJSON_GetObjectItemCaseSensitive(ac_j, "flight");
        if (flight_j && cJSON_IsString(flight_j)) {
            strncpy(ac.flight, flight_j->valuestring, sizeof(ac.flight) - 1);
            /* strip trailing spaces */
            int len = (int)strlen(ac.flight);
            while (len > 0 && ac.flight[len - 1] == ' ')
                ac.flight[--len] = '\0';
        }

        double lat = get_double(ac_j, "lat");
        double lon = get_double(ac_j, "lon");
        if (!isnan(lat) && !isnan(lon)) {
            ac.lat = lat;
            ac.lon = lon;
            ac.has_position = true;
        } else {
            ac.lat = NAN;
            ac.lon = NAN;
        }

        ac.alt_baro  = get_float(ac_j, "alt_baro");
        ac.gs        = get_float(ac_j, "gs");
        ac.track     = get_float(ac_j, "track");
        ac.rssi      = get_float(ac_j, "rssi");
        ac.seen      = get_float(ac_j, "seen");
        ac.seen_pos  = get_float(ac_j, "seen_pos");

        /* Reject physically impossible values — these are corrupted messages */
        if (!isnan(ac.alt_baro) &&
            (ac.alt_baro < -2000.0f || ac.alt_baro > 70000.0f))
            ac.alt_baro = NAN;
        if (!isnan(ac.gs) && (ac.gs < 0.0f || ac.gs > 800.0f))
            ac.gs = NAN;
        if (!isnan(ac.track) && (ac.track < 0.0f || ac.track >= 360.0f))
            ac.track = NAN;

        ac.last_updated_ms = SDL_GetTicks();
        ac.fade_alpha = 1.0f;
        ac.active = true;

        local[n++] = ac;
    }

    cJSON_Delete(root);

    SDL_LockMutex(list->mutex);
    for (int i = 0; i < n; i++)
        aircraft_list_upsert(list, &local[i]);
    SDL_UnlockMutex(list->mutex);
}
