# Recovery interface

The Extra tab groups the added recovery tools into Display and input, Storage,
Backup and restore, Linux and boot, Files, and Diagnostics. Its rows use the
active OrangeFox colors, rounded group cards and trailing navigation arrows.
Explanations below each entry describe the next action before opening a tool.
Storage and diagnostics separate inspection from maintenance and recovery.

Interface scale uses a selection followed by Apply. Compact (50%), Balanced
(75%) and Large (100%) are presets; custom values from 50% through 100% remain
available. Selecting, resetting or loading a size does not resize the interface.
Apply validates the selection and queues the existing render-thread theme reload.
Saving records the applied size in an already mounted private directory. A RAM
directory remains volatile; the interface does not mount storage to save a size.

Five footer targets share the full viewport. The gesture indicator remains
centered inside its own bottom strip. Menu labels reserve a scaled icon column,
a scaled gap and a bounded trailing margin. Descriptions use a smaller font;
existing OrangeFox rows without a secondary font retain their font fallback.

## Icon provenance

The added monochrome icons are from [Lucide](https://lucide.dev/), release
0.563.0, immutable revision `e9e060a851a44ab9c33e296caeec9c6c778c51d1`.
Lucide is ISC licensed, with retained MIT notices for Feather portions. The
original license ships in the recovery payload and source archive. Each SVG and
72 by 72 RGBA PNG has a checksum in `src/device/xiaomi/uke/ui-icons/ORIGINS.json`.
OrangeFox applies the current theme color at runtime. The selected Extra icon
uses white inside the stock accent pill.

Original SVGs remain under `referances/lucide-0.563.0/`. The host-only
`scripts/check-ui-icons.sh` checks source and PNG identities, then re-renders
the SVGs with ImageMagick/RSVG and compares RGBA pixels. Committed PNG bytes
include their original metadata; pixel verification ignores metadata timestamps.
ImageMagick 7.1.2-32 and RSVG 2.63.2 were used for the reviewed rasters. No SVG
engine or host raster tool is added to the tablet.

## Validation scope

`tests/check-menu-rendering.sh` compiles the actual patched OrangeFox row
renderer with host graphics stand-ins and checks 60 layouts, scaled icon gaps,
text clipping, description fonts and legacy fallback. Native management tests
exercise the actual scale callbacks, including deferred reset/load, explicit
application and invalid-selection refusal. XML checks verify descriptions and
the explicit Apply path. These checks complement the separate adapted GUI VM
review; they do not establish touch/display behavior on tablet hardware.
