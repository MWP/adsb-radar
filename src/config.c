// adsb-radar — INI config file parser; populates Config struct from adsb-radar.ini
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static MapSourceType detect_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return MAP_NONE;
    if (strcmp(dot, ".geojson") == 0 || strcmp(dot, ".json") == 0)
        return MAP_GEOJSON;
    if (strcmp(dot, ".shp") == 0)
        return MAP_SHAPEFILE;
    return MAP_NONE;
}

void config_defaults(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->dump1090_host, "127.0.0.1", sizeof(cfg->dump1090_host) - 1);
    cfg->dump1090_port    = 8080;
    cfg->poll_interval_ms = 1000;

    cfg->screen_width  = 0;
    cfg->screen_height = 0;
    cfg->fullscreen    = true;
    cfg->kmsdrm_device_index = 0;
    cfg->show_buttons        = true;
    cfg->show_exit_button    = false;

    cfg->map_layer_count = 0;

    cfg->center_lat = 0.0;
    cfg->center_lon = 0.0;
    cfg->zoom_level = 800.0f;

    cfg->max_age_seconds     = 30.0f;
    cfg->fade_start_seconds  = 20.0f;
    cfg->inactive_seconds    = 10.0f;
    cfg->trail_seconds       = 0.0f;
    cfg->aircraft_dot_radius = 5.0f;
    cfg->heading_line_length = 20.0f;

    strncpy(cfg->font_path, "assets/fonts/DejaVuSans.ttf",
            sizeof(cfg->font_path) - 1);
    cfg->font_size_pt      = 12.0f;
    cfg->list_font_path[0] = '\0';
    cfg->list_font_size_pt = 0.0f;

    cfg->col_background[0] = 10;  cfg->col_background[1] = 10;
    cfg->col_background[2] = 20;  cfg->col_background[3] = 255;
    cfg->col_map[0] = 60;  cfg->col_map[1] = 80;
    cfg->col_map[2] = 60;  cfg->col_map[3] = 255;
    cfg->col_range_circle[0] = 40;  cfg->col_range_circle[1] = 80;
    cfg->col_range_circle[2] = 40;  cfg->col_range_circle[3] = 180;
    cfg->col_aircraft[0] = 0;   cfg->col_aircraft[1] = 220;
    cfg->col_aircraft[2] = 0;   cfg->col_aircraft[3] = 255;
    cfg->col_heading[0] = 0;   cfg->col_heading[1] = 200;
    cfg->col_heading[2] = 200; cfg->col_heading[3] = 255;
    cfg->col_label[0] = 200; cfg->col_label[1] = 200;
    cfg->col_label[2] = 200; cfg->col_label[3] = 220;

    cfg->max_range_circle       = false;
    cfg->max_range_fill         = false;
    cfg->col_max_range[0]       = 255; cfg->col_max_range[1]      = 100;
    cfg->col_max_range[2]       = 0;   cfg->col_max_range[3]      = 180;
    cfg->col_max_range_fill[0]  = 255; cfg->col_max_range_fill[1] = 100;
    cfg->col_max_range_fill[2]  = 0;   cfg->col_max_range_fill[3] = 30;
    cfg->col_inactive[0] = 130; cfg->col_inactive[1] = 130;
    cfg->col_inactive[2] = 130; cfg->col_inactive[3] = 255;

    cfg->gps_enabled       = false;
    strncpy(cfg->gps_port, "/dev/ttyACM0", sizeof(cfg->gps_port) - 1);
    cfg->gps_baud          = 9600;
    cfg->gps_lock_on_start = false;
    cfg->col_gps_marker[0] = 255; cfg->col_gps_marker[1] = 220;
    cfg->col_gps_marker[2] = 0;   cfg->col_gps_marker[3] = 255;
}

