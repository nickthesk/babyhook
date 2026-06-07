# cat-steamtxtmode

`cat-steamtxtmode` is an `LD_PRELOAD` shim for headless Steam bot sessions.

It does three things:

- rewrites top-level `steamwebhelper` argv with lower-memory CEF switches
- drops present/vsync/audio calls that headless bots do not need
- applies optional signature patches from `src/steam_patches.cpp`

Build and install:

```sh
cd botpanel/cat-steamtxtmode
sudo ./install.sh
```

Runtime preload:

```sh
STEAM_LD_PRELOAD=libcatsteamtxtmode.so
```

Environment:

| var | default | effect |
| --- | --- | --- |
| `CAT_STM_DISABLE` | `0` | disable the shim |
| `CAT_STM_WEBHELPER_TRIM` | `1` | inject CEF switches |
| `CAT_STM_WEBHELPER_SINGLE` | `0` | add `--single-process` |
| `CAT_STM_NO_VSYNC` | `1` | force swap interval `0` |
| `CAT_STM_NO_AUDIO` | `1` | block OpenAL/ALSA device open |
| `CAT_STM_NO_PRESENT` | `1` | drop buffer swaps |
| `CAT_STM_PRESENT_FPS` | `0` | rate-limit swaps instead of dropping all |
| `CAT_STM_PATCHES` | `1` | apply signature patches |
| `CAT_STM_LOG` | `1` | write shim logs to stderr |
