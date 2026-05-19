// adsb-radar — serial NMEA GPS reader thread; parses $GPRMC/$GPGGA and updates GPSState
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200112L
#include "gps.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <SDL3/SDL.h>

/* ── Serial helpers ──────────────────────────────────────────────────────── */

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

static int open_serial(const char *port, int baud)
{
    int fd = open(port, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    /* Switch to blocking I/O after open */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) { close(fd); return -1; }

    /* Raw 8N1, no flow control, no processing */
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;   /* 1-second read timeout */

    cfsetispeed(&tty, baud_to_speed(baud));
    cfsetospeed(&tty, baud_to_speed(baud));

    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return -1; }
    return fd;
}

/* ── NMEA helpers ────────────────────────────────────────────────────────── */

static bool nmea_checksum_ok(const char *s)
{
    if (s[0] != '$') return false;
    const char *star = strchr(s, '*');
    if (!star || (int)strlen(star) < 3) return false;

    unsigned char xorval = 0;
    for (const char *p = s + 1; p < star; p++)
        xorval ^= (unsigned char)*p;

    unsigned int expected = 0;
    if (sscanf(star + 1, "%2X", &expected) != 1) return false;
    return xorval == (unsigned char)expected;
}

/* Split on ',' and '*'; fills up to max_fields entries. Returns field count. */
static int nmea_split(const char *s, char fields[][32], int max_fields)
{
    int fi = 0;
    const char *p = s;
    while (fi < max_fields) {
        int len = 0;
        while (*p && *p != ',' && *p != '*' && len < 31)
            fields[fi][len++] = *p++;
        fields[fi][len] = '\0';
        fi++;
        if (!*p || *p == '*') break;
        p++;    /* skip ',' */
    }
    return fi;
}

/* NMEA DDMM.MMMM → decimal degrees */
static double ddmm_to_deg(const char *field)
{
    if (!field || !field[0]) return 0.0;
    double raw = atof(field);
    int    deg = (int)(raw / 100.0);
    double min = raw - (double)(deg * 100);
    return (double)deg + min / 60.0;
}

static void update_fix(GPSState *gps, double lat, double lon)
{
    SDL_LockMutex(gps->gps_mutex);
    gps->lat        = lat;
    gps->lon        = lon;
    gps->fix_status = GPS_STATUS_FIX;
    gps->fix_time_ms = SDL_GetTicks();
    SDL_UnlockMutex(gps->gps_mutex);
}

static void update_no_fix(GPSState *gps)
{
    SDL_LockMutex(gps->gps_mutex);
    if (gps->fix_status == GPS_STATUS_FIX)
        gps->fix_status = GPS_STATUS_NO_FIX;
    SDL_UnlockMutex(gps->gps_mutex);
}

static void parse_gprmc(const char *s, GPSState *gps)
{
    char f[12][32];
    if (nmea_split(s, f, 12) < 7) return;

    if (f[2][0] != 'A') { update_no_fix(gps); return; }

    double lat = ddmm_to_deg(f[3]);
    if (f[4][0] == 'S') lat = -lat;
    double lon = ddmm_to_deg(f[5]);
    if (f[6][0] == 'W') lon = -lon;
    update_fix(gps, lat, lon);
}

static void parse_gpgga(const char *s, GPSState *gps)
{
    char f[10][32];
    if (nmea_split(s, f, 10) < 7) return;

    if (f[6][0] == '0' || f[6][0] == '\0') { update_no_fix(gps); return; }

    double lat = ddmm_to_deg(f[2]);
    if (f[3][0] == 'S') lat = -lat;
    double lon = ddmm_to_deg(f[4]);
    if (f[5][0] == 'W') lon = -lon;
    update_fix(gps, lat, lon);
}

static void process_sentence(const char *s, GPSState *gps)
{
    if (!nmea_checksum_ok(s)) return;
    /* Accept both GP (single-system) and GN (multi-constellation) talker IDs */
    if (strncmp(s + 3, "RMC,", 4) == 0) parse_gprmc(s, gps);
    else if (strncmp(s + 3, "GGA,", 4) == 0) parse_gpgga(s, gps);
}

/* ── Main read loop ──────────────────────────────────────────────────────── */

static void read_loop(int fd, GPSThreadArgs *args)
{
    char   line[128];
    int    len               = 0;
    int    consecutive_empty = 0;

    while (!*args->quit) {
        char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n < 0) return;      /* error — reconnect */
        if (n == 0) {
            /* VTIME expired with no data */
            if (++consecutive_empty > 10) return;   /* device gone */
            continue;
        }
        consecutive_empty = 0;

        if (ch == '\r') continue;
        if (ch == '\n') {
            line[len] = '\0';
            if (len > 0) process_sentence(line, args->gps);
            len = 0;
            continue;
        }
        if (len < (int)sizeof(line) - 1)
            line[len++] = ch;
        else
            len = 0;    /* overlong line — discard and resync */
    }
}

/* ── Thread entry point ──────────────────────────────────────────────────── */

int gps_thread_func(void *arg)
{
    GPSThreadArgs *args = (GPSThreadArgs *)arg;
    GPSState      *gps  = args->gps;
    const Config  *cfg  = args->config;

    while (!*args->quit) {
        int fd = open_serial(cfg->gps_port, cfg->gps_baud);
        if (fd < 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "GPS: cannot open %s (%s) — retrying in 5 s",
                        cfg->gps_port, strerror(errno));
            SDL_LockMutex(gps->gps_mutex);
            gps->fix_status = GPS_STATUS_NO_DEVICE;
            SDL_UnlockMutex(gps->gps_mutex);

            SDL_LockMutex(args->quit_mutex);
            if (!*args->quit)
                SDL_WaitConditionTimeout(args->wake_cond, args->quit_mutex, 5000);
            SDL_UnlockMutex(args->quit_mutex);
            continue;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPS: opened %s at %d baud", cfg->gps_port, cfg->gps_baud);
        SDL_LockMutex(gps->gps_mutex);
        if (gps->fix_status == GPS_STATUS_NO_DEVICE)
            gps->fix_status = GPS_STATUS_NO_FIX;
        SDL_UnlockMutex(gps->gps_mutex);

        read_loop(fd, args);
        close(fd);

        if (!*args->quit) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPS: lost %s — reconnecting", cfg->gps_port);
            SDL_LockMutex(gps->gps_mutex);
            gps->fix_status = GPS_STATUS_NO_DEVICE;
            SDL_UnlockMutex(gps->gps_mutex);
        }
    }

    free(args);
    return 0;
}
