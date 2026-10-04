# Stock boot programming layout verification

The common storage preflight and active-slot installer distinguish **OEM file
bytes** from the **whole partition's reviewed programming layout**. Both retain
exact partition geometry and whole-partition SHA-256 checks. A shorter OEM file
checksum must never be compared against a larger full partition.

Only Global OS3.0.303.0.WOZMIXM source pins are reviewed. These are source-derived
canonical layouts, not physical Pad 7 or POCO Pad X1 dumps. Installed model/SKU,
UFS ranges, firmware trust and forced-reboot acceptance remain separate gates.
The common storage writer remains disabled.

## DTBO and AVB footer

The pinned dtbo.img has 20,971,520 bytes. Its OEM GPT partition has 25,165,824
bytes. The source SHA-256 is
`044aae9d9a144e9a05b91d2785a2ff4504c78caa11f8c22781839ba2f6c76490`.
Its original DT table extent is 592,007 bytes, embedded vbmeta starts at
593,920 and has 640 bytes, and its final 64 bytes contain an AVB footer.

Pinned [AOSP fastboot](https://android.googlesource.com/platform/system/core/+/1efa79514b2f520c20a837c9216ff6b6e7e0dda3/fastboot/fastboot.cpp)
copies a raw image with an AVB footer into a fresh temporary file and duplicates
that footer at the end of a larger physical partition. It preserves the
original source bytes and leaves the intervening fresh-file gap zero. The
[AVB footer structure](https://android.googlesource.com/platform/external/avb/+/5ac0c3a071d811846a62412383dd6e259f341e6e/libavb/avb_footer.h)
describes the footer at the partition end. This source behavior does not prove
which flashing path an installed tablet used.

For the reviewed 24 MiB DTBO layout, the complete partition is:

1. Exact pinned 20 MiB source, including its original footer.
2. Zero bytes up to the final 64 bytes of the 24 MiB partition.
3. An exact duplicate of the source's 64-byte footer at the partition end.

The resulting whole-partition SHA-256 is
`9e55ff8afdf178e424187f0dc7d6dd2fa570308e22d8df7ac895d65017dbc0d7`.
Native preflight and installer compare the entire 24 MiB object against this
digest. Changed DT contents, nonzero gap bytes, a wrong/missing final footer,
different geometry or truncation are refused. A source-prefix match alone is
insufficient. No existing partition is padded, repaired or rewritten by this
verification.

Boot, init_boot, vendor_boot and recovery source lengths equal their reviewed
partition capacities; their whole-partition digests equal source digests.
Both A/B boot stacks and the inactive stock recovery remain required. Active
custom recovery is excluded only by the existing explicit slot policy.

## Reproducing and interpreting evidence

The host-only describe-stock-boot-programming.sh script takes the previously
verified OEM input directory and a fresh output catalog. It rechecks all five
source lengths/hashes, reconstructs canonical layouts in private disposable
files and records source and whole-partition lengths/hashes separately. It
never executes fastboot, OEM programs or storage writes. The independently
generated catalog must match compiled native pins.

Actual installer helper tests independently reconstruct the DTBO file, preserve
its source checksum, require the complete normalized digest and reject changes
inside the DT contents, fresh gap and duplicated footer. Both source and
whole-partition hashes remain named explicitly in native preflight records.
These host fixtures do not invoke production block-device selection or writes.

An installed layout with another tail policy remains refused. Supporting it
requires exact model/SKU/firmware and measured partition evidence; do not infer
that a rejected device needs rewriting. The unsigned embedded DTBO vbmeta does
not establish installed AVB, KeyMint/TEE or Android encryption trust.

The six-LUN image coordinator defaults to selected OEM source extents and
protected tails. It now offers an explicit reviewed whole-boot layout policy
for the five compiled boot payloads at their exact pinned capacities. DTBO then
includes its canonical gap and final footer, with all original tail bytes backed
up for rollback. Both modes report complete selected boot-layout checks
separately from programmed-range verification. A prefix job may commit while
reporting a noncanonical protected tail. Neither image success nor a matching
whole source layout proves installed-firmware trust or Android boot acceptance.
See [the image contract](STOCK-IMAGE-RESTORE.md) and the accompanying
[preflight build report](../reports/URE-STOCK-PREFLIGHT-BUILD.md).
