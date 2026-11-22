# Framely for DaVinci Resolve

**Framely – Picture in Picture** is an OpenFX effect for DaVinci Resolve Studio (tested on 21.2, macOS Apple Silicon).
It's a picture-in-picture that behaves predictably:

- **Centred, whole picture visible** the moment you apply it. No Inspector zoom-out workaround.
- **No pixel values.** Every number is a % of the screen or of each image, so results don't depend on the timeline or
  photo resolution, and settings pasted between clips of different shapes behave predictably.
- **Measure from: Screen / Each image**, with a per-section override.
- **Size by** Fit in box (screen) or Scale (image) · Position · optional **safe area** with separate margins.
- **Crop**, then **Zoom/Pan** inside the window (crop never moves the image).
- **Roundness** 0–100% (fully rounded ends) plus **Circle** 0–100%.
- **Border** inside or outside · **Drop shadow** (opacity, distance, softness, size, angle, colour).
  Turning border or shadow on/off never moves or resizes the picture. Edges are sharp and anti-aliased, never feathered.
- **Guides** in the viewer only (OpenFX Overlay): ghost outlines for 9:16, 4:5, 1:1, 4:3, 16:9 and a custom ratio,
  centre crosshair/dot, box and safe-area outlines, and a "no room" warning. Guides never appear in renders.
- **Save as Default / Reset to Default** at the top, a reset button per section, and Resolve's ↺ per value.
- Drag the picture in the viewer (OpenFX Overlay); hold Option to pan the image inside.

## Install (macOS)

Copy `Framely.ofx.bundle` into `/Library/OFX/Plugins/` (admin password needed) and restart Resolve.
It appears under **Effects › Open FX › Framely**; searching "Picture in Picture" finds it.

## Build

Needs Xcode command line tools. No CMake.

```bash
make            # builds build/Framely.ofx.bundle (arm64)
make install    # copies into /Library/OFX/Plugins/Framely.ofx.bundle (that folder must be writable by you)
make harness    # command-line renderer used for tests
```

## How it's tested

- `src/FramelyCore.h` holds all geometry and pixel maths with no OFX dependency. Its geometry is checked against the
  original interactive spec on 400 random cases (`tools/mockcheck/mockgeom.js`, run with Deno).
- `tools/harness.cpp` renders the same maths to PNG. Frames rendered by Resolve itself (render queue, PNG) are compared
  pixel by pixel with the harness: 9 setting presets × portrait/square/landscape × 4K/HD/vertical (`tools/compare.py`).

## Notes

- The saved default lives in `~/Library/Application Support/Framely/defaults.txt`.
- Uses the OpenFX SDK 1.5.1 (BSD-3-Clause), vendored in `vendor/openfx` with one small patch (search `FRAMELY PATCH`).
