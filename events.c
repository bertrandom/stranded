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
#include "graphics.h"
#include "config.h"
#include "night.h"
#include "events.h"


static uint32 lastTicks = 0x00ffffff;
static int oneFrame = 0;
static uint32 evActionEventType = (uint32) -1;

int evHotKeysEnabled = 0;
int evPaused   = 0;
int evMaxSpeed = 0;
int evSkipScene = 0;   // set to end the current scene and play the next one


static void eventsQuit()
{
    cfgCountRunningTime(0);     // save it, and stop counting: SDL is about to quit
    graphicsEnd();
    exit(255);
}


static void eventsDoAction(int action)
{
    switch (action) {

        case EV_ACTION_PAUSE:
            evPaused = 1;
            break;

        case EV_ACTION_RESUME:
            evPaused = 0;
            break;

        case EV_ACTION_TOGGLE_PAUSE:
            evPaused = !evPaused;
            break;

        case EV_ACTION_STEP:
            oneFrame = 1;
            break;

        case EV_ACTION_TOGGLE_MAXSPEED:
            evMaxSpeed = !evMaxSpeed;
            break;

        case EV_ACTION_TOGGLE_FULLSCREEN:
            grToggleFullScreen();
            oneFrame = 1;   // to redraw the window // TODO
            break;

        case EV_ACTION_QUIT:
            eventsQuit();
            break;

        case EV_ACTION_SLEEP:
            nightSetOverride(NIGHT_FORCE_SLEEP);
            break;

        case EV_ACTION_WAKE:
            nightSetOverride(NIGHT_FORCE_WAKE);
            break;

        case EV_ACTION_AUTO:
            nightSetOverride(NIGHT_AUTO);
            break;

        case EV_ACTION_NEXT_SCENE:
            if (!nightAsleep)
                evSkipScene = 1;
            break;

        case EV_ACTION_SETTINGS:
            cfgApply();
            nightScheduleChanged();
            break;
    }
}


static void eventsProcessEvents()
{
    SDL_Event event;

    cfgSaveRunningTime(0);
    grServiceSnapshot();

    while (SDL_PollEvent(&event)) {

        if (event.type == evActionEventType) {
            eventsDoAction(event.user.code);
            continue;
        }

        switch(event.type) {

            case SDL_KEYDOWN:

                if (evHotKeysEnabled) {

                    switch (event.key.keysym.sym) {

                        case SDLK_SPACE:
                            eventsDoAction(EV_ACTION_TOGGLE_PAUSE);
                            break;

                        case SDLK_m:
                            eventsDoAction(EV_ACTION_TOGGLE_MAXSPEED);
                            break;

                        case SDLK_n:
                            eventsDoAction(EV_ACTION_NEXT_SCENE);
                            break;

                        case SDLK_RETURN:
                            if (event.key.keysym.mod & KMOD_LALT)
                                eventsDoAction(EV_ACTION_TOGGLE_FULLSCREEN);
                            else
                                eventsDoAction(EV_ACTION_STEP);
                            break;

                        case SDLK_ESCAPE:
                            eventsQuit();
                            break;
                    }
                }
                else {
                    // Normal behaviour : no hot keys, the screen saver
                    // terminates if any key is pressed
                    eventsQuit();
                }
                break;

            case SDL_WINDOWEVENT:
                grRefreshDisplay();
                break;

            case SDL_QUIT:
                eventsQuit();
                break;
        }
    }
}


static void eventsSleepDuringNight()
{
    nightEnter();

    while (nightShouldSleep()) {
        SDL_Delay(100);
        eventsProcessEvents();
    }

    nightLeave();
}


void eventsInit()
{
    evActionEventType = SDL_RegisterEvents(1);
    lastTicks = SDL_GetTicks();
}


void eventsWaitTick(uint16 delay)
{
    delay *= 20;
    oneFrame = 0;

    eventsProcessEvents();

    while (1) {

        if (nightShouldSleep()) {
            eventsSleepDuringNight();
            lastTicks = SDL_GetTicks() - delay;  // no need to wait any longer
        }

        // Skipping a scene doesn't wait, even when paused
        if (evSkipScene || !((evPaused && !oneFrame)
               || (!evMaxSpeed && (SDL_GetTicks() - lastTicks < delay))))
            break;

        SDL_Delay(5);
        eventsProcessEvents();
    }

    lastTicks = SDL_GetTicks();
}


// Thread-safe: the action is performed by the main thread
void eventsPushAction(int action)
{
    SDL_Event event;

    if (evActionEventType == (uint32) -1)
        return;

    SDL_zero(event);
    event.type      = evActionEventType;
    event.user.code = action;
    SDL_PushEvent(&event);
}
