# Oma Own Note

A Markdown writing app built with Qt Quick and C++. It follows the desktop dark and light theme.

This repository is a personal fork of https://github.com/omacom-io/omawrite.

Qt settings and crash-recovery files live under `JustNak/oma-own-note`. Files from an upstream install stay where they are.

## Build

You need Qt 6.5 or newer (`qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`), a C++17 compiler, and `xdg-desktop-portal` with a portal backend. `src/systemtheme.cpp` uses `Qt::ColorScheme`, which is not in Qt 6.4.

```
./bin/build
```

The binary is `build/oma-own-note`.

```
./bin/test
```

`bin/test` runs `scripts/check-identity.sh`, then the offscreen Qt tests.

On Arch, `./bin/install` builds the `oma-own-note` package with `makepkg`.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new Oma Own Note window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.
- `Ctrl+=` zooms the writing canvas in. `Ctrl+-` zooms out. `Ctrl+0` resets to 100%.

Unsaved drafts are recovered after an abnormal exit. Oma Own Note also watches open files and warns before an external change can replace local work.

Text follows the desktop text size. On Omarchy that is `omarchy display text size`. Elsewhere it is GNOME's `text-scaling-factor`. The default of 12px leaves the editor at the size it is designed around. Larger and smaller sizes scale from there.

If Omarchy is installed, the app reads `~/.local/state/omarchy/current/theme/colors.toml` and follows theme changes. Without that file it uses the built-in dark and light colors.

## Fonts

The iA Writer Mono font is bundled under the SIL Open Font License 1.1. See `fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on IBM Plex, copyright IBM Corp.
