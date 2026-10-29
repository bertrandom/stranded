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
#include <time.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#if defined(HAVE_LIBDRM) && defined(SDL_VIDEO_DRIVER_KMSDRM)
#include <xf86drm.h>
#include <xf86drmMode.h>
#define USE_DPMS
#endif

#include "mytypes.h"
#include "utils.h"
#include "graphics.h"
#include "resource.h"
#include "events.h"


static SDL_Window *sdl_window;
static SDL_Renderer *sdl_renderer;
static SDL_Texture *sdl_texture;

// Everything is drawn here, then scaled to fill the display by grPresent()
static SDL_Surface *grCanvas;

static void grPresent();

// Snapshots of the canvas for other threads (the web page's preview): the
// main thread copies the canvas when asked, from grServiceSnapshot()
static SDL_mutex *grSnapshotLock;
static SDL_cond  *grSnapshotDone;
static int       grSnapshotRequested = 0;
static uint32    grSnapshotCount = 0;
static uint32    *grSnapshotPixels = NULL;

static uint8 ttmPalette[16][4];

static SDL_Surface *grSavedZonesLayer = NULL;

static SDL_Surface *grSavedScreen = NULL;

SDL_Surface *grBackgroundSfc = NULL;

int grDx = 0;
int grDy = 0;
int grWindowed = 0;
int grUpdateDelay = 0;

// Aspect ratio to draw for, eg. 16:9; 0:0 means the display's
int grAspectW = 0;
int grAspectH = 0;

// On a widescreen display, the picture is widened rather than stretched:
// the 640x480 scene is drawn in the middle of a wider canvas, with a margin
// of grMarginX pixels on each side. For open ocean scenes, the ocean and sky
// are extended into the margins; other scenes keep black margins
int grWidth = SCREEN_WIDTH;
int grMarginX = 0;

// Size of the window, or of the display when full screen
int grDisplayWidth = 0;
int grDisplayHeight = 0;
static int grWideScene = 0;


static void grReleaseScreen()
{
    free(grBackgroundSfc->pixels);
    SDL_FreeSurface(grBackgroundSfc);
    grBackgroundSfc = NULL;
}


static void grReleaseSavedLayer()
{
    SDL_FreeSurface(grSavedZonesLayer);
    grSavedZonesLayer = NULL;
}


static void grPutPixel(SDL_Surface *sfc, uint16 x, uint16 y, uint8 color)
{
    // TODO: Implement Cohen-Sutherland clipping algorithm or such for
    // grDrawLine(), and another ad hoc algorithm for grDrawCircle()

    if (x>=0 && y>=0 && x<sfc->w && y<sfc->h) {

        uint8 *pixel = (uint8*) sfc->pixels;

        pixel += (y * sfc->pitch) + (x * sfc->format->BytesPerPixel);

        pixel[0] = ttmPalette[color][0];
        pixel[1] = ttmPalette[color][1];
        pixel[2] = ttmPalette[color][2];
        pixel[3] = 0;
    }
}


static void grDrawHorizontalLine(SDL_Surface *sfc, sint16 x1, sint16 x2, sint16 y, uint8 color)
{
    if (y < 0 || y >= sfc->h)
        return;

    x1 = x1 < 0 ? 0 : x1;
    x2 = x2 >= sfc->w ? sfc->w - 1 : x2;

    for (int x=x1; x<=x2; x++)
        grPutPixel(sfc, x, y, color);
}


void grLoadPalette(struct TPalResource *palResource)
{
    if (palResource == NULL)
        fatalError("NULL palette\n");

    for (int i=0; i < 16; i++) {
        ttmPalette[i][0] = palResource->colors[i].b << 2;
        ttmPalette[i][1] = palResource->colors[i].g << 2;
        ttmPalette[i][2] = palResource->colors[i].r << 2;
        ttmPalette[i][3] = 0;
    }
}


