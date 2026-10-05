# Host localization key generation

The host catalog tool validates the complete JSON owner before extracting a
source-to-key table. The owner remains alive throughout iteration. It refuses
duplicate source text, duplicate key names, malformed row types, unsupported
schemas and reserved `Extra` alias collisions. It does not silently overwrite
one message with another.

This closes the reproduced AUD-030 draft lifetime defect at source and focused
host level. Translation provenance and semantic review belong to AUD-031.
Complete Android, shipping GUI, combined VM and tablet acceptance remain separate.

## Generate structural inputs

The tool is built only inside the host `BUILD_TESTING` configuration. It uses
the pinned JsonCpp sources, host libxml2/OpenSSL development libraries, C++20
and existing Bash resource controls. No translator, generator or Python runtime
is installed in recovery.

```sh
bash tests/check-localization-keys.sh native
bash tests/check-localization-keys.sh sanitizer
build/ure-host/uke-locale-catalog prepare . build/locale-catalog
build/ure-host/uke-locale-catalog keys build/locale-catalog/catalog.json build/locale-catalog/ure-locale-keys.hpp
```

`prepare` inventories current source strings and contexts. Its regex-based
collection is a structural draft, not competent linguistic review. `keys`
generates only a lookup header. Legacy translation commands remain disabled.
The separate [offline provenance tool](LOCALIZATION-PROVENANCE.md) validates
current-source jobs and unreviewed responses; materialization still requires
competent review. The optional network script is not invoked by these checks
or normal recovery builds.

## Schema and bounded output

The root must be an object with integer `schema_version: 1` and a `strings`
array. Each row needs nonempty string `source` and `name` fields. Other catalog
fields, such as contexts and language inventories, do not establish translation
acceptance merely because key extraction succeeds.

| Input | Limit or rule |
|---|---|
| Catalog file | 32 MiB; regular file; final input component cannot be a symlink |
| JSON nesting | 64; duplicate object members, extra trailing input and comments refuse |
| Catalog rows | At most 16,384 |
| Source text | At most 65,536 bytes; valid UTF-8 without NUL |
| Key name | At most 128 ASCII bytes; letter/underscore first, then letters, digits, `_`, `.` or `-` |
| Table source/key bytes | 2 MiB total, independent of the larger language inventory |
| `Extra` | Exactly `ure_extra_tab`; a validated empty catalog still produces this one alias |

The current pinned JsonCpp accepts comments in some object positions even when
its comment option is disabled. An additional scan rejects slash tokens outside
JSON strings; URLs and slash bytes inside strings remain data.

Entries use deterministic byte order matching the actual `string_view` lookup.
Fixed three-digit octal C++ escapes preserve UTF-8, non-BMP characters, control
bytes, quotes, backslashes and following digits. JSON surrogate escapes are not
used as C++ universal-character escapes.

All schema, type, uniqueness and budget checks finish before publishing a header.
Publication uses a private same-directory regular file and atomic rename;
indirect/special destinations and parent traversal refuse. Invalid catalogs keep
the previous header unchanged. File and final-directory synchronization errors
are reported; this host artifact mechanism is not a tablet write journal.

## Evidence and remaining gates

The focused producer runs the actual generator schema/publication test, native
display/preview/layout/management callback controls, and four independently
compiled headers: empty, one row, non-BMP/control text and the current full
catalog. The unchanged actual lookup adapter checks every generated entry's
count, order, exact source bytes and key. Unknown text and typed opaque data
retain their original bytes. Repeated generation must be byte-identical.

The sanitizer mode sets halt/leak options inside the isolated host service and
requires a deliberately invalid, host-only copy of the original temporary-owner
pattern to fail with ASan `heap-use-after-free`. That negative executable is
never a passing CTest or tablet payload. The documented pinned-runtime vptr
exclusion remains.

Before/after source inventories, compiler identities, current catalog/header
hashes and receipts are retained privately. They explicitly mark complete
native-catalog, Android, shipping GUI, VM, provenance, semantic and physical
acceptance as false. A compiled header proves structural closure; it does not
prove translated warnings, language identity or a current shipping image.