static void trim(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static bool parse_color(const char *val, Uint8 out[4])
{
    unsigned r = 0, g = 0, b = 0, a = 255;
    int n = sscanf(val, "%u,%u,%u,%u", &r, &g, &b, &a);
    if (n < 3) return false;
    out[0] = (Uint8)r; out[1] = (Uint8)g;
    out[2] = (Uint8)b; out[3] = (Uint8)a;
    return true;
}

bool config_load(Config *cfg, const char *path)
{
    config_defaults(cfg);

    FILE *f = NULL;
    if (path) f = fopen(path, "r");
    if (!f)   f = fopen("adsb-radar.ini", "r");
    if (!f) {
        const char *home = getenv("HOME");
        if (home) {
            char buf[1024];
            snprintf(buf, sizeof(buf),
                     "%s/.config/adsb-radar/adsb-radar.ini", home);
            f = fopen(buf, "r");
        }
    }
    if (!f) return true;

    char line[1024];
    char section[64] = "";

    /* Backward-compat temporaries for old source/file keys */
    char   legacy_file[512] = "";
    MapSourceType legacy_type = MAP_NONE;
    bool   have_layer_keys = false;

    while (fgets(line, sizeof(line), f)) {
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        trim(line);
        if (!line[0]) continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section) - 1);
                trim(section);
            }
            continue;
        }

        char key[256] = "", val[512] = "";
        if (sscanf(line, " %255[^=] = %511[^\n]", key, val) < 2) continue;
        trim(key); trim(val);