// Widen the canvas to the display's aspect ratio, if it's wider than 4:3
static void grInitWidth()
{
    int aspectW = grAspectW;
    int aspectH = grAspectH;
    SDL_DisplayMode mode;

    if (!aspectW || !aspectH) {
        if (SDL_GetDesktopDisplayMode(0, &mode))
            return;
        aspectW = mode.w;
        aspectH = mode.h;
    }

    int width = (SCREEN_HEIGHT * aspectW / aspectH) & ~1;

    // Beyond 638 pixels, there's not enough of the scene to mirror
    if (width > SCREEN_WIDTH)
        grMarginX = (width - SCREEN_WIDTH) / 2 > SCREEN_WIDTH - 2 ? SCREEN_WIDTH - 2 : (width - SCREEN_WIDTH) / 2;

    grWidth = SCREEN_WIDTH + 2 * grMarginX;
}


void graphicsInit()
{
    SDL_Init(SDL_INIT_VIDEO);

    grInitWidth();

    // Full screen at the display's own resolution, rather than switching to
    // a 640x480 mode: the picture is scaled up by the renderer instead
    sdl_window = SDL_CreateWindow(
        "Stranded",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        grWidth,
        SCREEN_HEIGHT,
        (grWindowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP)
    );

    if (sdl_window == NULL)
        fatalError("Could not create window: %s", SDL_GetError());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, 0);

    if (sdl_renderer == NULL)
        fatalError("Could not create renderer: %s", SDL_GetError());

    SDL_RenderSetLogicalSize(sdl_renderer, grWidth, SCREEN_HEIGHT);

    sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGB888,
                                    SDL_TEXTUREACCESS_STREAMING, grWidth, SCREEN_HEIGHT);
    grCanvas = SDL_CreateRGBSurfaceWithFormat(0, grWidth, SCREEN_HEIGHT, 32, SDL_PIXELFORMAT_RGB888);

    if (sdl_texture == NULL || grCanvas == NULL)
        fatalError("Could not create canvas: %s", SDL_GetError());

    SDL_GetRendererOutputSize(sdl_renderer, &grDisplayWidth, &grDisplayHeight);

    grSnapshotLock   = SDL_CreateMutex();
    grSnapshotDone   = SDL_CreateCond();
    grSnapshotPixels = safe_malloc(grWidth * SCREEN_HEIGHT * sizeof(uint32));

    if (!grWindowed)
        SDL_ShowCursor(SDL_DISABLE);

    grPresent();

    grLoadPalette(palResources[0]);  // TODO ?

    srand(time(NULL));

    eventsInit();
}


void graphicsEnd()
{
    SDL_FreeSurface(grCanvas);
    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}


// Show the canvas, scaled to the window
static void grPresent()
{
    SDL_UpdateTexture(sdl_texture, NULL, grCanvas->pixels, grCanvas->pitch);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
}


// Called regularly by the main thread: copy the canvas if a snapshot was
// requested
void grServiceSnapshot()
{
    if (grSnapshotLock == NULL || !grSnapshotRequested)
        return;

    SDL_LockMutex(grSnapshotLock);

    for (int y=0; y < SCREEN_HEIGHT; y++)
        memcpy(grSnapshotPixels + y * grWidth,
               (uint8 *) grCanvas->pixels + y * grCanvas->pitch,
               grWidth * sizeof(uint32));

    grSnapshotRequested = 0;
    grSnapshotCount++;
    SDL_CondBroadcast(grSnapshotDone);
    SDL_UnlockMutex(grSnapshotLock);
}


// From any thread: get a copy of what's on screen, as 0x00RRGGBB pixels,
// to free() afterwards. NULL if the main thread didn't answer in time
uint32 *grTakeSnapshot(int *width, int *height)
{
    uint32 *pixels = NULL;

    if (grSnapshotLock == NULL)
        return NULL;

    SDL_LockMutex(grSnapshotLock);

    uint32 count = grSnapshotCount;
    grSnapshotRequested = 1;

    while (count == grSnapshotCount)
        if (SDL_CondWaitTimeout(grSnapshotDone, grSnapshotLock, 2000) == SDL_MUTEX_TIMEDOUT)
            break;

    if (count != grSnapshotCount) {
        pixels = safe_malloc(grWidth * SCREEN_HEIGHT * sizeof(uint32));
        memcpy(pixels, grSnapshotPixels, grWidth * SCREEN_HEIGHT * sizeof(uint32));
        *width  = grWidth;
        *height = SCREEN_HEIGHT;
    }

    SDL_UnlockMutex(grSnapshotLock);

    return pixels;
}


