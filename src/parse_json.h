// adsb-radar — parse_aircraft_json declaration
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "aircraft.h"

/* Parse a dump1090-fa aircraft.json body and upsert into list.
   json_body must be NUL-terminated.  Acquires list->mutex internally. */
void parse_aircraft_json(const char *json_body, AircraftList *list);
