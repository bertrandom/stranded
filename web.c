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
#include <stdarg.h>
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
#include "story.h"
#include "web.h"


int webPort = 0;    // 0: web server disabled

// Set by Makefile.linux, from git
#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif


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
#include <sys/wait.h>
#include <netinet/in.h>

#define REQUEST_LEN     8192
#define RESPONSE_LEN    4096
#define PAGE_FILENAME   "webui.html"

// Where the page is installed, if it isn't in the current directory
#ifndef DATA_DIR
#define DATA_DIR        "/usr/share/stranded"
#endif

// Installed by the Debian package, to check for and install updates
#define UPDATE_HELPER   "/usr/lib/stranded/stranded-update"

#define JSON_BOOL(x)    ((x) ? "true" : "false")


// Actions, as POST /api/<name>
static const struct {
    char *name;
    int  action;
    char *description;
} webActions[] = {
    { "pause",           EV_ACTION_PAUSE,           "Pause (hot key: Space)" },
    { "resume",          EV_ACTION_RESUME,          "Resume (hot key: Space)" },
    { "toggle-pause",    EV_ACTION_TOGGLE_PAUSE,    "Pause or resume (hot key: Space)" },
    { "step",            EV_ACTION_STEP,            "When paused, advance one frame (hot key: Return)" },
    { "next-scene",      EV_ACTION_NEXT_SCENE,      "End the current scene and play the next one (hot key: N)" },
    { "toggle-maxspeed", EV_ACTION_TOGGLE_MAXSPEED, "Toggle max / normal speed (hot key: M)" },
    { "sleep",           EV_ACTION_SLEEP,           "Turn the display off now, until the next scheduled switch" },
    { "wake",            EV_ACTION_WAKE,            "Turn the display on now, until the next scheduled switch" },
    { "auto",            EV_ACTION_AUTO,            "Cancel a manual sleep or wake, and follow the night schedule" },
    { "restart",         EV_ACTION_QUIT,            "Quit, so that the service manager starts stranded again. Returns {\"ok\": true} rather than the status" },
    { "quit",            EV_ACTION_QUIT,            "Terminate (hot key: Esc). Run as a service, stranded is started again. Returns {\"ok\": true} rather than the status" },
};


