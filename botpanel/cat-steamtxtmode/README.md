# cat-steamtxtmode

`cat-steamtxtmode` is an `LD_PRELOAD` shim for old Steam `-vgui` bot sessions.

It keeps Steam alive while making the native client close to headless:

- suppresses X11 window mapping/raising and shrinks created windows
- drops OpenGL/EGL/SDL draw and present calls
- forces swap interval `0`
- blocks OpenAL/ALSA device opens
- sleeps tight zero-timeout wait loops in `steam` and `steamwebhelper`
- trims any `steamwebhelper` process that still starts

Build and install:

```sh
cd botpanel/cat-steamtxtmode
sudo ./install.sh
```

Runtime preload:

```sh
STEAM_LD_PRELOAD=libcatsteamtxtmode.so
```

The bot panel launches Steam with `-vgui` by default and warns when the selected Steam package version is not `1689034492`.

Environment:

| var | default | effect |
| --- | --- | --- |
| `CAT_STM_DISABLE` | `0` | disable the shim |
| `CAT_STM_HIDE_X11` | `1` | suppress and shrink X11 windows |
| `CAT_STM_NO_GL` | `1` | drop GL/EGL/SDL draw and present calls |
| `CAT_STM_NO_VSYNC` | `1` | force swap interval `0` |
| `CAT_STM_NO_AUDIO` | `1` | block OpenAL/ALSA device open |
| `CAT_STM_WEBHELPER_TRIM` | `1` | inject low-memory webhelper switches |
| `CAT_STM_WEBHELPER_SINGLE` | `1` | add `--single-process` |
| `CAT_STM_LOOP_SLEEP` | `1` | sleep before zero-timeout waits |
| `CAT_STM_LOOP_SLEEP_US` | `5000` | microseconds to sleep before zero-timeout waits |
| `CAT_STM_LOG` | `1` | write shim logs to stderr |
| `CAT_STEAM_VGUI` | `1` | launch Steam with `-vgui` from the bot panel |
| `CAT_STEAM_VGUI_TARGET_VERSION` | `1689034492` | downgraded Steam version expected by the bot panel |