void grRefreshDisplay()
{
    grPresent();
}


void grToggleFullScreen()
{
    grWindowed = !grWindowed;

    if (grWindowed) {
        SDL_SetWindowFullscreen(sdl_window, 0);
        SDL_ShowCursor(SDL_ENABLE);
    }
    else {
        SDL_SetWindowFullscreen(sdl_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE);
    }

    SDL_GetRendererOutputSize(sdl_renderer, &grDisplayWidth, &grDisplayHeight);

    grPresent();
}


// Switch the monitor's power on or off. Only possible when SDL draws
// directly through KMS/DRM, ie. not in a window under X11 or Wayland
static void grSetDpms(int on)
{
#ifdef USE_DPMS
    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(sdl_window, &info)
            || info.subsystem != SDL_SYSWM_KMSDRM)
        return;

    int fd = info.info.kmsdrm.drm_fd;
    drmModeRes *resources = drmModeGetResources(fd);

    if (resources == NULL)
        return;

    for (int i=0; i < resources->count_connectors; i++) {

        drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);

        if (connector == NULL)
            continue;

        if (connector->connection == DRM_MODE_CONNECTED) {

            for (int j=0; j < connector->count_props; j++) {

                drmModePropertyRes *prop = drmModeGetProperty(fd, connector->props[j]);

                if (prop != NULL && !strcmp(prop->name, "DPMS")) {
                    if (drmModeConnectorSetProperty(fd, connector->connector_id, prop->prop_id,
                            on ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF))
                        fprintf(stderr, "Couldn't set DPMS on connector %d\n", connector->connector_id);
                }

                drmModeFreeProperty(prop);
            }
        }

        drmModeFreeConnector(connector);
    }

    drmModeFreeResources(resources);
#endif
}


// Black out the screen and power the monitor off, keeping a copy of the
// current picture to restore it later
void grDisplayOff()
{
    SDL_Surface *sfc = grCanvas;

    grSavedScreen = SDL_ConvertSurface(sfc, sfc->format, 0);
    SDL_FillRect(sfc, NULL, SDL_MapRGB(sfc->format, 0, 0, 0));
    grPresent();

    grSetDpms(0);
}


void grDisplayOn()
{
    grSetDpms(1);

    if (grSavedScreen != NULL) {
        SDL_BlitSurface(grSavedScreen, NULL, grCanvas, NULL);
        SDL_FreeSurface(grSavedScreen);
        grSavedScreen = NULL;
    }

    grPresent();
}


void grUpdateDisplay(struct TTtmThread *ttmBackgroundThread,
                     struct TTtmThread *ttmThreads,
                     struct TTtmThread *ttmHolidayThread)
{
    SDL_Surface *sfc = grCanvas;