// The other endpoints, as listed by GET /api
static const struct {
    char *method;
    char *path;
    char *description;
} webEndpoints[] = {
    { "GET",  "/",                "The settings page" },
    { "GET",  "/api",             "This list of endpoints and settings" },
    { "GET",  "/api/status",      "Current state, what's playing (nowPlaying), the display's resolution (display), the git revision stranded was built from (revision) and the settings" },
    { "GET",  "/api/settings",    "Same as GET /api/status" },
    { "POST", "/api/settings",    "Change one or more settings. Body: a JSON object, eg. {\"nightMode\": true, \"nightStart\": \"22:00\"}. Either all of them are applied or, on error, none" },
    { "GET",  "/api/screenshot",  "What's on screen, as a BMP image" },
    { "POST", "/api/reset-timer", "Reset the \"stranded for\" timer to zero" },
    { "POST", "/api/reboot",      "Reboot the computer, with systemctl reboot. Needs the permission to (see the README). Returns {\"ok\": true}" },
    { "POST", "/api/shutdown",    "Shut the computer down, with systemctl poweroff. Needs the permission to (see the README). Returns {\"ok\": true}" },
    { "GET",  "/api/update",      "Whether a newer release is available: {\"installed\", \"latest\", \"updateAvailable\", \"updating\"}. Only when installed with the Debian package" },
    { "POST", "/api/update",      "Download and install the latest release, in the background; stranded restarts once it's installed. Returns {\"ok\": true}" },
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
        "  \"revision\": \"%s\",\n"
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
        GIT_REVISION,
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

    if (f == NULL)
        f = fopen(DATA_DIR "/" PAGE_FILENAME, "rb");

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


// The settings which POST /api/settings accepts, as listed by GET /api
static const struct {
    char *name;
    char *type;
    char *constraints;  // more JSON properties for GET /api, or NULL
    char *description;
} webSettings[] = {
    { "storyDay",   "integer", "\"minimum\": 1, \"maximum\": 11",
      "Day of the story. It normally advances by one each day; a new value takes effect at the next scene" },
    { "sound",      "boolean", NULL, "Play sounds. Without effect if started with nosound" },
    { "hotkeys",    "boolean", NULL, "Enable hot keys. When off, any key press quits" },
    { "nightMode",  "boolean", NULL, "Turn the display off during night hours" },
    { "nightStart", "string",  "\"format\": \"HH:MM\"", "Start of the night, local time" },
    { "nightEnd",   "string",  "\"format\": \"HH:MM\"", "End of the night, local time" },
    { "cec",        "boolean", NULL, "Also put the TV in standby with HDMI-CEC" },
};


// A value of a JSON object, as found in a settings request
enum { JSON_STRING, JSON_INTEGER, JSON_BOOLEAN };

struct TJsonValue {
    int  type;
    char string[64];
    long integer;
    int  boolean;
};


static void webSkipJsonSpaces(char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')
        (*p)++;
}


// Parse the JSON string at *p, and move past it. Returns 0 if malformed
static int webParseJsonString(char **p, char *out, size_t outLen)
{
    size_t n = 0;

    if (**p != '"')
        return 0;

    for ((*p)++; **p != '"'; (*p)++) {

        char ch = **p;

        if (ch == 0)
            return 0;

        if (ch == '\\') {
            switch (*++(*p)) {
                case '"': case '\\': case '/': ch = **p; break;
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                default: return 0;      // \uXXXX isn't needed for any setting
            }
        }

        if (n + 1 >= outLen)
            return 0;

        out[n++] = ch;
    }

    (*p)++;
    out[n] = 0;

    return 1;
}


// Parse the JSON value at *p, and move past it. Only strings, integers and
// booleans are accepted: no setting needs anything else
static int webParseJsonValue(char **p, struct TJsonValue *value)
{
    if (**p == '"') {
        value->type = JSON_STRING;
        return webParseJsonString(p, value->string, sizeof(value->string));
    }

    if (!strncmp(*p, "true", 4) || !strncmp(*p, "false", 5)) {
        value->type = JSON_BOOLEAN;
        value->boolean = (**p == 't');
        *p += value->boolean ? 4 : 5;
        return 1;
    }

    if (**p == '-' || (**p >= '0' && **p <= '9')) {
        char *end;
        value->type = JSON_INTEGER;
        value->integer = strtol(*p, &end, 10);
        if (end == *p || *end == '.' || *end == 'e' || *end == 'E')
            return 0;
        *p = end;
        return 1;
    }

    return 0;
}


// Returns an error message, or NULL if the setting is valid
static char *webApplySetting(struct TConfig *c, char *key, struct TJsonValue *value)
{
    int *boolSetting = NULL;
    int time;

    if (!strcmp(key, "storyDay")) {
        if (value->type != JSON_INTEGER || value->integer < 1 || value->integer > 11)
            return "storyDay must be a number from 1 to 11";
        c->currentDay = value->integer;
        // So that it isn't incremented right away
        c->date = storyClockIsSynchronized() ? getDayOfYear() : -1;
        return NULL;
    }

    if (!strcmp(key, "nightStart") || !strcmp(key, "nightEnd")) {
        if (value->type != JSON_STRING || (time = cfgParseTime(value->string)) == -1)
            return "nightStart and nightEnd must be strings, as HH:MM";
        if (!strcmp(key, "nightStart"))
            c->nightStart = time;
        else
            c->nightEnd = time;
        return NULL;
    }

    if      (!strcmp(key, "sound"))     boolSetting = &c->sound;
    else if (!strcmp(key, "hotkeys"))   boolSetting = &c->hotkeys;
    else if (!strcmp(key, "nightMode")) boolSetting = &c->nightMode;
    else if (!strcmp(key, "cec"))       boolSetting = &c->cec;
    else
        return "unknown setting, see GET /api";

    if (value->type != JSON_BOOLEAN)
        return "boolean settings must be true or false";

    *boolSetting = value->boolean;

    return NULL;
}


// Body: a JSON object, eg. {"sound": false, "nightStart": "22:00"}
// Either every setting is applied, or none of them
static void webHandleSettings(int fd, char *body)
{
    struct TConfig c;
    char *error = NULL;
    char *p = body;

    cfgLock();
    c = cfg;

    webSkipJsonSpaces(&p);

    if (*p++ != '{')
        error = "expected a JSON object";

    webSkipJsonSpaces(&p);

    if (error == NULL && *p == '}')
        p++;

    else while (error == NULL) {

        char key[32];
        struct TJsonValue value;

        if (!webParseJsonString(&p, key, sizeof(key))) {
            error = "malformed JSON: expected a setting name";
            break;
        }

        webSkipJsonSpaces(&p);

        if (*p++ != ':') {
            error = "malformed JSON: expected a colon";
            break;
        }

        webSkipJsonSpaces(&p);

        if (!webParseJsonValue(&p, &value)) {
            error = "malformed JSON: values must be strings, integers or booleans";
            break;
        }

        if ((error = webApplySetting(&c, key, &value)) != NULL)
            break;

        webSkipJsonSpaces(&p);

        if (*p == ',') {
            p++;
            webSkipJsonSpaces(&p);
        }
        else if (*p == '}') {
            p++;
            break;
        }
        else {
            error = "malformed JSON: expected a comma or a closing brace";
        }
    }

    webSkipJsonSpaces(&p);

    if (error == NULL && *p != 0)
        error = "malformed JSON: unexpected data after the object";

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


// Append to a response being built. Returns the new length
static size_t webAppend(char *buf, size_t len, size_t bufLen, const char *fmt, ...)
{
    va_list ap;

    if (len >= bufLen)
        return len;

    va_start(ap, fmt);
    int n = vsnprintf(buf + len, bufLen - len, fmt, ap);
    va_end(ap);

    return n < 0 ? len : len + n;
}


static void webRespondApi(int fd)
{
    char body[8192];
    char escaped[512];
    size_t len = 0, max = sizeof(body);
    int numEndpoints = sizeof(webEndpoints) / sizeof(webEndpoints[0]);
    int numActions   = sizeof(webActions) / sizeof(webActions[0]);
    int numSettings  = sizeof(webSettings) / sizeof(webSettings[0]);

    len = webAppend(body, len, max, "{\n  \"endpoints\": [\n");

    for (int i=0; i < numEndpoints + numActions; i++) {

        char path[64];
        const char *method, *description;

        if (i < numEndpoints) {
            method = webEndpoints[i].method;
            snprintf(path, sizeof(path), "%s", webEndpoints[i].path);
            description = webEndpoints[i].description;
        }
        else {
            method = "POST";
            snprintf(path, sizeof(path), "/api/%s", webActions[i - numEndpoints].name);
            description = webActions[i - numEndpoints].description;
        }

        webJsonEscape(escaped, sizeof(escaped), description);
        len = webAppend(body, len, max,
            "    { \"method\": \"%s\", \"path\": \"%s\", \"description\": \"%s\" }%s\n",
            method, path, escaped, i < numEndpoints + numActions - 1 ? "," : "");
    }

    len = webAppend(body, len, max, "  ],\n  \"settings\": [\n");

    for (int i=0; i < numSettings; i++) {
        webJsonEscape(escaped, sizeof(escaped), webSettings[i].description);
        len = webAppend(body, len, max,
            "    { \"name\": \"%s\", \"type\": \"%s\", %s%s\"description\": \"%s\" }%s\n",
            webSettings[i].name, webSettings[i].type,
            webSettings[i].constraints ? webSettings[i].constraints : "",
            webSettings[i].constraints ? ", " : "",
            escaped, i < numSettings - 1 ? "," : "");
    }

    len = webAppend(body, len, max, "  ]\n}\n");

    if (len >= max) {
        webRespondError(fd, 500, "response too large");
        return;
    }

    webRespondJson(fd, 200, body);
}


// Run a command, and wait for it. Up to outLen - 1 bytes of what it prints
// go to out (if not NULL). Returns its exit status, or -1 if it couldn't run
static int webRun(char *const argv[], char *out, size_t outLen)
{
    int status, pipeFds[2];
    size_t len = 0;

    if (pipe(pipeFds) < 0)
        return -1;

    pid_t pid = fork();

    if (pid == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipeFds[1]);

    // Read everything, so that the command never blocks on a full pipe
    for (ssize_t n=1; n > 0; ) {
        char buf[512];
        n = read(pipeFds[0], buf, sizeof(buf));
        for (ssize_t i=0; i < n; i++)
            if (out != NULL && len + 1 < outLen)
                out[len++] = buf[i];
    }

    close(pipeFds[0]);

    if (out != NULL && outLen)
        out[len] = 0;

    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}


// Reboot or power off the computer, through systemd. stranded usually
// runs as an unprivileged user: it needs to be allowed to, by a polkit rule
static void webSystemctl(int fd, char *verb)
{
    char *argv[] = { "systemctl", "--no-ask-password", verb, NULL };

    if (webRun(argv, NULL, 0) != 0) {
        char message[128];
        snprintf(message, sizeof(message),
                 "systemctl %s failed: is stranded allowed to? See the README", verb);
        webRespondError(fd, 500, message);
        return;
    }

    webRespondJson(fd, 200, "{\"ok\": true}\n");
}


// GET: whether a newer release is available, as reported by the Debian
// package's update helper. POST: start installing it, as root, through
// stranded-update.service; stranded restarts once it's installed
static void webHandleUpdate(int fd, int isPost)
{
    char output[RESPONSE_LEN];

    if (access(UPDATE_HELPER, X_OK) != 0) {
        webRespondError(fd, 404, "updating is only possible when installed with the Debian package");
        return;
    }

    if (!isPost) {
        char *argv[] = { UPDATE_HELPER, "check", NULL };

        if (webRun(argv, output, sizeof(output)) != 0 || output[0] != '{') {
            webRespondError(fd, 500, "couldn't check for updates: see stranded's log");
            return;
        }

        webRespondJson(fd, 200, output);
        return;
    }

    char *argv[] = { "systemctl", "--no-ask-password", "--no-block",
                     "start", "stranded-update.service", NULL };

    if (webRun(argv, NULL, 0) != 0) {
        webRespondError(fd, 500, "couldn't start stranded-update.service: is stranded allowed to? See the README");
        return;
    }

    webRespondJson(fd, 200, "{\"ok\": true}\n");
}


static void webHandleAction(int fd, char *name)
{
    if (!strcmp(name, "reboot")) {
        webSystemctl(fd, "reboot");
        return;
    }

    if (!strcmp(name, "shutdown")) {
        webSystemctl(fd, "poweroff");
        return;
    }

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

    webRespondError(fd, 404, "unknown action, see GET /api");
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
    else if (!strcmp(path, "/api") || !strcmp(path, "/api/")) {
        if (isGet)
            webRespondApi(fd);
        else
            webRespondError(fd, 405, "use GET");
    }
    else if (!strcmp(path, "/api/status")) {
        if (isGet)
            webRespondStatus(fd);
        else
            webRespondError(fd, 405, "use GET");
    }
    else if (!strcmp(path, "/api/update")) {
        if (isGet || isPost)
            webHandleUpdate(fd, isPost);
        else
            webRespondError(fd, 405, "use GET or POST");
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
        webRespondError(fd, 404, "not found, see GET /api");
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
