/*
 *  This file is part of 'Johnny Reborn'
 *
 *  An open-source engine for the classic
 *  'Johnny Castaway' screensaver by Sierra.
 *
 *  Copyright (C) 2019 Jeremie GUILLAUME
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

// A minimal HTTP server, running in its own thread, which serves a settings
// page and a small API. Changes are handed over to the main thread through
// SDL events (see eventsPushAction())

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>
#include "mytypes.h"
#include "utils.h"
#include "graphics.h"
#include "events.h"
#include "sound.h"
#include "config.h"
#include "night.h"
#include "ads.h"
#include "web.h"


int webPort = 0;    // 0: web server disabled


#ifdef _WIN32

void webInit()
{
    if (webPort)
        fprintf(stderr, "The web server isn't supported on Windows\n");
}

#else

#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define REQUEST_LEN     8192
#define RESPONSE_LEN    4096
#define PAGE_FILENAME   "webui.html"

#define JSON_BOOL(x)    ((x) ? "true" : "false")


static const struct {
    char *name;
    int  action;
} webActions[] = {
    { "pause",             EV_ACTION_PAUSE             },
    { "resume",            EV_ACTION_RESUME            },
    { "toggle-pause",      EV_ACTION_TOGGLE_PAUSE      },
    { "step",              EV_ACTION_STEP              },
    { "toggle-maxspeed",   EV_ACTION_TOGGLE_MAXSPEED   },
    { "quit",              EV_ACTION_QUIT              },
    { "sleep",             EV_ACTION_SLEEP             },
    { "wake",              EV_ACTION_WAKE              },
    { "auto",              EV_ACTION_AUTO              },
    { "next-scene",        EV_ACTION_NEXT_SCENE        },
};


static void webSend(int fd, const char *data, size_t len)
{
    while (len) {
        ssize_t n = write(fd, data, len);
        if (n <= 0)
            return;
        data += n;
        len  -= n;
    }
}


static void webRespond(int fd, int status, const char *contentType,
                       const char *body, size_t len)
{
    char header[256];
    char *reason;

    switch (status) {
        case 200: reason = "OK";                    break;
        case 400: reason = "Bad Request";           break;
        case 404: reason = "Not Found";             break;
        case 405: reason = "Method Not Allowed";    break;
        default:  reason = "Internal Server Error"; break;
    }

    int n = snprintf(header, sizeof(header),
                "HTTP/1.1 %d %s\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n"
                "\r\n",
                status, reason, contentType, len);

    webSend(fd, header, n);
    webSend(fd, body, len);
}


static void webRespondJson(int fd, int status, const char *body)
{
    webRespond(fd, status, "application/json", body, strlen(body));
}


static void webRespondError(int fd, int status, const char *message)
{
    char body[256];

    snprintf(body, sizeof(body), "{\"error\": \"%s\"}\n", message);
    webRespondJson(fd, status, body);
}


// Copy a string for a JSON string value, escaping what needs to be
static void webJsonEscape(char *out, size_t outLen, const char *in)
{
    size_t n = 0;

    for (; *in && n + 7 < outLen; in++) {
        if (*in == '"' || *in == '\\') {
            out[n++] = '\\';
            out[n++] = *in;
        }
        else if ((unsigned char) *in < 0x20) {
            n += snprintf(out + n, outLen - n, "\\u%04x", *in);
        }
        else {
            out[n++] = *in;
        }
    }

    out[n] = 0;
}


static void webRespondStatus(int fd)
{
    char body[RESPONSE_LEN];
    struct TConfig c;
    struct TNowPlaying nowPlaying;
    char description[sizeof(nowPlaying.description) * 6 + 1];
    char *override;

    cfgLock();
    c = cfg;
    nowPlaying = adsNowPlaying;
    cfgUnlock();

    webJsonEscape(description, sizeof(description), nowPlaying.description);

    switch (nightOverride) {
        case NIGHT_FORCE_SLEEP: override = "sleep"; break;
        case NIGHT_FORCE_WAKE:  override = "wake";  break;
        default:                override = "auto";  break;
    }

    snprintf(body, sizeof(body),
        "{\n"
        "  \"paused\": %s,\n"
        "  \"maxSpeed\": %s,\n"
        "  \"soundAvailable\": %s,\n"
        "  \"asleep\": %s,\n"
        "  \"nightScheduled\": %s,\n"
        "  \"nightOverride\": \"%s\",\n"
        "  \"runningTime\": %lld,\n"
        "  \"nowPlaying\": {\n"
        "    \"activity\": \"%s\",\n"
        "    \"ads\": \"%s\",\n"
        "    \"tag\": %d,\n"
        "    \"description\": \"%s\",\n"
        "    \"storyDay\": %d\n"
        "  },\n"
        "  \"display\": {\n"
        "    \"width\": %d,\n"
        "    \"height\": %d,\n"
        "    \"pictureWidth\": %d,\n"
        "    \"pictureHeight\": %d\n"
        "  },\n"
        "  \"settings\": {\n"
        "    \"storyDay\": %d,\n"
        "    \"sound\": %s,\n"
        "    \"hotkeys\": %s,\n"
        "    \"debug\": %s,\n"
        "    \"nightMode\": %s,\n"
        "    \"nightStart\": \"%02d:%02d\",\n"
        "    \"nightEnd\": \"%02d:%02d\",\n"
        "    \"cec\": %s\n"
        "  }\n"
        "}\n",
        JSON_BOOL(evPaused),
        JSON_BOOL(evMaxSpeed),
        JSON_BOOL(!soundDisabled),
        JSON_BOOL(nightAsleep),
        JSON_BOOL(nightScheduled),
        override,
        cfgGetRunningTime(),
        nowPlaying.activity,
        nowPlaying.adsName,
        nowPlaying.adsTag,
        description,
        nowPlaying.storyDay,
        grDisplayWidth,
        grDisplayHeight,
        grWidth,
        SCREEN_HEIGHT,
        c.currentDay,
        JSON_BOOL(c.sound),
        JSON_BOOL(c.hotkeys),
        JSON_BOOL(c.debug),
        JSON_BOOL(c.nightMode),
        c.nightStart / 60, c.nightStart % 60,
        c.nightEnd / 60, c.nightEnd % 60,
        JSON_BOOL(c.cec));

    webRespondJson(fd, 200, body);
}


static void webPutUint16(uint8 *p, uint16 value)
{
    p[0] = value & 0xff;
    p[1] = value >> 8;
}


static void webPutUint32(uint8 *p, uint32 value)
{
    webPutUint16(p, value & 0xffff);
    webPutUint16(p + 2, value >> 16);
}


// What's on screen, as a BMP image. The game only uses a few colors, so
// it's an 8 bits per pixel image (a quarter of the size of a true color one)
static void webRespondScreenshot(int fd)
{
    int width, height;
    uint32 *pixels = grTakeSnapshot(&width, &height);

    if (pixels == NULL) {
        webRespondError(fd, 500, "no picture available");
        return;
    }

    uint32 palette[256];
    int numColors = 0;
    int rowLen = (width + 3) & ~3;
    size_t headerLen = 14 + 40 + 256 * 4;
    size_t len = headerLen + rowLen * height;
    uint8 *bmp = safe_malloc(len);

    memset(bmp, 0, len);

    // Pixels, bottom-up, as indexes in the palette. More than 256 colors
    // isn't expected: the extra ones are drawn with the last one
    for (int y=0; y < height; y++) {

        uint8 *row = bmp + headerLen + (height - 1 - y) * rowLen;

        for (int x=0; x < width; x++) {

            uint32 color = pixels[y * width + x] & 0xffffff;
            int i = row[x - (x > 0)];   // most often, the same color as on the left

            if (x == 0 || palette[i] != color)
                for (i=0; i < numColors && palette[i] != color; i++)
                    ;

            if (i == numColors) {
                if (numColors < 256)
                    palette[numColors++] = color;
                else
                    i = 255;
            }

            row[x] = i;
        }
    }

    free(pixels);

    // File header
    bmp[0] = 'B'; bmp[1] = 'M';
    webPutUint32(bmp + 2, len);
    webPutUint32(bmp + 10, headerLen);

    // Info header
    webPutUint32(bmp + 14, 40);
    webPutUint32(bmp + 18, width);
    webPutUint32(bmp + 22, height);
    webPutUint16(bmp + 26, 1);           // planes
    webPutUint16(bmp + 28, 8);           // bits per pixel
    webPutUint32(bmp + 34, rowLen * height);
    webPutUint32(bmp + 46, 256);         // colors in the palette

    // Palette, as B, G, R, 0
    for (int i=0; i < numColors; i++)
        webPutUint32(bmp + 54 + i * 4, palette[i]);

    webRespond(fd, 200, "image/bmp", (char *) bmp, len);
    free(bmp);
}


static void webRespondPage(int fd)
{
    FILE *f = fopen(PAGE_FILENAME, "rb");

    if (f == NULL) {
        webRespondError(fd, 404, PAGE_FILENAME " not found");
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *page = safe_malloc(len);
    size_t numRead = fread(page, 1, len, f);
    fclose(f);

    webRespond(fd, 200, "text/html; charset=utf-8", page, numRead);
    free(page);
}


// Decode a x-www-form-urlencoded value, in place
static void webUrlDecode(char *str)
{
    char *out = str;

    for (; *str; str++) {
        if (*str == '+') {
            *out++ = ' ';
        }
        else if (*str == '%' && str[1] && str[2]) {
            char hex[3] = { str[1], str[2], 0 };
            *out++ = (char) strtol(hex, NULL, 16);
            str += 2;
        }
        else {
            *out++ = *str;
        }
    }

    *out = 0;
}


static int webParseBool(const char *value, int *result)
{
    if (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "on"))
        *result = 1;
    else if (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "off"))
        *result = 0;
    else
        return 0;

    return 1;
}


// Returns an error message, or NULL if the setting is valid
static char *webApplySetting(struct TConfig *c, char *key, char *value)
{
    int *boolSetting = NULL;
    int time;

    if (!strcmp(key, "storyDay")) {
        char *end;
        long day = strtol(value, &end, 10);
        if (*value == 0 || *end != 0 || day < 1 || day > 11)
            return "storyDay must be between 1 and 11";
        c->currentDay = day;
        c->date = getDayOfYear();   // so that it isn't incremented right away
        return NULL;
    }

    if (!strcmp(key, "nightStart") || !strcmp(key, "nightEnd")) {
        if ((time = cfgParseTime(value)) == -1)
            return "nightStart and nightEnd must be HH:MM";
        if (!strcmp(key, "nightStart"))
            c->nightStart = time;
        else
            c->nightEnd = time;
        return NULL;
    }

    if      (!strcmp(key, "sound"))     boolSetting = &c->sound;
    else if (!strcmp(key, "hotkeys"))   boolSetting = &c->hotkeys;
    else if (!strcmp(key, "debug"))     boolSetting = &c->debug;
    else if (!strcmp(key, "nightMode")) boolSetting = &c->nightMode;
    else if (!strcmp(key, "cec"))       boolSetting = &c->cec;
    else
        return "unknown setting";

    if (!webParseBool(value, boolSetting))
        return "boolean settings must be true or false";

    return NULL;
}


// Body: x-www-form-urlencoded, eg. sound=false&nightStart=22:00
// Either every setting is applied, or none of them
static void webHandleSettings(int fd, char *body)
{
    struct TConfig c;
    char *error = NULL;
    char *pair, *savePtr;

    cfgLock();
    c = cfg;

    for (pair = strtok_r(body, "&", &savePtr);
         pair != NULL && error == NULL;
         pair = strtok_r(NULL, "&", &savePtr)) {

        char *value = strchr(pair, '=');

        if (value == NULL) {
            error = "expected key=value pairs";
            break;
        }

        *value++ = 0;
        webUrlDecode(pair);
        webUrlDecode(value);
        error = webApplySetting(&c, pair, value);
    }

    if (error == NULL) {
        cfg = c;
        cfgSave();
    }

    cfgUnlock();

    if (error != NULL) {
        webRespondError(fd, 400, error);
        return;
    }

    eventsPushAction(EV_ACTION_SETTINGS);
    SDL_Delay(150);     // let the main thread apply it before reporting
    webRespondStatus(fd);
}


static void webHandleAction(int fd, char *name)
{
    // Not an SDL action: nothing for the main thread to do
    if (!strcmp(name, "reset-timer")) {
        cfgResetRunningTime();
        webRespondStatus(fd);
        return;
    }

    for (int i=0; i < sizeof(webActions) / sizeof(webActions[0]); i++) {

        if (!strcmp(name, webActions[i].name)) {

            if (webActions[i].action == EV_ACTION_QUIT) {
                webRespondJson(fd, 200, "{\"ok\": true}\n");
                eventsPushAction(EV_ACTION_QUIT);
                return;
            }

            eventsPushAction(webActions[i].action);
            SDL_Delay(150);     // let the main thread perform it before reporting
            webRespondStatus(fd);
            return;
        }
    }

    webRespondError(fd, 404, "unknown action");
}


static void webHandleClient(int fd)
{
    char request[REQUEST_LEN + 1];
    size_t len = 0;
    char *body = NULL;
    char method[8], path[256];
    long contentLength = 0;
    ssize_t n;

    struct timeval timeout = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Read the headers
    while (len < REQUEST_LEN) {
        if ((n = read(fd, request + len, REQUEST_LEN - len)) <= 0)
            break;
        len += n;
        request[len] = 0;
        if ((body = strstr(request, "\r\n\r\n")) != NULL)
            break;
    }

    if (body == NULL || sscanf(request, "%7s %255s", method, path) != 2) {
        webRespondError(fd, 400, "malformed request");
        return;
    }

    *body = 0;
    body += 4;

    for (char *line = strstr(request, "\r\n"); line != NULL; line = strstr(line + 2, "\r\n"))
        if (!strncasecmp(line + 2, "Content-Length:", 15))
            contentLength = atol(line + 17);

    if (contentLength < 0 || body - request + contentLength > REQUEST_LEN) {
        webRespondError(fd, 400, "request too large");
        return;
    }

    // Read the rest of the body
    while ((long) (len - (body - request)) < contentLength) {
        if ((n = read(fd, request + len, REQUEST_LEN - len)) <= 0)
            break;
        len += n;
    }

    if ((long) (len - (body - request)) < contentLength) {
        webRespondError(fd, 400, "incomplete request body");
        return;
    }

    body[contentLength] = 0;

    char *query = strchr(path, '?');
    if (query != NULL)
        *query = 0;

    int isGet  = !strcmp(method, "GET");
    int isPost = !strcmp(method, "POST");

    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        if (isGet)
            webRespondPage(fd);
        else
            webRespondError(fd, 405, "use GET");
    }
    else if (!strcmp(path, "/api/status")) {
        if (isGet)
            webRespondStatus(fd);
        else
            webRespondError(fd, 405, "use GET");
    }
    else if (!strcmp(path, "/api/screenshot")) {
        if (isGet)
            webRespondScreenshot(fd);
        else
            webRespondError(fd, 405, "use GET");
    }
    else if (!strcmp(path, "/api/settings")) {
        if (isGet)
            webRespondStatus(fd);
        else if (isPost)
            webHandleSettings(fd, body);
        else
            webRespondError(fd, 405, "use GET or POST");
    }
    else if (!strncmp(path, "/api/", 5)) {
        if (isPost)
            webHandleAction(fd, path + 5);
        else
            webRespondError(fd, 405, "use POST");
    }
    else {
        webRespondError(fd, 404, "not found");
    }
}


static int webThread(void *data)
{
    int server = (int) (intptr_t) data;

    while (1) {

        int client = accept(server, NULL, NULL);

        if (client < 0) {
            if (errno != EINTR)
                SDL_Delay(100);
            continue;
        }

        webHandleClient(client);
        close(client);
    }

    return 0;
}


void webInit()
{
    struct sockaddr_in addr;
    int server, yes = 1;

    if (!webPort)
        return;

    signal(SIGPIPE, SIG_IGN);   // a client closing early mustn't kill us

    if ((server = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Web server: socket() failed: %s\n", strerror(errno));
        return;
    }

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(webPort);

    if (bind(server, (struct sockaddr *) &addr, sizeof(addr)) < 0
            || listen(server, 8) < 0) {
        fprintf(stderr, "Web server: couldn't listen on port %d: %s\n",
                webPort, strerror(errno));
        close(server);
        return;
    }

    SDL_CreateThread(webThread, "web", (void *) (intptr_t) server);
    fprintf(stderr, "Web server listening on port %d\n", webPort);
}

#endif
