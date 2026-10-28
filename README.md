# Johnny Reborn

Johnny Reborn is an open source engine for the classic Johnny Castaway screen saver, developed by Dynamix for Windows 3.x and published by Sierra, back in 1992.

It is written in C using the SDL2 library, and was successfully compiled and tested on Linux as well as Windows (MinGW), both 32 and 64 bits.


## How to install

For the screen saver to work, you'll need the two data files from the original
software: `RESOURCE.MAP` and `RESOURCE.001`.

Optionnaly, if you want sounds, you'll need some extra files as well. You can get them [here](https://github.com/nivs1978/Johnny-Castaway-Open-Source/tree/master/JCOS/Resources), from the github repository of the "JCOS" project - another port of the scrantics engine.

As a result, here is what your directory should look like:

 | File name    | size (bytes) | md5                              |
 | -------------|--------------|----------------------------------|
 | RESOURCE.MAP |         1461 | 8bb6c99e9129806b5089a39d24228a36 |
 | RESOURCE.001 |      1175645 | 374e6d05c5e0acd88fb5af748948c899 |
 | sound0.wav   |        10768 | 53695b0df262c2a8772f69b95fd89463 |
 | sound1.wav   |        11264 | 35d08fdf2b29fc784cbec78b1fe9a7f2 |
 | sound2.wav   |         1536 | f93710cc6f70633393423a8a152a2c85 |
 | sound3.wav   |         7680 | 05a08cd60579e3ebcf26d650a185df25 |
 | sound4.wav   |         5120 | be4dff1a2a8e0fc612993280df721e0d |
 | sound5.wav   |         3072 | 24deaef44c8b5bb84678978564818103 |
 | sound6.wav   |        15872 | eb1055b6cf3d6d7361e9a00e8b088036 |
 | sound7.wav   |        15360 | cab94bace3ef401238daded2e2acec34 |
 | sound8.wav   |         2560 | 39515446ceb703084d446bd3c64bfbb0 |
 | sound9.wav   |         3584 | f86d5ce3a43cbe56a8af996427d5c173 |
 | sound10.wav  |        20480 | 5b8535f625094aa491bf8e6246342c77 |
 | sound12.wav  |         5632 | 8c173a95da644082e573a0a67ee6d6a3 |
 | sound14.wav  |        11776 | e064634cfb9125889ce06314ca01a1ea |
 | sound15.wav  |         3072 | b3db873332dda51e925533c009352c90 |
 | sound16.wav  |         7680 | 2eabfe83958db0cad77a3a9492d65fe7 |
 | sound17.wav  |         4608 | 2497d51f0e1da6b000dae82090531008 |
 | sound18.wav  |        14336 | 994a5d06f9ff416215f1874bc330e769 |
 | sound19.wav  |         3584 | 5e9cb5a08f39cf555c9662d921a0fed7 |
 | sound20.wav  |         7680 | 80e7eb0e0c384a51e642e982446fcf1d |
 | sound21.wav  |         5120 | 1a3ab0c7cec89d7d1cd620abdd161d91 |
 | sound22.wav  |         1536 | a0f4179f4877cf49122cd87ac7908a1e |
 | sound23.wav  |         2048 | 52fc04e523af3b28c4c6758cdbcafb84 |
 | sound24.wav  |         9728 | 5a6696cda2a07969522ac62db3e66757 |

All files must be copied in the same directory as the `jc_reborn` executable.


## How to run

By default, the engine runs full screen and plays the life of Johnny on his island, as the original did.

