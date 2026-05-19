// adsb-radar — background HTTP polling thread; fetches aircraft.json from dump1090 or a local file
// Copyright (C) 2026 Mark Williams
// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200112L
#include "network.h"
#include "parse_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

/* POSIX sockets */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>

#define HTTP_BUF_SIZE (256 * 1024)

static char *http_get(const char *host, int port, const char *path)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return NULL;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return NULL; }

    /* 3-second connection timeout via SO_SNDTIMEO */
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res); return NULL;
    }
    freeaddrinfo(res);

    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Accept: application/json\r\n"
        "\r\n",
        path, host, port);

    if (send(fd, req, req_len, 0) != req_len) {
        close(fd); return NULL;
    }

    char *buf = malloc(HTTP_BUF_SIZE);
    if (!buf) { close(fd); return NULL; }

    int total = 0, n;
    while ((n = (int)recv(fd, buf + total, HTTP_BUF_SIZE - total - 1, 0)) > 0) {
        total += n;
        if (total >= HTTP_BUF_SIZE - 1) break;
    }
    close(fd);
    buf[total] = '\0';
    return buf;
}

static const char *http_body(const char *response)
{
    const char *sep = strstr(response, "\r\n\r\n");
    return sep ? sep + 4 : NULL;
}

static char *read_local_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0 || sz >= HTTP_BUF_SIZE) { fclose(f); return NULL; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    buf[sz] = '\0';
    return buf;
}

int network_thread_func(void *userdata)
{
    NetworkThreadArgs *args = (NetworkThreadArgs *)userdata;
    const bool local_mode = args->config->aircraft_json_path[0] != '\0';

    while (true) {
        SDL_LockMutex(args->quit_mutex);
        bool should_quit = *args->quit;
        SDL_UnlockMutex(args->quit_mutex);
        if (should_quit) break;

        if (local_mode) {
            char *json = read_local_file(args->config->aircraft_json_path);
            if (json) {
                parse_aircraft_json(json, args->aircraft_list);
                free(json);
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Cannot read local JSON '%s'",
                            args->config->aircraft_json_path);
            }
        } else {
            char *response = http_get(args->config->dump1090_host,
                                       args->config->dump1090_port,
                                       "/data/aircraft.json");
            if (response) {
                const char *body = http_body(response);
                if (body)
                    parse_aircraft_json(body, args->aircraft_list);
                else
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "HTTP response has no body");
                free(response);
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "HTTP GET failed — retrying in %d ms",
                            args->config->poll_interval_ms);
            }
        }

        /* Sleep, waking immediately if signalled */
        SDL_LockMutex(args->quit_mutex);
        if (!*args->quit)
            SDL_WaitConditionTimeout(args->wake_cond, args->quit_mutex,
                                     args->config->poll_interval_ms);
        SDL_UnlockMutex(args->quit_mutex);
    }

    free(args);
    return 0;
}
