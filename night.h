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

// Manual override of the night schedule, which lasts until the next
// scheduled switch
#define NIGHT_AUTO          0
#define NIGHT_FORCE_SLEEP   1
#define NIGHT_FORCE_WAKE    2

extern int nightOverride;
extern int nightAsleep;
extern int nightScheduled;

int  nightShouldSleep();
void nightSetOverride(int override);
void nightScheduleChanged();
void nightEnter();
void nightLeave();