    // Unless the scene is widened, only show its 640x480 part, with black
    // margins
    SDL_Rect sceneRect = { grMarginX, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
    SDL_Rect *srcRect = grWideScene ? NULL : &sceneRect;

    if (!grWideScene && grMarginX) {
        SDL_Rect margin = { 0, 0, grMarginX, SCREEN_HEIGHT };
        SDL_FillRect(sfc, &margin, SDL_MapRGB(sfc->format, 0, 0, 0));
        margin.x = grMarginX + SCREEN_WIDTH;
        SDL_FillRect(sfc, &margin, SDL_MapRGB(sfc->format, 0, 0, 0));
    }

    // Blit the background
    if (grBackgroundSfc != NULL) {
        SDL_Rect dest = { srcRect ? grMarginX : 0, 0, 0, 0 };
        SDL_BlitSurface(grBackgroundSfc, srcRect, sfc, &dest);
    }

    // If not NULL, blit the optional layer of saved zones
    if (grSavedZonesLayer != NULL) {
        SDL_Rect dest = { srcRect ? grMarginX : 0, 0, 0, 0 };
        SDL_BlitSurface(grSavedZonesLayer, srcRect, sfc, &dest);
    }


    // Blit successively each thread's layer
    for (int i=0; i < MAX_TTM_THREADS; i++)
        if (ttmThreads[i].isRunning) {
            SDL_Rect dest = { srcRect ? grMarginX : 0, 0, 0, 0 };
            SDL_BlitSurface(ttmThreads[i].ttmLayer, srcRect, sfc, &dest);
        }

    // Finally, blit the holiday layer
    if (ttmHolidayThread != NULL)
        if (ttmHolidayThread->isRunning) {
            SDL_Rect dest = { srcRect ? grMarginX : 0, 0, 0, 0 };
            SDL_BlitSurface(ttmHolidayThread->ttmLayer, srcRect, sfc, &dest);
        }

    // Wait for the tick ...
    eventsWaitTick(grUpdateDelay);

    // ... and refresh the display
    grPresent();
}


SDL_Surface *grNewLayer()
{
    SDL_Surface *sfc = SDL_CreateRGBSurface(SDL_SWSURFACE, grWidth, 480, 32, 0, 0, 0, 0);
    SDL_Rect dest = { 0, 0, grWidth, 480 };
    SDL_FillRect(sfc, &dest, SDL_MapRGB(sfc->format, 0xa8, 0, 0xa8));
    SDL_SetColorKey(sfc, SDL_TRUE, SDL_MapRGB(sfc->format, 0xa8, 0, 0xa8));

    return sfc;
}


void grFreeLayer(SDL_Surface *sfc)
{
    SDL_FreeSurface(sfc);
}


void grSetClipZone(SDL_Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2)
{
    // In a widened scene, a zone reaching the edge of the 640x480 scene
    // reaches the edge of the widened one, so that sprites aren't cut off
    // in the middle of the ocean
    if (grWideScene && x1 <= 0)
        x1 = -grMarginX;
    if (grWideScene && x2 >= SCREEN_WIDTH - 1)
        x2 = SCREEN_WIDTH + grMarginX;

    x1 += grDx + grMarginX; y1 += grDy;
    x2 += grDx + grMarginX; y2 += grDy;

    SDL_Rect rect = { x1, y1, x2-x1, y2-y1 };
    SDL_SetClipRect(sfc, &rect);
}


void grCopyZoneToBg(SDL_Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    x += grDx + grMarginX; y += grDy;
    SDL_Rect rect = { (short) x, (short) y, width + 2, height };

    if (grSavedZonesLayer == NULL)
        grSavedZonesLayer = grNewLayer();

    SDL_BlitSurface(sfc, &rect, grSavedZonesLayer, &rect);

    // Note : without the +2 in width+2 above, there would be a graphical
    // glitch (2 unfilled pixels) on the hull of the cargo, caused by an
    // error in coordinates in GJIVS6.TTM
    // Obviously, the original soft rounds the SAVE_IMAGE boundaries on
    // one way or another.
}


void grSaveImage1(SDL_Surface *sfc, uint16 arg0, uint16 arg1, uint16 arg2, uint16 arg3) // TODO : rename ?
{
//    ttmSetColors(4,4);
//    ttmDrawRect(arg0,arg1,arg2,arg3);
//    ttmSaveImage0(arg0,arg1,arg2,arg3);
//    ttmUpdate();
}


void grSaveZone(SDL_Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    // Minimalistic implementation: we don't really save the zone,
    // and let grRestoreZone() simply erase the 'saved zones' layer
}


void grRestoreZone(SDL_Surface *sfc, uint16 x, uint16 y, uint16 width, uint16 height)
{
    // In Johnny's TTMs, we never have RESTORE_ZONE called
    // while several zones are saved. So we simply free the
    // whole saved zones layer
    grReleaseSavedLayer();
}


void grDrawPixel(SDL_Surface *sfc, sint16 x, sint16 y, uint8 color)
{
    x += grDx + grMarginX; y += grDy;
    grPutPixel(sfc, x, y, color);
}


void grDrawLine(SDL_Surface *sfc, sint16 x1, sint16 y1, sint16 x2, sint16 y2, uint8 color)
{
    x1 += grDx + grMarginX; y1 += grDy;
    x2 += grDx + grMarginX; y2 += grDy;

    SDL_LockSurface(sfc);

    // Bresenham's line drawing algorithm
    // Note : the code below intends to be pixel-perfect

    uint16 dx, dy, cumul, x, y;
    int xinc, yinc;

    x = x1;
    y = y1;
    dx = abs(x2 - x1);
    dy = abs(y2 - y1);

    xinc = (x2>x1 ? 1 : -1);
    yinc = (y2>y1 ? 1 : -1);

    if (dy < dx) {
        cumul = (dx + 1) >> 1;

        for (int i=0; i < dx; i++) {

            grPutPixel(sfc, x, y, color);

            x += xinc;
            cumul += dy;

            if (cumul > dx) {
                cumul -= dx;
                y += yinc;
            }
        }
    }
    else {
        cumul = (dy + 1) >> 1;

        for (int i=0; i < dy; i++) {

            grPutPixel(sfc, x, y, color);

            y += yinc;
            cumul += dx;

            if (cumul > dy) {
                cumul -= dy;
                x += xinc;
            }
        }
    }

    SDL_UnlockSurface(sfc);
}


void grDrawRect(SDL_Surface *sfc, sint16 x, sint16 y, uint16 width, uint16 height, uint8 color)
{
    x += grDx + grMarginX; y += grDy;

    SDL_Rect dest = { x, y, width, height };
    SDL_FillRect(sfc,
                 &dest,
                 SDL_MapRGB(sfc->format,
                            ttmPalette[color][2],  // TODO ?
                            ttmPalette[color][1],
                            ttmPalette[color][0]
                 )
    );
}


void grDrawCircle(SDL_Surface *sfc, sint16 x1, sint16 y1, uint16 width, uint16 height, uint8 fgColor, uint8 bgColor)
{
    x1 += grDx + grMarginX; y1 += grDy;

    // We can only draw regular circles
    if (width != height) {
        fprintf(stderr, "Warning : grDrawCircle() : unable to draw ellipse\n");
        return;
    }

    // In original data, every width is even
    if (width % 2) {
        fprintf(stderr, "Warning : grDrawCircle() : unable to process odd diameters\n");
        return;
    }

    // Bresenham's circle drawing algorithm
    // Note : the code below intends to be pixel-perfect

    SDL_LockSurface(sfc);

    uint16 r = (width >> 1) - 1;
    uint16 xc = x1 + r;
    uint16 yc = y1 + r;
    sint16 x = 0;
    sint16 y = r;
    int d = 1 - r;

    while (1) {

        grDrawHorizontalLine(sfc, xc-x, xc+x+1, yc+y+1, bgColor);
        grDrawHorizontalLine(sfc, xc-x, xc+x+1, yc-y  , bgColor);

        grDrawHorizontalLine(sfc, xc-y, xc+y+1, yc+x+1, bgColor);
        grDrawHorizontalLine(sfc, xc-y, xc+y+1, yc-x  , bgColor);

        if (y-x <= 1)
            break;

        if (d < 0)
            d += (x << 1) + 3;
        else {
            d += ((x - y) << 1) + 5;
            y--;
        }

        x++;
    }

    if (fgColor != bgColor) {

        x = 0;
        y = r;
        d = 1 - r;

        while (1) {

            grPutPixel(sfc, xc-x  , yc+y+1, fgColor);
            grPutPixel(sfc, xc+x+1, yc+y+1, fgColor);

            grPutPixel(sfc, xc-x  , yc-y  , fgColor);
            grPutPixel(sfc, xc+x+1, yc-y  , fgColor);

            grPutPixel(sfc, xc-y  , yc+x+1, fgColor);
            grPutPixel(sfc, xc+y+1, yc+x+1, fgColor);

            grPutPixel(sfc, xc-y  , yc-x  , fgColor);
            grPutPixel(sfc, xc+y+1, yc-x  , fgColor);

            if (y-x <= 1)
                break;

            if (d < 0)
                d += (x << 1) + 3;
            else {
                d += ((x - y) << 1) + 5;
                y--;
            }

            x++;
        }
    }

    SDL_UnlockSurface(sfc);
}


void grDrawSprite(SDL_Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y, uint16 spriteNo, uint16 imageNo)
{
    if (spriteNo >= ttmSlot->numSprites[imageNo]) {
        fprintf(stderr, "Warning : grDrawSprite(): less than %d sprites loaded in slot %d\n", imageNo, spriteNo);
        return;
    }

    x += grDx + grMarginX; y += grDy;

    SDL_Surface *srcSfc = ttmSlot->sprites[imageNo][spriteNo];

    SDL_Rect dest = { x, y, 0, 0 };
    SDL_BlitSurface(srcSfc, NULL, sfc, &dest);
}


void grDrawSpriteFlip(SDL_Surface *sfc, struct TTtmSlot *ttmSlot, sint16 x, sint16 y, uint16 spriteNo, uint16 imageNo)
{
    if (spriteNo >= ttmSlot->numSprites[imageNo]) {
        fprintf(stderr, "Warning : grDrawSpriteFlip(): less than %d sprites loaded in slot %d\n", imageNo, spriteNo);
        return;
    }

    x += grDx + grMarginX; y += grDy;

    SDL_Surface *srcSfc = ttmSlot->sprites[imageNo][spriteNo];
    x += srcSfc->w - 1;

    for (int i=0; i < srcSfc->w; i++) {

        SDL_Rect src = { i, 0, 1, srcSfc->h };
        SDL_Rect dest = { x - i, y, 0, 0 };

        SDL_BlitSurface(srcSfc, &src, sfc, &dest);
    }
}


void grClearScreen(SDL_Surface *sfc)
{
    SDL_Rect rect;

    SDL_GetClipRect(sfc, &rect);
    SDL_SetClipRect(sfc, NULL);
    SDL_FillRect(sfc, NULL, SDL_MapRGB(sfc->format, 0xa8, 0, 0xa8));
    SDL_SetClipRect(sfc, &rect);
}


void grLoadScreen(char *strArg)
{
    if (grBackgroundSfc != NULL)
        grReleaseScreen();

    if (grSavedZonesLayer != NULL)
        grReleaseSavedLayer();

    struct TScrResource *scrResource = findScrResource(strArg);

    if ((scrResource->width % 2) == 1) {
        fprintf(stderr, "Warning: grLoadScreen(): can't manage odd widths\n");
    }

    if (scrResource->width > 640 || scrResource->height > 480) {
        fatalError("grLoadScreen(): can't manage more than 640x480 resolutions");
    }

    uint16 width  = scrResource->width;
    uint16 height = scrResource->height;
    uint16 sfcWidth = width + 2 * grMarginX;

    uint8 *outData = safe_malloc(sfcWidth * height * sizeof(uint32));
    memset(outData, 0, sfcWidth * height * sizeof(uint32));

    uint8 *inPtr  = scrResource->uncompressedData;

    for (int y=0; y < height; y++) {

        uint8 *outPtr = outData + (y * sfcWidth + grMarginX) * sizeof(uint32);

        for (int x=0; x < width/2; x++) {
            memcpy(outPtr, ttmPalette[(inPtr[0] & 0xf0) >> 4] , 4); outPtr += 4;
            memcpy(outPtr, ttmPalette[(inPtr[0] & 0x0f)     ] , 4); outPtr += 4;
            inPtr++;
        }
    }

    // Only the open ocean scenes are extended: the others have something
    // other than ocean and sky on their edges (eg. a part of the island)
    grWideScene = grMarginX > 0 && grMarginX < width
                  && (!strncmp(strArg, "OCEAN", 5) || !strcmp(strArg, "NIGHT.SCR"));

    // Extend the ocean and sky by mirroring the edges of the scene. Their
    // outermost columns have stray pixels, which would show as a line
    // once mirrored, so they're replaced first
    if (grWideScene) {
        for (int y=0; y < height; y++) {

            uint32 *row   = (uint32 *) outData + y * sfcWidth;
            uint32 *left  = row + grMarginX;
            uint32 *right = row + grMarginX + width - 1;

            left[0]  = left[2];
            right[0] = right[-2];

            for (int k=1; k <= grMarginX; k++) {
                left[-k]  = left[k];
                right[k]  = right[-k];
            }
        }
    }

    grBackgroundSfc = SDL_CreateRGBSurfaceFrom((void*)outData,
                                      sfcWidth, height, 32, 4*sfcWidth, 0, 0, 0, 0);
}


void grInitEmptyBackground()
{
    if (grBackgroundSfc != NULL)
        grReleaseScreen();

    if (grSavedZonesLayer != NULL)
        grReleaseSavedLayer();

    uint8 *data = safe_malloc(grWidth * 480 * sizeof(uint32));
    memset(data, 0, grWidth * 480 * sizeof(uint32));
    grBackgroundSfc = SDL_CreateRGBSurfaceFrom((void*)data,
                                      grWidth, 480, 32, 4*grWidth, 0, 0, 0, 0);
    grWideScene = 0;
}


void grReleaseBmp(struct TTtmSlot *ttmSlot, uint16 bmpSlotNo)
{
    for (int i=0; i < ttmSlot->numSprites[bmpSlotNo]; i++) {
        free(ttmSlot->sprites[bmpSlotNo][i]->pixels);
        SDL_FreeSurface(ttmSlot->sprites[bmpSlotNo][i]);
    }

    ttmSlot->numSprites[bmpSlotNo] = 0;
}


void grLoadBmp(struct TTtmSlot *ttmSlot, uint16 slotNo, char *strArg)
{
    if (ttmSlot->numSprites[slotNo])
        grReleaseBmp(ttmSlot, slotNo);

    struct TBmpResource *bmpResource = findBmpResource(strArg);
    uint8 *inPtr = bmpResource->uncompressedData;

    ttmSlot->numSprites[slotNo] = bmpResource->numImages;

    for (int image=0; image < bmpResource->numImages; image++) {

        if ((bmpResource->widths[image] % 2) == 1)
            fatalError("grLoadBmp(): can't manage odd widths");

        uint16 width  = bmpResource->widths[image];
        uint16 height = bmpResource->heights[image];

        uint8 *outData = safe_malloc(width * height * sizeof(uint32));

        uint8 *outPtr = outData;

        for (int inOffset=0; inOffset < (width*height/2); inOffset++) {
            memcpy(outPtr, ttmPalette[(inPtr[0] & 0xf0) >> 4] , 4); outPtr += 4;
            memcpy(outPtr, ttmPalette[(inPtr[0] & 0x0f)     ] , 4); outPtr += 4;
            inPtr++;
        }

        SDL_Surface *surface = SDL_CreateRGBSurfaceFrom((void*)outData,
                                               width, height, 32, 4*width, 0, 0, 0, 0);
        SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, 0xa8, 0, 0xa8));
        ttmSlot->sprites[slotNo][image] = surface;
    }
}


