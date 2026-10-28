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

extern int evHotKeysEnabled;
extern int evPaused;
extern int evMaxSpeed;

// Actions which can be triggered from another thread (eg. the web server)
enum {
    EV_ACTION_PAUSE,
    EV_ACTION_RESUME,
    EV_ACTION_TOGGLE_PAUSE,
    EV_ACTION_STEP,
    EV_ACTION_TOGGLE_MAXSPEED,
    EV_ACTION_TOGGLE_FULLSCREEN,
    EV_ACTION_QUIT,
    EV_ACTION_SLEEP,
    EV_ACTION_WAKE,
    EV_ACTION_AUTO,
    EV_ACTION_SETTINGS
};

void eventsInit();
void eventsWaitTick(uint16 delay);
void eventsPushAction(int action);
