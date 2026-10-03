# Stock boot preflight programming-layout correction

Date: 3 October 2026. Implementation:
`a27f4ca36afe6273db1dbb06dd7d8d8a7546e1f9`; source parent:
`66e92d36e725d2ca3292958d2e268ffa232ee717`.
Local candidate: `artifacts/ure-stock-preflight-alpha/`. Nothing was published,
flashed or normalized on a storage device. Earlier sealed candidates remain
unchanged.

This advances the third ordered request by correcting the common preflight and
installer's DTBO source/capacity mismatch. It does **not** complete or accept the
live UFS writer, model/SKU/range provenance or physical forced-reboot recovery.

## Corrected contract

The reviewed Global DTBO source has 20,971,520 bytes while its OEM GPT partition
has 25,165,824. Comparing the source checksum with all 24 MiB was wrong; accepting
only the first 20 MiB would omit the tail and end metadata. The policy now stores
source length/hash and whole-partition length/hash separately, while retaining
the exact geometry and whole-partition checks.

Pinned AOSP fastboot `copy_avb_footer` preserves a raw source with an AVB footer,
extends a fresh temporary file to the larger physical capacity with a zero gap,
and duplicates the source footer at the partition end. The independently
reconstructed 24 MiB whole-partition digest is
`9e55ff8afdf178e424187f0dc7d6dd2fa570308e22d8df7ac895d65017dbc0d7`.
It is a source-derived canonical layout, **not a measured tablet dump**.
Other installed tail policies remain refused pending model/firmware evidence.

Both native storage preflight and the active-slot installer use that complete
digest. Preflight records expose the source bytes/hash, partition bytes/hash,
programming layout and whole-partition scope. Verification does not pad, repair
or rewrite DTBO. Both stock boot slots and inactive stock recovery remain
required; the installer retains its original active-slot-only behavior.

The separate six-LUN image coordinator still programs reviewed OEM source
extents and protects existing tails. Its success does not establish that those
tails satisfy this stricter boot layout or boot Android. The common live writer
remains closed. KeyMint/TEE and Android encryption authorization are unchanged.

## Source and independent reconstruction

The [five-image programming catalog](../manifests/stock-boot-programming-global.json)
records independently rechecked source hashes and canonical full-capacity
digests. Boot, init_boot, vendor_boot and recovery have matching source/capacity
lengths; their digests are unchanged. Only DTBO needs the reviewed larger layout.
The Bash recipe uses private disposable files, never fastboot or OEM scripts,
and refuses existing output catalogs. Sources remain unchanged.

Exact primary source identities are AOSP `system/core`
`1efa79514b2f520c20a837c9216ff6b6e7e0dda3`, fastboot.cpp `copy_avb_footer`, and
`external/avb` `5ac0c3a071d811846a62412383dd6e259f341e6e`, avb_footer.h.
The [operation contract](../docs/STOCK-BOOT-PREFLIGHT.md) links those sources.
Pinned host avbtool info_image reports footer 1.0, original DT extent 592,007
bytes, vbmeta offset 593,920, size 640 and algorithm NONE for this DTBO source.
Those embedded fields do not prove installed AVB or KeyMint/TEE trust.

## Validation

