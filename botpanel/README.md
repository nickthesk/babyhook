# Cat botpanel setup

This botpanel is bundled inside the main Cat repository. Do not clone separate
`catbot-ipc-server`, `catbot-ipc-web-panel`, or `cathook` repositories.

```sh
git clone https://github.com/pupnoodle/cathook
cd cathook
./setup.sh
```

Next, edit `botpanel/accounts.txt` and add bot accounts in this format:

USERNAME:PASSWORD
USERNAME:PASSWORD
USERNAME:PASSWORD

# Cat botpanel launcher

`./botpanel/start` asks which headless bot display backend to use each time
when both xpra and Xvfb are installed. The choice is not saved between runs.
Choosing xpra stops the matching Xvfb display first; choosing Xvfb stops the
matching xpra display first.

- Default display: `:699`
- Override display: `CAT_DISPLAY=:700 ./botpanel/start`
- Xpra-specific override: `CAT_XPRA_DISPLAY=:700 ./botpanel/start`
- Legacy Xvfb override still accepted: `CAT_XVFB_DISPLAY=:700 ./botpanel/start`
- Xvfb client limit: `CAT_XVFB_MAX_CLIENTS=512 ./botpanel/start` (default `512`; accepted values are `64`, `128`, `256`, `512`)
- Override hidden TF2 window flags: `CAT_GAME_WINDOW_OPTIONS="-gl -silent -sw -w 800 -h 600" ./botpanel/start`
- Use an existing desktop display instead: `CAT_VISIBLE_WINDOWS=1 ./botpanel/start`
- After game IPC has stayed connected for 10 seconds, the panel freezes the main `steamwebhelper` in that bot's Steam process tree and kills its child helper processes.
- Disable helper cleanup: `CAT_STEAMWEBHELPER_CLEANUP=0 ./botpanel/start`
- Override helper cleanup delay: `CAT_STEAMWEBHELPER_CLEANUP_SECONDS=15 ./botpanel/start`
- Optional ban tracker API key: `CAT_STEAM_WEB_API_KEY=... ./botpanel/start`; without it, the panel falls back to Steam Community profile HTML checks.
- Host Steam content is shared at `/opt/steamapps`. Botpanel bind-mounts that path to the detected host Steam `steamapps` directory, then bot instances symlink their own `steamapps` directory to it. Debian/Ubuntu and Arch Steam layouts are checked.
- The host Steam path is detected automatically. If detection fails, the launcher prints and writes `/tmp/cat-steamapps-detect.log` with every checked path.

`./botpanel/stop` stops the matching headless display unless `CAT_VISIBLE_WINDOWS=1` is set.

`./botpanel/fix_permissions` continuously repairs botpanel, `/opt/cathook`,
runtime `.so` files including SDL/Steam API libraries, `/opt/steamapps`,
detected Steam/TF2 paths, and `botpanel/accounts.txt` permissions. It runs once
immediately and then every second; use `./botpanel/fix_permissions --once` for a
single pass. It logs each repair pass by default; add `-silent` to suppress
terminal output.

`./botpanel/fix-oldshi` repairs old botpanel path layouts that created partial
Steam directories or recursive `/opt/steamapps` symlinks. Stop the panel first;
the script refuses to touch live bot paths unless `CAT_FIX_OLD_PANEL_FORCE=1` is
set.

`./botpanel/update` updates this single repository, installs dependencies,
builds Cat default/textmode libraries, builds the bundled IPC server, installs
web panel npm dependencies, and refreshes navmeshes in `/opt/cathook/navmeshes`.
When TF2 is installed it also refreshes `/opt/steamapps`; set
`CAT_UPDATE_HOST_NAVMESHES=1` to copy navmeshes directly through the host Steam
path instead.
