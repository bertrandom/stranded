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

// What's on screen, for the web page. Read and written under cfgLock()
struct TNowPlaying {
    char activity[8];       // "intro", "scene", "walk", or "" when unknown
    char adsName[13];       // for a scene: the ADS script and tag,
    int  adsTag;            // with the tag's description
    char description[64];
    int  storyDay;          // day of the story the scene belongs to, or 0
};

extern struct TNowPlaying adsNowPlaying;

void adsSetNowPlaying(char *activity, char *adsName, int adsTag, int storyDay);
void adsInit();
void adsInitIsland();
void adsReleaseIsland();
void adsNoIsland();
void adsPlay(char *adsName, uint16 adsTag);
void adsPlayIntro();
void adsPlayWalk();
void adsPlaySingleTtm(char *ttmName);
void adsPlayBench();  // TODO

