# EWallCycle

A minimal, fast wallpaper cycler for [sway](https://swaywm.org/) using [swaybg](https://github.com/swaywm/swaybg). Written in C, about 400 lines, no dependencies beyond libc and `swaybg`.

Project page: <https://github.com/el10ta1d3rs0n/EWallCycle>

The command is called `ewallcycle`.

There is no daemon and no GUI. Each keypress runs the program once, it swaps the wallpaper, and it exits.

## Features

- Cycle wallpapers with a keyboard shortcut: next, previous, or random
- Natural sort order (`img2.png` comes before `img10.png`)
- Remembers the last wallpaper and restores it after a restart or shutdown
- Flicker-free switching: the new `swaybg` starts before the old one is stopped
- Fill modes: `fill`, `fit`, `stretch`, `center`, `tile`
- Tiny state file, written atomically

## Requirements

- sway (any Wayland compositor that works with `swaybg` should also work)
- `swaybg`
- `gcc` to build (Arch: `base-devel`)

## Install

### Arch Linux (PKGBUILD)

You only need the `PKGBUILD` file. It downloads the source from GitHub by itself, so you do not need to download `ewallcycle.c`.

```sh
mkdir ewallcycle && cd ewallcycle
curl -O https://raw.githubusercontent.com/el10ta1d3rs0n/EWallCycle/main/PKGBUILD
makepkg -si
```

This installs the binary to `/usr/bin/ewallcycle`. To update later, run `makepkg -si` again in the same folder.

### Manual

Clone the repository and build it yourself:

```sh
git clone https://github.com/el10ta1d3rs0n/EWallCycle.git
cd EWallCycle
sudo pacman -S --needed swaybg base-devel
gcc -O2 -Wall -Wextra -o ewallcycle ewallcycle.c
strip ewallcycle
install -Dm755 ewallcycle ~/.local/bin/ewallcycle
```

Make sure `~/.local/bin` is in your `PATH`.

## Quick start

Set your wallpaper folder once. This picks the first image and applies it:

```sh
ewallcycle set-dir ~/Pictures/Wallpapers
```

After that, use `next`, `prev` or `random`.

## Usage

```
ewallcycle set-dir <path>   set wallpaper folder and apply first image
ewallcycle next             next wallpaper
ewallcycle prev             previous wallpaper
ewallcycle random           random wallpaper
ewallcycle restore          reapply the last wallpaper (use at login)
ewallcycle mode <m>         fill | fit | stretch | center | tile
ewallcycle current          print the current wallpaper path
```

## Sway configuration

Add to `~/.config/sway/config`:

```
exec ewallcycle restore

bindsym $mod+w        exec ewallcycle next
bindsym $mod+Shift+w  exec ewallcycle prev
bindsym $mod+Ctrl+w   exec ewallcycle random
```

Then reload with `swaymsg reload`.

**Important:** remove any existing `output * bg ...` line or `exec swaybg ...` line from your config, otherwise it will conflict with ewallcycle.

Use `exec`, not `exec_always`, for the restore line. With `exec_always` it would run again on every config reload (harmless, but redundant).

## How it works

- The wallpaper folder is scanned each time you cycle, so you can add or remove images without any extra step.
- Only regular files directly inside the folder are used. Subfolders and hidden files are ignored.
- Supported extensions: `jpg`, `jpeg`, `png`, `webp`, `bmp` (case-insensitive). What `swaybg` can actually display depends on how it was built.
- Wallpapers are applied to all outputs (`-o '*'`).
- The PID of the running `swaybg` is stored in `$XDG_RUNTIME_DIR/ewallcycle.pid`. Before killing the old process, ewallcycle checks that the PID really belongs to `swaybg`.

### State file

State is saved in `$XDG_CONFIG_HOME/ewallcycle/state` (usually `~/.config/ewallcycle/state`) as plain text:

```
dir=/home/you/Pictures/Wallpapers
current=/home/you/Pictures/Wallpapers/forest.jpg
mode=fill
```

If the saved wallpaper no longer exists when you run `restore`, ewallcycle falls back to the first image in the folder.

## Troubleshooting

| Problem | Fix |
|---|---|
| `no wallpaper directory set` | Run `ewallcycle set-dir <path>` once. |
| `no images found in ...` | Check the folder contains files with a supported extension. |
| Wallpaper doesn't change | Make sure no other `swaybg` or `output * bg` setting is in your sway config. |
| Brief flash when switching large images | Increase the 300 ms delay in `apply()` in the source. |
| Image is not shown | Your `swaybg` build may not support that format. Try png or jpg. |
| Shortcut does nothing | Run `ewallcycle next` in a terminal and read the error message. |

## License

MIT. See the `LICENSE` file.
