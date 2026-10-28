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

#include <SDL2/SDL.h>
#include "mytypes.h"
#include "utils.h"
#include "graphics.h"
#include "config.h"
#include "night.h"


int nightOverride  = NIGHT_AUTO;
int nightAsleep    = 0;
int nightScheduled = 0;

static uint32 lastCheckTicks = 0;
static int    scheduleChecked = 0;


// Must be called with the config locked
static int nightIsScheduled()
{
    int now   = getMinutesOfDay();
    int start = cfg.nightStart;
    int end   = cfg.nightEnd;

    if (!cfg.nightMode || start == end)
        return 0;

    if (start < end)
        return start <= now && now < end;
    else
        return now >= start || now < end;   // night spans midnight
}


static void nightCec(int on)
{
#ifdef __linux__
    if (system(on
            ? "cec-ctl -d /dev/cec0 --playback --to 0 --image-view-on >/dev/null 2>&1"
            : "cec-ctl -d /dev/cec0 --playback --to 0 --standby >/dev/null 2>&1"))
        debugMsg("cec-ctl failed");
#endif
}


int nightShouldSleep()
{
    uint32 ticks = SDL_GetTicks();

    // Checking the time once per second is enough
    if (!scheduleChecked || ticks - lastCheckTicks >= 1000) {

        int scheduled;

        cfgLock();
        scheduled = nightIsScheduled();
        cfgUnlock();

        // A scheduled switch ends any manual override
        if (scheduleChecked && scheduled != nightScheduled)
            nightOverride = NIGHT_AUTO;

        nightScheduled  = scheduled;
        lastCheckTicks  = ticks;
        scheduleChecked = 1;
    }

    if (nightOverride == NIGHT_FORCE_SLEEP)
        return 1;

    if (nightOverride == NIGHT_FORCE_WAKE)
        return 0;

    return nightScheduled;
}


void nightSetOverride(int override)
{
    nightOverride = override;
}


// The night settings were changed: check them again without waiting
void nightScheduleChanged()
{
    scheduleChecked = 0;
}


void nightEnter()
{
    fprintf(stderr, "Night mode: turning the display off\n");
    nightAsleep = 1;

    grDisplayOff();

    if (cfg.cec)
        nightCec(0);
}


void nightLeave()
{
    fprintf(stderr, "Night mode: turning the display on\n");
    nightAsleep = 0;

    if (cfg.cec)
        nightCec(1);

    grDisplayOn();
}
