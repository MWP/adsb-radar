// adsb-radar — TTF label cache: creates, caches, and draws per-aircraft callsign/altitude labels
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#include "render_labels.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

void label_cache_init(LabelCache *cache)
{
    memset(cache, 0, sizeof(*cache));
}

void label_cache_destroy(LabelCache *cache)
{
    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        if (cache->entries[i].text) {
            TTF_DestroyText(cache->entries[i].text);
            cache->entries[i].text = NULL;
        }
    }
}

static LabelEntry *find_or_alloc(LabelCache *cache, const char *hex)
{
    /* First pass: find existing entry by hex prefix of key */
    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        if (cache->entries[i].text &&
            strncmp(cache->entries[i].key, hex, 6) == 0)
            return &cache->entries[i];
    }
    /* Second pass: find empty slot */
    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        if (!cache->entries[i].text) {
            cache->entries[i].key[0] = '\0';
            return &cache->entries[i];
        }
    }
    return NULL;
}

void render_label(LabelCache *cache,
                  TTF_Font *font,
                  SDL_Renderer *renderer,
                  TTF_TextEngine *engine,
                  const Aircraft *ac,
                  float sx, float sy,
                  bool metric)
{
    char label[64];
    const char *name = ac->flight[0] ? ac->flight : ac->hex;
    char alt_str[16] = "----";
    char gs_str[16]  = "--";

    if (!isnan(ac->alt_baro) && ac->alt_baro > -9999.0f) {
        if (metric)
            snprintf(alt_str, sizeof(alt_str), "%dm", (int)(ac->alt_baro * 0.3048f));
        else
            snprintf(alt_str, sizeof(alt_str), "%dft", (int)ac->alt_baro);
    }
    if (ac->gs >= 0.0f) {
        if (metric)
            snprintf(gs_str, sizeof(gs_str), "%dkm/h", (int)(ac->gs * 1.852f));
        else
            snprintf(gs_str, sizeof(gs_str), "%dkt", (int)ac->gs);
    }
    snprintf(label, sizeof(label), "%s\n%s %s", name, alt_str, gs_str);

    /* Build cache key: "hex:label" */
    char key[32];
    snprintf(key, sizeof(key), "%.6s:%.24s", ac->hex, label);

    LabelEntry *entry = find_or_alloc(cache, ac->hex);
    if (!entry) return;

    /* Recreate TTF_Text only if content changed */
    if (!entry->text || strcmp(entry->key, key) != 0) {
        if (entry->text) {
            TTF_DestroyText(entry->text);
            entry->text = NULL;
        }
        entry->text = TTF_CreateText(engine, font, label, 0);
        if (!entry->text) return;
        strncpy(entry->key, key, sizeof(entry->key) - 1);
    }

    /* Apply fade alpha */
    Uint8 alpha = (Uint8)(ac->fade_alpha * 255.0f);
    TTF_SetTextColor(entry->text, 200, 200, 200, alpha);

    /* Draw at position (offset up from the aircraft dot) */
    TTF_DrawRendererText(entry->text, sx + 8.0f, sy - 8.0f);
}

void label_cache_prune(LabelCache *cache,
                        const Aircraft *entries, int count)
{
    for (int i = 0; i < AIRCRAFT_MAX; i++) {
        if (!cache->entries[i].text) continue;

        char hex[7];
        strncpy(hex, cache->entries[i].key, 6);
        hex[6] = '\0';

        bool found = false;
        for (int j = 0; j < count; j++) {
            if (entries[j].active &&
                strcmp(entries[j].hex, hex) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            TTF_DestroyText(cache->entries[i].text);
            cache->entries[i].text = NULL;
            cache->entries[i].key[0] = '\0';
        }
    }
}
