// adsb-radar — Config struct definition and config_load/config_defaults declarations
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>

typedef enum {
    MAP_NONE = 0,
    MAP_GEOJSON,
    MAP_SHAPEFILE,
} MapSourceType;

#define MAX_MAP_LAYERS 16

typedef struct {
    MapSourceType type;
    char          path[512];
    bool          has_color;
    Uint8         color[4];   /* overrides col_map when has_color is true */
} MapLayerConfig;

typedef struct {
    /* Network */
    char  dump1090_host[256];
    int   dump1090_port;
    int   poll_interval_ms;
    char  aircraft_json_path[512]; /* non-empty: read local file instead of HTTP */

    /* Display */
    int   screen_width;
    int   screen_height;
    bool  fullscreen;
    int   kmsdrm_device_index;
    char  video_driver[32];   /* empty = SDL auto-detect; "kmsdrm", "x11", etc. */
    bool  show_buttons;       /* show on-screen zoom/pan buttons */
    bool  show_exit_button;   /* show on-screen exit button (bottom-left) */

    /* Map layers */
    MapLayerConfig map_layers[MAX_MAP_LAYERS];
    int            map_layer_count;

    /* View */
    double center_lat;
    double center_lon;
    float  zoom_level;       /* pixels per degree longitude */

    /* Aircraft */
    float  max_age_seconds;
    float  fade_start_seconds;
    float  inactive_seconds;      /* seconds without update before dot goes grey */
    float  trail_seconds;         /* 0 = disabled */
    float  aircraft_dot_radius;
    float  heading_line_length;

    /* Labels */
    char  font_path[512];
    float font_size_pt;
    char  list_font_path[512]; /* empty = use font_path */
    float list_font_size_pt;   /* 0 = use font_size_pt */

    /* Units */
    bool  units_metric;      /* false = imperial (nm/kt/ft), true = metric (km/kmh/m) */

    /* Max range circle */
    bool  max_range_circle;      /* show circle at furthest aircraft seen */
    bool  max_range_fill;        /* fill the max range circle */

    /* GPS */
    bool  gps_enabled;
    char  gps_port[64];
    int   gps_baud;
    bool  gps_lock_on_start;

    /* Colours (RGBA) */
    Uint8 col_background[4];
    Uint8 col_map[4];
    Uint8 col_range_circle[4];
    Uint8 col_aircraft[4];
    Uint8 col_heading[4];
    Uint8 col_label[4];
    Uint8 col_gps_marker[4];
    Uint8 col_max_range[4];
    Uint8 col_max_range_fill[4];
    Uint8 col_inactive[4];        /* dot/trail/heading colour for stale aircraft */
} Config;

/* Populate cfg with built-in defaults, then overlay from file.
   Returns true on success; file not found is not an error (uses defaults). */
bool config_load(Config *cfg, const char *path);

/* Fill cfg with compiled-in defaults. */
void config_defaults(Config *cfg);
