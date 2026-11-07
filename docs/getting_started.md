# Getting started — the first five minutes

<!-- //ai: newcomer front door; the fresh-install rule is defined in main_load.c. -->

gX is a raylib-only game engine plus authoring tools and demo scenes. It is
not a game: story/game content is excluded, and Battle plus the gX-240
cartridges are engine demos. The public beta is for engine developers and
technical artists evaluating the renderer, GUI and tooling.

## 1. Build

```bash
./build.sh
./build/gX_build --test-only   # optional, about 2 s
```

Requirements and platform notes are in [`platforms.md`](platforms.md). The
public fast CI is `make check-fast` (about two minutes and needs a display for
the scene smokes).

## 2. Choose a front door

```bash
./build/gX_build --title          # front end (title menu)
./build/gX_build --gfx            # GFX renderer demo
./build/gX_build --console-shell  # gX-240 console boot view
./build/gX_build --showcase-y2k   # Y2K showcase
```

A genuinely fresh tree with **no** `resources/settings.json` and **no** save
opens the GFX renderer demo on a plain `./build/gX_build`. That is intentional:
the engine shows a live renderer immediately. Use `--title` or
`GX_SKIPTITLE=0` for the front end, or press **Esc** and choose **Back to
Title**. Any returning user with a settings file or save gets the title.

## 3. What to try

- **Title:** Resume, Run Demo, Load Save, Settings, Demos, Extras and Quit.
  The title footer lists the current navigation keys.
- **Demos:** Battle, the GFX showcase, the procedural background and the
  raymarch demo; the gX-240 console shell is also reachable from the Demos hub.
- **Settings:** Basic/Advanced/Expert levels, display/video, accessibility,
  audio, UI theme, controls and key bindings.
- **GFX editor:** `--gfx-editor` or the EXTRAS card. It is a technical preview;
  expect layout and workflow changes in future betas.
- **Console shell:** `--console-shell` boots the fictional gX-240 shell with
  cartridge slots, video output and save states.

## 4. Settings and saves

`resources/settings.json` is gitignored and does not exist in a source archive.
It is created when you Save settings. The `save/` directory appears when the
save-slot system writes a slot. Deleting the settings file changes the
fresh-install rule above, so keep or remove it deliberately.

## 5. Help and troubleshooting

- `./build/gX_build --help` lists every flag, including scene shortcuts, tools
  and audit hooks. It is a successful command and exits 0.
- The documentation index is [`docs/README.md`](README.md).
- Report bugs and feature requests through
  <https://github.com/lanttusoft/gX/issues> using the templates in
  [`SUPPORT.md`](../SUPPORT.md); include the version/commit, OS, GPU/driver,
  build mode and reproduction steps.
- Security reports use the private advisory route in
  [`SECURITY.md`](../SECURITY.md).

## 6. Headless captures and audits

For screenshots and audit runs:

```bash
GX_AUDIT_WINDOW=hidden GX_FRAMES=240 \
  GX_SCREENSHOT_DIR=/tmp/gx-shots \
  ./build/gX_build --title --ai-screenshot-final
```

`GX_FRAMES` controls when the process exits; it must exceed the capture frame
(180 by default). A shorter run can exit successfully without writing a
screenshot. `GX_SCREENSHOT_DIR` redirects the output; the default is
`screenshot/` relative to the working directory. `GX_SCRATCH` redirects
gate/audit scratch data, not the screenshot directory.
