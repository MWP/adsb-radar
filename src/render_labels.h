// adsb-radar — LabelCache struct and label draw declarations
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "aircraft.h"

/* Per-aircraft label cache entry */
typedef struct {
    char       key[32];       /* "hex:flight:alt:gs" used to detect changes */
    TTF_Text  *text;
} LabelEntry;

typedef struct {
    LabelEntry entries[AIRCRAFT_MAX];
    int        count;
} LabelCache;

void label_cache_init(LabelCache *cache);
void label_cache_destroy(LabelCache *cache);

/* Draw label for aircraft at screen position (sx, sy).
   Creates/updates a cached TTF_Text object as needed. */
void render_label(LabelCache *cache,
                  TTF_Font *font,
                  SDL_Renderer *renderer,
                  TTF_TextEngine *engine,
                  const Aircraft *ac,
                  float sx, float sy,
                  bool metric);

/* Remove cache entries for aircraft that are no longer active. */
void label_cache_prune(LabelCache *cache,
                        const Aircraft *entries, int count);
