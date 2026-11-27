# Applications folder / Папка приложений

A launcher is a small text file `NAME.app`; HomeServer OS lists every launcher from two places:

| Folder | Purpose |
|---|---|
| `/usr/share/hs/apps/` | built-in launchers (copied from `apps/launchers/` in this repository at build time) |
| `/etc/hs/apps/` | your own launchers and the ones created by `hs-get` when you install a program |

```
NAME=My program         shown in the launcher
CMD=my-program --flag   command to run
DESC=What it does       one line (optional)
TERM=1                  1 = run inside a terminal window / the console, 0 = needs the whole screen (graphical program)
```

Where they appear: menu **Applications → My apps**, the desktop start menu **Apps**, and the command `hs-launch`.
Programs installed with `hs-get` / the catalog get a launcher automatically and lose it again on removal.
To add a program by hand: put the binary in `/opt/apps/NAME/`, link it into `/usr/local/bin`, add `/etc/hs/apps/NAME.app`.
Native desktop apps (drawn by `hs-gui`) use the API in `src/hs-app.h`; see `src/hs-app-3d.h` for an example.