| Stage | Result and boundary |
|---|---|
| Project policy | 19 host checks passed; no hardware test |
| Native C++ | All 21 CTest executables passed with warnings as errors; 220.12 seconds |
| Native pin oracle | All five compiled boot policies match the independently reconstructed catalog; ten raw/sparse source pins retain their separate catalog |
| Actual installer helper | Pinned DTBO source and full normalized layout accepted; changed DT contents, gap byte, final footer and truncation refused; source and full hashes explicitly differ |
| Complete host gates | Saved plans, journals, stock/GPT/partition/backup/restore/display/filesystem/rescue/boot-codec and strict installer/payload gates passed |
| Sanitizers | All 21 native executables passed pinned ASan/UBSan/leak checks with halt-on-error; 460.98 seconds; documented host-runtime vptr exclusion remains |
| Independent shell recipe | All five source lengths/hashes and reconstructed digests match the reviewed catalog; existing-output overwrite refused |
| ARM64 build | Native CLI/library, installer and OrangeFox compiled successfully in 2:40 with the preserved stock kernel |
| Actual ramdisk | Compressed header-v4 LZ4 payload extracted; 206 ELF objects have dependency closure; two embedded ZIPs pass integrity/privacy/no-Python scans; source/staging agree |
| Compiled corrected policy | New full DTBO pin is present in both extracted CLI and installer; build inclusion is not positive live preflight |
| Extracted CLI QEMU user | Six-LUN stock and baseline image operations/readback/rollback passed on host-backed regular images; no tablet or rendered UI |
| Package repeat | All six initially checksummed assets agree across two packaging runs; no independent full compile-reproducibility claim |
| Physical / guest reset | Not run for this binary: installed firmware, model/SKU, UFS, stock boot, forced reboot, GUI or rollback; VM records remain null |

The narrow native input manifest now also includes installer_test.cpp, which
was already included in earlier complete project inputs/source archives but
omitted from the narrower native receipt. Fresh native/sanitizer receipts bind
manifest SHA-256
`c2d435fa133aba79bf52d359f0c716446a5f02be0019aa7fb40304d854d59610`.
Compiler scopes remain GCC 16.2.1 / Red Hat 16.2.1-2 for host C++, pinned host
Clang executable `55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f`
for native instrumentation, and the locked Android toolchain for ARM64.
The standalone actual installer fixture uses the host compiler; the native
21-test sanitizer result is not relabeled as standalone-installer instrumentation.

The existing upstream packaging `local` outside-function warning remains
separate from the successful build and final compressed-payload audit. No new
tablet dependency or project Python was introduced. Earlier generic partition
VM reset evidence is retained against its own CLI, not this binary.

## Exact artifacts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Recovery image | 104857600 | `c3c2bc352dec3ff022cbc64a16d77cdfeb5a2cdf4d487174b93eb89c7b2e9d54` |
| Temporary fastboot-boot image | 100663296 | `56478b4fa2f9a28dcf19a7b8beadf7c0d1ee83593238fc19b881390bdfe195fb` |
| Flashable ZIP | 33789632 | `e412468635bf08f1b812085db1ca933980f727cd8c0e3e5e79ed771984364268` |
| Compressed shared ramdisk | 38664051 | `b8b43a51c2960305b632670309f595f972aa9478f16d2b037d802f7cc57e5089` |
| Native CLI | 1855128 | `8450b79b9c99ee56708403fadcfd20c65cb700a285f57eb24e28ca9d4de30599` |
| Preserved stock kernel | 35432960 | `97b2c53022e8c0bd0b279c4c592a196126a6eb0a93647c2957b39dcd5f2dc1c9` |

The sealed manifest records clean Git source identity, complete project inputs,
source archives, native/sanitizer receipts, extracted audit and tool identities.
SHA256SUMS covers every generated public asset. AVB remains NONE and the ZIP
unsigned. Temporary-boot and recovery-flash assets retain separate roles.

This is the dated current-source correction to REC-L006 / REC-S002, documented
with REC-P001–REC-P004 in the root
[engineering lessons](../../docs/lessons/2026-10-03-RECOVERY-BOOT-PREFLIGHT.md).
The [earlier stock image checkpoint](URE-STOCK-JOB-BUILD.md), its checksums and
its source-era mismatch finding remain preserved.

Remaining work stays ordered: live writer/model/SKU/range provenance and exact
UFS forced-reboot acceptance first; then filesystem resize/repair/tool packaging,
Android trust/A-B/Virtual-A-B/super/OTA/isolation, real Arch/Fedora rescue,
Btrfs kernel/receive/restore/paths/boot and complete boot/root/DT/ABI/signature
closure. Neither commercial model's physical acceptance is inferred here.
