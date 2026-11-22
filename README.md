# Framely for DaVinci Resolve

**Framely – Picture in Picture** is an OpenFX effect for DaVinci Resolve Studio (tested on 21.2, macOS Apple Silicon).
It's a picture-in-picture that behaves predictably:

- **Centred, whole picture visible** the moment you apply it. No Inspector zoom-out workaround.
- **No pixel values.** Every number is a % of the screen or of each image, so results don't depend on the timeline or
  photo resolution, and settings pasted between clips of different shapes behave predictably.
- **Sizing: Fixed on screen / Scales with picture** for crop, corners, border and shadow, with a per-section override.
- **Size by** Fit in box (screen) or Scale (image) · Position · optional **safe area** with separate margins.
- **Crop**, then **Zoom/Pan** inside the window (crop never moves the image).
- **Roundness** 0–100% (fully rounded ends) plus **Circle** 0–100%.
- **Border** inside or outside · **Drop shadow** (opacity, distance, softness, size, angle, colour).
  Turning border or shadow on/off never moves or resizes the picture. Edges are sharp and anti-aliased, never feathered.
- **Guides** in the viewer only (OpenFX Overlay): ghost outlines for 9:16, 4:5, 1:1, 4:3, 16:9 and a custom ratio,
  centre crosshair/dot, box and safe-area outlines, and a "no room" warning. Guides never appear in renders.
- **Save as Default / Reset to Default** at the top, a reset button per section, and Resolve's ↺ per value.
- Drag the picture in the viewer (OpenFX Overlay); hold Option to pan the image inside.

> **Alpha.** This is an early public build (see Releases for the current version). Expect rough edges; please report issues.

## Install (macOS)

1. Download `Framely-macOS.zip` from [Releases](https://github.com/open-luc/Framely-for-DaVinci/releases) and unzip it.
2. Move `Framely.ofx.bundle` into `/Library/OFX/Plugins/` (Finder asks for your password).
3. Restart Resolve. Framely is under **Effects › Open FX › Framely**; searching "Picture in Picture" finds it.

Works on Apple Silicon and Intel Macs.

## Updating

Open the effect's **Updates** section in the Inspector:
**Check for Updates** looks on GitHub, **Install Update** downloads and installs the new version (macOS may ask for your
password), then restart Resolve. **Open Releases Page** opens this repo's downloads. Framely also checks quietly once a
day and renames the section to "Updates – vX.Y.Z available" when there's something new.

## Saved default

**Save as Default** stores the current settings; new copies of the effect start from them, and **Reset to Default** and
each section's reset button go back to them. Resolve's own ↺ arrow on each value goes back to the built-in factory value,
so changing your saved default never changes clips in older projects.

## Build

Needs Xcode command line tools. No CMake.

```bash
make            # builds build/Framely.ofx.bundle (arm64)
make dist       # universal (arm64 + x86_64) dist/Framely-macOS.zip
make install    # copies into /Library/OFX/Plugins/Framely.ofx.bundle (that folder must be writable by you)
make harness    # command-line renderer used for tests
```

## How it's tested

- `src/FramelyCore.h` holds all geometry and pixel maths with no OFX dependency. Its geometry is checked against the
  original interactive spec on 400 random cases (`tools/mockcheck/mockgeom.js`, run with Deno).
- `tools/harness.cpp` renders the same maths to PNG. Frames rendered by Resolve itself (render queue, PNG) are compared
  pixel by pixel with the harness: 9 setting presets × portrait/square/landscape × 4K/HD/vertical (`tools/compare.py`).

## Known issues (alpha)

- After you change the viewer zoom, Resolve sometimes keeps drawing the guides at the old zoom (too big, Centre marker in
  the wrong place). Resizing the viewer, or changing the zoom again, redraws them correctly. The plugin draws in the
  coordinates Resolve gives it, so this looks like a Resolve issue (still investigating).
- Resolve doesn't tell plugins when you open or close a section, so sections can't remember your last choice between
  clips. Guides starts collapsed; the rest start open.
- A copy of the effect you never touch follows a later change of your saved default after Resolve restarts. Touch any
  setting to lock a clip.

## Notes

- The saved default lives in `~/Library/Application Support/Framely/defaults.txt`.
- Uses the OpenFX SDK 1.5.1 (BSD-3-Clause), vendored in `vendor/openfx` with one small patch (search `FRAMELY PATCH`).
