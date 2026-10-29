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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>
#include "mytypes.h"
#include "utils.h"
#include "events.h"
#include "sound.h"
#include "config.h"

#define BUFFER_LEN 100


struct TConfig cfg;

static SDL_mutex *cfgMutex = NULL;

static uint32 runningTicks = 0;      // when cfg.runningTime was last updated
static int    runningCounted = 1;    // 0 while the display is off
static uint32 lastSaveTicks = 0;

#define RUNNING_TIME_SAVE_DELAY 60000   // ms


static char *cfgFullPath()
{
    char *home;
    static char *result = NULL;


    if (result == NULL) {

        home = getenv("HOME");

        if (home != NULL) {
            if (strlen(home)) {
                result = safe_malloc(strlen(home) + strlen(CFG_FILENAME) + 2);
                strcpy(result, home);
                strcat(result, "/");
                strcat(result, CFG_FILENAME);
            }
        }

        if (result == NULL) {
            result = safe_malloc(strlen(CFG_FILENAME) + 1);
            strcpy(result, CFG_FILENAME);
        }
    }

    return result;
}


// Parses "HH:MM", returns minutes after midnight or -1 if invalid
int cfgParseTime(char *str)
{
    int hours, minutes;

    if (sscanf(str, "%d:%d", &hours, &minutes) != 2
         || hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
        return -1;

    return hours * 60 + minutes;
}


static void cfgFileRead()
{
    char buf[BUFFER_LEN];
    int  time;

    FILE *f = fopen(cfgFullPath(), "r");

    if (f == NULL)
        return;

    while (fgets(buf, BUFFER_LEN, f) != NULL) {

        if (strstr(buf, "currentDay=") == buf)
            cfg.currentDay = atoi(buf + 11);

        else if (strstr(buf, "date=") == buf)
            cfg.date = atoi(buf + 5);

        else if (strstr(buf, "sound=") == buf)
            cfg.sound = atoi(buf + 6) != 0;

        else if (strstr(buf, "hotkeys=") == buf)
            cfg.hotkeys = atoi(buf + 8) != 0;

        else if (strstr(buf, "nightMode=") == buf)
            cfg.nightMode = atoi(buf + 10) != 0;

        else if (strstr(buf, "nightStart=") == buf) {
            if ((time = cfgParseTime(buf + 11)) != -1)
                cfg.nightStart = time;
        }

        else if (strstr(buf, "nightEnd=") == buf) {
            if ((time = cfgParseTime(buf + 9)) != -1)
                cfg.nightEnd = time;
        }

        else if (strstr(buf, "cec=") == buf)
            cfg.cec = atoi(buf + 4) != 0;

        else if (strstr(buf, "runningTime=") == buf)
            cfg.runningTime = atoll(buf + 12);
    }

    fclose(f);
}


// Set the defaults (from the command line options), then override them
// with what was saved in the config file
void cfgInit()
{
    cfgMutex = SDL_CreateMutex();

    cfg.currentDay = 0;
    cfg.date       = -1;    // not known yet
    cfg.sound      = 1;
    cfg.hotkeys    = evHotKeysEnabled;
    cfg.nightMode  = 0;
    cfg.nightStart = 22 * 60;
    cfg.nightEnd   = 8 * 60;
    cfg.cec        = 1;
    cfg.runningTime = 0;

    cfgFileRead();

    runningTicks = lastSaveTicks = SDL_GetTicks();

    cfgApply();
}


void cfgLock()
{
    SDL_LockMutex(cfgMutex);
}


void cfgUnlock()
{
    SDL_UnlockMutex(cfgMutex);
}


// Must be called with the config locked
void cfgSave()
{
    FILE *f = fopen(cfgFullPath(), "w");

    if (f == NULL) {
        debugMsg("Warning: couldn't open %s for writing", CFG_FILENAME);
    }
    else {
        fprintf(f, "currentDay=%d\n", cfg.currentDay);
        fprintf(f, "date=%d\n", cfg.date);
        fprintf(f, "sound=%d\n", cfg.sound);
        fprintf(f, "hotkeys=%d\n", cfg.hotkeys);
        fprintf(f, "nightMode=%d\n", cfg.nightMode);
        fprintf(f, "nightStart=%02d:%02d\n", cfg.nightStart / 60, cfg.nightStart % 60);
        fprintf(f, "nightEnd=%02d:%02d\n", cfg.nightEnd / 60, cfg.nightEnd % 60);
        fprintf(f, "cec=%d\n", cfg.cec);
        fprintf(f, "runningTime=%lld\n", cfg.runningTime);
        fclose(f);
    }
}


// Push the settings to the modules which use them. Called from the main thread
void cfgApply()
{
    cfgLock();
    evHotKeysEnabled = cfg.hotkeys;
    soundMuted       = !cfg.sound;
    cfgUnlock();
}



// Must be called with the config locked. Only whole seconds are added,
// the remainder is kept for the next update
static void cfgUpdateRunningTime()
{
    if (!runningCounted)
        return;

    uint32 now = SDL_GetTicks();

    // SDL's ticks start again from 0 after SDL_Quit(): don't count that as
    // 49 days (2^32 ms) going by
    if ((sint32) (now - runningTicks) < 0) {
        runningTicks = now;
        return;
    }

    uint32 seconds = (now - runningTicks) / 1000;

    cfg.runningTime += seconds;
    runningTicks    += seconds * 1000;
}


long long cfgGetRunningTime()
{
    long long result;

    cfgLock();
    cfgUpdateRunningTime();
    result = cfg.runningTime;
    cfgUnlock();

    return result;
}


// Saved once a minute, and when quitting, so that at most a minute is lost
// if the program is killed
void cfgSaveRunningTime(int force)
{
    if (!force && SDL_GetTicks() - lastSaveTicks < RUNNING_TIME_SAVE_DELAY)
        return;

    cfgLock();
    cfgUpdateRunningTime();
    cfgSave();
    lastSaveTicks = SDL_GetTicks();
    cfgUnlock();
}


// The running timer only counts while Johnny is on screen: it's stopped
// while night mode has the display off
void cfgCountRunningTime(int counted)
{
    cfgLock();
    cfgUpdateRunningTime();
    runningCounted = counted;
    runningTicks = SDL_GetTicks();
    cfgSave();
    cfgUnlock();
}


void cfgResetRunningTime()
{
    cfgLock();
    cfg.runningTime = 0;
    runningTicks = SDL_GetTicks();
    cfgSave();
    cfgUnlock();
}
