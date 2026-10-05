# Multilingual recovery text

The recovery renderer now selects licensed script fonts, shapes logical text
with HarfBuzz, and orders mixed-direction runs with FriBidi. Measurements,
clipped prefixes and raster drawing share the same production layout. Original
UTF-8 byte offsets remain intact, including malformed filename display bytes.

This is a source and reference-host checkpoint for AUD-029. A new complete
Android image, recovery-partition capacity check, shipping GUI review, combined
VM and physical tablet acceptance remain required. Existing public artifacts
have not been rebuilt by this checkpoint.

## Pinned sources and assets

[`text-layout.lock.json`](../manifests/text-layout.lock.json) identifies the
original sources, generated tables, exact font/notice bytes and local patches.

| Component | Pin and purpose |
|---|---|
| HarfBuzz 14.5.1 | `eb033319fc2ebe723597fa497d4bb4331bab8b6b`; OpenType shaping and Unicode 18 properties |
| FriBidi 1.0.17 | `b93119f5fdc7ea47672cc304c1455ffa6dfe7536`; four C translation units for types, brackets, embedding levels and line reorder |
| FreeType | AOSP `d968d2541f7158e18ab22680bfa08a538019bf6a`; the reviewed empty-variation-region backport |
| AOSP Noto | `f1fe27f9777986e0f5973b4e6dda8be9dae49a95`; six unchanged font files and six original OFL notices |

Roboto remains the primary font. Fallbacks cover Arabic, Hebrew, Bengali,
Devanagari, Thai and regional CJK. The original CJK collection selects JP, KR,
SC and TC faces for the corresponding locales. One immutable collection buffer
is shared across regions and font sizes. Indic variable fonts use their original
Regular weight 400 defaults. Font assets are not subsetted or regenerated.
The unrelated local CJK draft is not an accepted redistribution asset.

The FriBidi release includes Unicode 18 input data but its distributed generated
tables and version header still identify Unicode **14.0.0**. These unchanged
tables are used deliberately; the checkpoint does not claim Unicode 18 bidi
conformance. HarfBuzz's different property version does not remove that limit.
Paragraph direction uses the first strong character. This work orders text; it
does not mirror the entire GUI or supply paragraph line breaking.

## Layout, limits and failure behavior

Font/script/embedding-level runs are shaped from logical scalars. Visual glyphs
retain their original logical byte clusters. Width fitting never splits the
tested ligatures, combining marks or Indic conjuncts. A fitted prefix is shaped
again for its actual line-edge context, with a bounded additional search when
context changes its width. Combining glyph masks use source-over coverage so a
transparent mark pixel cannot erase its base.

Existing [text budgets](TEXT-RENDERING-LIMITS.md) remain enforced. New shared
HarfBuzz/FriBidi allocator limits are 16 MiB total and 4 MiB per allocation;
layout accepts at most 8,192 scalars and 32,768 output glyphs. Shaper buffer,
operation and table-sanitization limits are fixed in the reviewed adapters.
Library allocation failures invalidate the layout instead of publishing partial
positions. Font source, parser, glyph/string cache and temporary surface budgets
remain separate from total process RSS and sanitizer overhead.

The local FriBidi patch checks both line-reorder heap allocations and cleans up
partially allocated deep-isolate bracket stacks. Real failure injection reproduced
the original null access before this correction. FreeType uses the upstream
[empty-region fix](https://github.com/freetype/freetype/commit/b1cbcb20454e3b465b0d3ea4d5457975cfa747e7),
which prevents null-pointer arithmetic in variable-font delta lookup.

Language changes clear cached strings under the existing renderer lock. They
do not rewrite filenames or language files. Font loading and all FreeType size
activation remain serialized. Stable multiscript line metrics include ascenders
and descenders from the loaded families; changed theme spacing therefore needs
fresh whole-GUI review.

## Reproduce the focused checks

First stage the normal reviewed build tree. The text-source scripts read exact
Git/release archives and do not execute donor build scripts or generators.
The CMake and Android adapters compile the same fixed C/C++ source lists.

```sh
bash scripts/prepare-build-tree.sh global-os3.0.303.0
bash tests/check-text-layout-resources.sh
bash tests/check-text-patches.sh
bash tests/check-multilingual-text.sh native
bash tests/check-multilingual-text.sh sanitizer
```

The focused producer retains before/after source identities, three actual
executables, logs and eleven native PPM atlases in private build directories.
It uses the host resource envelope and compiler cache. Sanitizer options are
set **inside** the isolated service, and a deliberately failing host-only
overflow probe verifies that undefined behavior actually halts. Any sanitizer
diagnostic also invalidates the result. The pinned runtime's previously documented
vptr exclusion remains; it is not presented as complete UB coverage.

These records are marked `focused-production-text-host`. They explicitly refuse
the interpretation of complete native-catalog, Android-build, shipping-GUI, VM
or tablet acceptance, and are not substitutes for release-policy receipts.

The production fixture reads all 32 pinned language resources, splitting resource
newlines as the GUI/console does. It checks 35,191 string/display records and
798,009 scalars excluding ASCII controls without a missing output glyph, independent mixed
Hebrew/numeric order, Arabic contextual forms, combining/conjunct clustering,
regional CJK families, Regular variation coordinates and interleaved preview
font sizes. Eleven scales from
50 through 100 draw fourteen script samples in all four orientations: 616 native
adapter draws. This does not prove translation meaning or whole-page geometry.

## Packaging and redistribution

The host font packager validates all six source/notice pairs and destination
paths before copying. Changed, missing, indirect or special files, duplicate
destinations and unknown assets refuse. The extracted-image auditor requires
the same original bytes, source table and exact license notices. Package helpers
are host-only and do not become tablet tools.

The image retains root and per-file HarfBuzz/FriBidi notices, the Microsoft USE
notice, original font notices and the pinned
[Unicode data license](https://www.unicode.org/license.txt). Source archives
include original Noto/FreeType snapshots, the versioned text-library snapshots,
reviewed adapters/patches, project sources and this guide. The unaccepted local
font draft is excluded from redistribution.

For a source-archive reconstruction, restore `harfbuzz-14.5.1/` and
`fribidi-1.0.17/` beneath `src/upstream/text-layout/`, and copy them to
`android/external/ure-harfbuzz/` and `android/external/ure-fribidi/` respectively.
Apply the reviewed FreeType/recovery stacks and copy the project text helper
into recovery's `minuitwrp/`. The complete application sources and build adapters
must accompany a binary release so the LGPL library can be rebuilt/relinked;
this checkpoint supplies no binary-only redistribution exception. Offline closure
of every Android dependency and reproducible binary builds remain separate gates.