To control it from a browser or turn the display off at night, see [Web control and night mode](#web-control-and-night-mode) below.

But, if you're curious about how the engine works, you may type `jc_reborn help` and see the different options available. Feel free to try them and explore the inner workings of Screen Antics !


## Web control and night mode

Run with the `web` option to start a small web server on port 80 (or `port <n>` to pick another port):

    jc_reborn hotkeys web

Open `http://<host>/` for a settings page. It serves `webui.html` from the current directory, so keep that file next to the `jc_reborn` executable. Listening on port 80 needs root, or the `CAP_NET_BIND_SERVICE` capability (for a systemd service: `AmbientCapabilities=CAP_NET_BIND_SERVICE`). If the port can't be opened, a warning is printed and Johnny plays on without the web server.

There is no authentication: anyone who can reach the port can change the settings or quit. Only use it on a network you trust.

### Night mode

When night mode is on, Johnny pauses during the night hours (22:00 to 08:00 by default) and the display is turned off:

  - the screen is blacked out;
  - when drawing directly through KMS/DRM (no desktop), the monitor is powered off with DPMS. This needs `libdrm` at build time, and is detected automatically by `Makefile.linux`;
  - optionally, a TV is put in standby with HDMI-CEC, using `cec-ctl` (from `v4l-utils`) on `/dev/cec0`.

In the morning, the display comes back on and Johnny picks up where he left off. The display can also be turned off or on by hand; that lasts until the next scheduled switch.

### Settings

Settings changed from the web page are saved in `~/.jc_reborn`, next to the story progress, and take precedence over the command line options at the next start.

| Setting      | Values           | Meaning |
| -------------|------------------|---------|
| `storyDay`   | 1 to 11          | Day of the story. It normally advances by one each day; a new value takes effect at the next scene |
| `sound`      | `true` / `false` | Play sounds. Without effect if started with `nosound` |
| `hotkeys`    | `true` / `false` | Enable hot keys. When off, any key press quits |
| `debug`      | `true` / `false` | Print debug info. Not saved |
| `nightMode`  | `true` / `false` | Turn the display off during night hours |
| `nightStart` | `HH:MM`          | Start of the night, local time |
| `nightEnd`   | `HH:MM`          | End of the night, local time |
| `cec`        | `true` / `false` | Also put the TV in standby with HDMI-CEC |

### API

| Request | Effect |
| --------|--------|
| `GET /api/status` | Current state and settings, as JSON |
| `POST /api/settings` | Change one or more settings. Body: form-encoded, eg. `nightMode=true&nightStart=22:00`. Either all of them are applied or, on error, none |
| `POST /api/pause`, `POST /api/resume`, `POST /api/toggle-pause` | Pause or resume (hot key: Space) |
| `POST /api/step` | When paused, advance one frame (hot key: Return) |
| `POST /api/toggle-maxspeed` | Toggle max / normal speed (hot key: M) |
| `POST /api/toggle-fullscreen` | Toggle full screen / windowed mode (hot key: Alt+Return) |
| `POST /api/quit` | Terminate (hot key: Esc) |
| `POST /api/sleep`, `POST /api/wake` | Turn the display off or on now, until the next scheduled switch |
| `POST /api/auto` | Cancel a manual sleep or wake, and follow the night schedule |

Every request except `quit` returns the same JSON as `/api/status`; errors return a 4xx status with `{"error": "..."}`. For example:

    curl http://pijohnny/api/status
    curl -d nightMode=true -d nightStart=22:00 -d nightEnd=08:00 http://pijohnny/api/settings
    curl -X POST http://pijohnny/api/toggle-pause


## Current status

### Short version
Currently, Johnny reborn is in "work in progress" state. Even so, as of January 2020, every scene works with only some inaccuracies here and there.

That means that the engine globally works in an acceptable way, but more needs to be done to fully understand a number of details and faithfully reproduce the behaviour of the original engine.

### Long version

Some great work was already made by Hans Milling for "Johnny Castaway Open Source" (JCOS), back in 2015. This includes:
  - the parsing and decoding of all the data files
  - the understanding of many instructions which constitute TTM and ADS scripts

What Johnny Reborn brings is:
  - the understanding of nearly every TTM and ADS instruction, and their parameters
  - the algorithm for Johnny to transitionnaly walk from scene to scene was implemented. Accuracy is not bad though not perfect.
  - the same can be said about the algorithm which randomly chooses scenes to be played.
  - as well as the algorithm which draws the island at a random place, clouds, etc.
  - all the work was made by observing the behaviour of the original software and trying to reproduce it as accurately as possible. For a better result, a complete disassembly of the original exe may be necessary - but wasn't done to this point.


## Thanks

I never would have been able to write Johnny Reborn without, directly or indirectly, all the people listed below. Many thanks to them.

  - Hans Milling aka nivs1978, author of the JCOS project - my main source of info for my first lines of the Johnny Reborn code
    - https://github.com/nivs1978/Johnny-Castaway-Open-Source
    - http://nivs.dk/jc/
  - Alexandre Fontoura aka xesf, author of castaway project - similar to Johnny Reborn, but in Javascript
    - https://github.com/xesf/castaway
    - https://castaway.xesf.net/viewer/
  - The Sierra Chest website, which has a nice section about Johnny Castaway, with many screenshots and video captures. Those turned out to be quite helpful:
    - http://sierrachest.com/index.php?a=games&id=255&title=johnny-castaway

Hans Milling thanks a number of people for giving him (or helping him find) some info about the original engine. They should not be forgotten, and I thank them - indirectly - as well:

  - Jeff Tunnel - For helping getting in contact with some of the original developers
  - Kevin and Liam Ryan - Assisting with information about the resource files
  - Jaap - Helping in finding the format of the resource files
  - Gregori - Assisting with the Lempel-Ziv decompression
  - Guido - The author of the xBaK project that led to understanding the TTM and ADS commands.


