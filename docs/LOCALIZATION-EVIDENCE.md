# Localization and GUI input identities

Changes to a font, translated label or theme can alter recovery behavior without
changing its executable. New build, host test, extracted-payload and adapted GUI
records therefore bind resource content as well as executables. Historical
records remain historical; adding a current digest to an old pass is prohibited.

## Source inventory

`configs/localization-inputs.json` declares the input closure. The host-only
`scripts/localization-evidence.sh source` reads the 32 actual language resources,
all common/extra/portrait theme files, packaged Roboto source and notices,
JsonCpp, libxml2 and FreeType sources/licenses, build consumers, the required
host key generator/lookup headers and their controls, plus optional owner
translation-fetch/font drafts. It checks
the six exact project revisions against the locked manifest, including projects
whose path defaults to their name. Duplicate, missing or new language codes
require a reviewed configuration change. Inventory does not execute the draft
generator, font helper or translation fetcher and does not contact a service.

The stable identity includes file bytes, paths, directory membership, modes and
regular-link target content. Special objects, ambiguous names, directory links
and unresolved links refuse. VCS administration is excluded from source scans;
pins are captured explicitly. Timestamp-only source changes are handled by the
separate full compile inventory. Local inventories use disk scratch and bounded
sorting. Their digests do not attest to a compromised host or a concurrent
uncooperative filesystem writer.

`native-inputs.sh` appends a `# localization_source_sha256=` comment containing
the composite source inventory's SHA-256. It is an inventory identity, not the
name of a file to pass to `sha256sum -c`. Existing per-file lines retain their
meaning. Native and sanitizer producers record this identity explicitly and
compare the full input manifest before/after execution. Publication requires
matching identities, complete named tests, and the compile-time source inventory.

Production build receipts preserve `inputs/localization.json` before/after the
compile and export it in `BUILD-COMPLETION.json`. Packages include matching
`LOCALIZATION-INPUTS.json` and `GUI-RESOURCE-INPUTS.json` in the ZIP, standalone
files and repeat checksums. Source archives carry the inventories, tools and
this guide and refuse source changes during archive creation. This does not
establish full offline Android dependency closure or font license acceptance.

## Payload and adapted GUI records

`scripts/localization-evidence.sh ui PAYLOAD` indexes all `twres` assets,
`sbin/maintainer.xml` and `system/etc/ure/licenses`, including styles, images,
language selectors, translated resources, fonts and notices. It permits no
indirect GUI resources and excludes no asset named like a VCS directory. An
extracted image must match the compiled payload inventory. The image audit and
manifest bind both source and actual GUI resource identities.

GUI adapter preparation and smoke execution require a current completed build.
The smoke runner records `localization_inputs_sha256`,
`shipping_ui_assets_sha256`, `overlay_ui_assets_sha256` and
`build_completion_receipt_index_sha256`. It verifies copy identity before its
documented page/timeout overlay and checks source/shipping/overlay resources
again after execution. The privately retained inventory JSON files accompany
the run. Process survival is still separate from visual acceptance.

A manually reviewed aggregate must preserve these identities at the top level
and for every run. Each run also needs `reviewed_ui_assets_sha256` equal to the
overlay identity whose screenshots were actually reviewed. The production
resource predicate refuses old/missing identities and different sources,
fonts, languages or reviewed overlays; the manifest driver independently checks
actual executable/adapter/runner identities and the existing visual predicates.
Do not retrofit these fields into historical reviews. Fresh portrait/landscape
and scale reviews are required after the remaining P1 work. Actual framebuffer
measurement and stronger screenshot/page oracles remain AUD-028/P2.

## Scope

An input inventory proves which bytes were captured. It does not establish
glyph coverage, RTL order, script shaping, correct translated warnings or font
license closure. The inventory records these acceptance flags as false. The
optional CJK draft has no accepted adjacent provenance/license and must not be
promoted by its content hash. The [AUD-029 checkpoint](MULTILINGUAL-TEXT.md)
provides separately scoped production-renderer source/host checks and exact
licensed assets; its results do not turn inventory flags into rendering passes.
The [AUD-030 checkpoint](LOCALIZATION-KEY-GENERATION.md) separately tests the
generator's owner lifetime, structural key closure and actual host lookup adapter.
Translation context/provenance and competent wording review remain AUD-031;
inventory does not promote the disabled translation commands into accepted tools.
Neither
host controls nor a generic VM establish tablet display/input acceptance.
