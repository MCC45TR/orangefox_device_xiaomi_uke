# Recovery text budgets

The native renderer accepts bounded display text without rewriting original
filename bytes. Malformed UTF-8 consumes one byte and displays U+FFFD; valid
Unicode scalars retain their original byte count. Width measurement does not
allocate a raster. Console wrapping must always consume a nonempty scalar,
including narrow viewports and leading punctuation.

Current implementation limits are:

| Resource | Bound |
|---|---:|
| Input before any string copy | 65,536 bytes |
| Scalars inspected by one measurement/layout | 8,192 |
| One text or glyph raster | 2 MiB |
| Text texture edge / font height | 4,096 / 2,048 pixels |
| Global owned glyph/string caches, including metadata allowance | 32 MiB |
| Per-font string/glyph entries | 128 / 512 |
| Font instances / one source / all owned source bytes | 64 / 32 MiB / 64 MiB |
| FreeType parser/library heap / one parser allocation | 64 MiB / 16 MiB |

The FreeType memory callbacks enforce parser limits **before** allocation or
decompression. Bounded source length alone does not bound WOFF expansion. Glyph
outline dimensions and their product are checked before rasterization. Rotated
surfaces preserve the checked pixel count and use automatic cleanup. A fixed
stderr diagnostic and failure result avoid recursive GUI logging or another
large allocation when a limit is reached. These counters describe owned
resources, not total process RSS, stack, libc or sanitizer overhead.

Font sizes share an owned face generation with separate FreeType size objects.
Generation identity includes file/inode, length and nanosecond change timestamps;
the read is rechecked before parsing. Theme replacement retains old bytes while
old references exist and loads the new source for new references. One global
short rendering lock protects shared face activation, lifecycle and accounting.
Temporary scaled fonts are released after drawing. Enlargement remains available
for scale preview, and width zero preserves the existing unscaled input contract.

The host fixture builds the pinned FreeType source with production-equivalent
zlib/WOFF support, compiles the complete production renderer and extracts actual
rotation, scale and console-wrap functions. It covers all four rotations, edge
texture coordinates, negative bearings/pitch, allocation failure, cache churn,
source replacement, decompression refusal, concurrency and complete ownership
release. Native and sanitizer results are separate from Android compilation,
shipping GUI, script shaping/fallback coverage and physical device acceptance.