#define MATCH(s, k) (strcmp(section, s) == 0 && strcmp(key, k) == 0)
#define SECT(s)      (strcmp(section, s) == 0)

        if      (MATCH("network", "host"))
            strncpy(cfg->dump1090_host, val, sizeof(cfg->dump1090_host) - 1);
        else if (MATCH("network", "port"))
            cfg->dump1090_port = atoi(val);
        else if (MATCH("network", "poll_interval_ms"))
            cfg->poll_interval_ms = atoi(val);
        else if (MATCH("network", "local_json"))
            strncpy(cfg->aircraft_json_path, val, sizeof(cfg->aircraft_json_path) - 1);

        else if (MATCH("display", "width"))
            cfg->screen_width = atoi(val);
        else if (MATCH("display", "height"))
            cfg->screen_height = atoi(val);
        else if (MATCH("display", "fullscreen"))
            cfg->fullscreen = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (MATCH("display", "kmsdrm_device"))
            cfg->kmsdrm_device_index = atoi(val);
        else if (MATCH("display", "video_driver"))
            strncpy(cfg->video_driver, val, sizeof(cfg->video_driver) - 1);
        else if (MATCH("display", "show_buttons"))
            cfg->show_buttons = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (MATCH("display", "exit_button"))
            cfg->show_exit_button = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);

        /* ── Map layers (new format) ──────────────────────────────── */
        else if (SECT("map")) {
            int idx = -1;
            char suffix[64];

            if (sscanf(key, "layer_%d_%63s", &idx, suffix) == 2 &&
                idx >= 0 && idx < MAX_MAP_LAYERS &&
                strcmp(suffix, "color") == 0) {
                parse_color(val, cfg->map_layers[idx].color);
                cfg->map_layers[idx].has_color = true;
                if (idx >= cfg->map_layer_count)
                    cfg->map_layer_count = idx + 1;

            } else if (sscanf(key, "layer_%d", &idx) == 1 &&
                       idx >= 0 && idx < MAX_MAP_LAYERS) {
                strncpy(cfg->map_layers[idx].path, val,
                        sizeof(cfg->map_layers[idx].path) - 1);
                cfg->map_layers[idx].type = detect_type(val);
                have_layer_keys = true;
                if (idx >= cfg->map_layer_count)
                    cfg->map_layer_count = idx + 1;

            /* Backward-compat: old source / file keys */
            } else if (strcmp(key, "source") == 0) {
                if      (strcmp(val, "geojson")   == 0) legacy_type = MAP_GEOJSON;
                else if (strcmp(val, "shapefile") == 0) legacy_type = MAP_SHAPEFILE;
            } else if (strcmp(key, "file") == 0) {
                strncpy(legacy_file, val, sizeof(legacy_file) - 1);
            }
        }

        else if (MATCH("view", "center_lat"))
            cfg->center_lat = atof(val);
        else if (MATCH("view", "center_lon"))
            cfg->center_lon = atof(val);
        else if (MATCH("view", "zoom_level"))
            cfg->zoom_level = (float)atof(val);

        else if (MATCH("aircraft", "max_age_seconds"))
            cfg->max_age_seconds = (float)atof(val);
        else if (MATCH("aircraft", "fade_start_seconds"))
            cfg->fade_start_seconds = (float)atof(val);
        else if (MATCH("aircraft", "inactive_seconds"))
            cfg->inactive_seconds = (float)atof(val);
        else if (MATCH("aircraft", "trail_seconds"))
            cfg->trail_seconds = (float)atof(val);
        else if (MATCH("aircraft", "dot_radius"))
            cfg->aircraft_dot_radius = (float)atof(val);
        else if (MATCH("aircraft", "heading_line_length"))
            cfg->heading_line_length = (float)atof(val);

        else if (MATCH("font", "path"))
            strncpy(cfg->font_path, val, sizeof(cfg->font_path) - 1);
        else if (MATCH("font", "size_pt"))
            cfg->font_size_pt = (float)atof(val);
        else if (MATCH("font", "list_path"))
            strncpy(cfg->list_font_path, val, sizeof(cfg->list_font_path) - 1);
        else if (MATCH("font", "list_size_pt"))
            cfg->list_font_size_pt = (float)atof(val);

        else if (MATCH("units", "system"))
            cfg->units_metric = (strcmp(val, "metric") == 0);

        else if (MATCH("display", "max_range_circle"))
            cfg->max_range_circle = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (MATCH("display", "max_range_fill"))
            cfg->max_range_fill = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);

        else if (MATCH("gps", "enabled"))
            cfg->gps_enabled = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (MATCH("gps", "port"))
            strncpy(cfg->gps_port, val, sizeof(cfg->gps_port) - 1);
        else if (MATCH("gps", "baud"))
            cfg->gps_baud = atoi(val);
        else if (MATCH("gps", "lock_on_start"))
            cfg->gps_lock_on_start = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);

        else if (MATCH("colours", "background"))   parse_color(val, cfg->col_background);
        else if (MATCH("colours", "map"))          parse_color(val, cfg->col_map);
        else if (MATCH("colours", "range_circle")) parse_color(val, cfg->col_range_circle);
        else if (MATCH("colours", "aircraft"))     parse_color(val, cfg->col_aircraft);
        else if (MATCH("colours", "heading"))      parse_color(val, cfg->col_heading);
        else if (MATCH("colours", "label"))        parse_color(val, cfg->col_label);
        else if (MATCH("colours", "gps_marker"))   parse_color(val, cfg->col_gps_marker);
        else if (MATCH("colours", "max_range"))      parse_color(val, cfg->col_max_range);
        else if (MATCH("colours", "max_range_fill")) parse_color(val, cfg->col_max_range_fill);
        else if (MATCH("colours", "inactive"))       parse_color(val, cfg->col_inactive);

#undef MATCH
#undef SECT
    }

    fclose(f);

    /* Backward compat: if old source/file keys were used and no layer_N
       keys were present, promote them to layer_0. */
    if (!have_layer_keys && legacy_file[0] && legacy_type != MAP_NONE) {
        cfg->map_layers[0].type = legacy_type;
        strncpy(cfg->map_layers[0].path, legacy_file,
                sizeof(cfg->map_layers[0].path) - 1);
        cfg->map_layer_count = 1;
    }

    return true;
}