void grFadeOut()
{
    static int fadeOutType = 0;
    SDL_Surface *sfc = grCanvas;
    SDL_Surface *tmpSfc = grNewLayer();


    // In a widened scene, the margins are faded out too
    int margin = grWideScene ? grMarginX : 0;

    grDx = grDy = 0;

    switch (fadeOutType) {

        // Circle from center
        case 0:
            // Note: we use tmpSfc to be sure we have a 32bpp surface,
            // which is needed by grDrawCircle()
            for (int radius=20; radius <= 400 + margin; radius += 20) {
                grDrawCircle(tmpSfc, 320 - radius, 240 - radius,
                    radius << 1, radius << 1, 5, 5);
                SDL_BlitSurface(tmpSfc, NULL, sfc, NULL);
                eventsWaitTick(1);
                grPresent();
            }
            break;

        // Rectangle from center
        case 1:
            for (int i=1; i <= 20 + (margin + 15) / 16; i++) {
                grDrawRect(sfc, 320 - i*16, 240 - i*12, i*32, i*24, 5);
                eventsWaitTick(1);
                grPresent();
            }
            break;

        // Right to left
        case 2:
            for (int i=600 + (margin + 39) / 40 * 40; i >= -margin; i -= 40) {
                grDrawRect(sfc, i, 0, 40, 480, 5);
                eventsWaitTick(1);
                grPresent();
            }
            break;

        // Left to right
        case 3:
            for (int i=-(margin + 39) / 40 * 40; i < 640 + margin; i += 40) {
                grDrawRect(sfc, i, 0, 40, 480, 5);
                eventsWaitTick(1);
                grPresent();
            }
            break;

        // Middle to left and right
        case 4:
            for (int i=0; i < 320 + margin; i += 20) {
                grDrawRect(sfc, 320+i, 0, 20, 480, 5);
                grDrawRect(sfc, 300-i, 0, 20, 480, 5);
                eventsWaitTick(1);
                grPresent();
            }
            break;
    }

    grFreeLayer(tmpSfc);

    // A next scene requested during the fade out is the one that follows it
    evSkipScene = 0;

    fadeOutType = (fadeOutType + 1) % 5;
}

