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

#define CFG_FILENAME ".stranded"

#define BRIGHTNESS_MIN 10   // not 0: a black screen would look broken

struct TConfig {
    int currentDay;
    int date;
    int sound;          // play sounds (if audio could be initialized)
    int hotkeys;        // enable hot keys
    int nightMode;      // turn the display off during night hours
    int nightStart;     // in minutes after midnight
    int nightEnd;       // in minutes after midnight
    int cec;            // also put the TV in standby via HDMI-CEC
    int brightness;     // in percent, from BRIGHTNESS_MIN to 100
    long long runningTime;   // seconds Johnny has been on screen, in total
};

extern struct TConfig cfg;

void cfgInit();
void cfgLock();
void cfgUnlock();
void cfgSave();
void cfgApply();
int  cfgParseTime(char *str);
long long cfgGetRunningTime();
void cfgSaveRunningTime(int force);
void cfgCountRunningTime(int counted);
void cfgResetRunningTime();
