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

`./botpanel/start` starts the headless bot display with xpra by default.

- Default display: `:100`
- Override display: `CAT_XPRA_DISPLAY=:101 ./botpanel/start`
- Legacy override still accepted: `CAT_XVFB_DISPLAY=:101 ./botpanel/start`
- Use an existing desktop display instead: `CAT_VISIBLE_WINDOWS=1 ./botpanel/start`
- After game IPC has stayed connected for 10 seconds, the panel freezes the main `steamwebhelper` in that bot's Steam process tree and kills its child helper processes.
- Disable helper cleanup: `CAT_STEAMWEBHELPER_CLEANUP=0 ./botpanel/start`
- Override helper cleanup delay: `CAT_STEAMWEBHELPER_CLEANUP_SECONDS=15 ./botpanel/start`
- Optional ban tracker API key: `CAT_STEAM_WEB_API_KEY=... ./botpanel/start`; without it, the panel falls back to Steam Community profile HTML checks.
- Host Steam content is protected by mounting `steamapps` through an overlay at `/opt/steamapps`; if overlayfs is unavailable the launcher falls back to a read-only bind mount.

`./botpanel/stop` stops the matching xpra display unless `CAT_VISIBLE_WINDOWS=1` is set.

`./botpanel/update` updates this single repository, installs dependencies,
builds Cat default/textmode libraries, builds the bundled IPC server, installs
web panel npm dependencies, and refreshes navmeshes in the botpanel steamapps
overlay when TF2 is installed. Set `CAT_UPDATE_HOST_NAVMESHES=1` only if you
explicitly want navmeshes copied into the host TF2 maps directory.
