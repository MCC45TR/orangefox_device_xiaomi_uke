# Uke OrangeFox development checkpoint

**26 October 2026 — source publication.**

This branch preserves pending build-evidence/cache validation, read-only existing-key access and deferred-userdata VM fixture source for the Uke recovery project.

Publication checks: changed shell entry points parse, and the actual build-evidence fixture passes. The latter covers compiler/header identity, interrupted builds, cache isolation and atomic publication on disposable host files. The generic ARM64 deferred-userdata VM and a complete new recovery image were not rebuilt by this publication.

Full tablet recovery, decryption, cold-start touch, storage mutation and boot routing remain separate acceptance gates. No image was flashed and no device was contacted.

Unaccepted local translation/font drafts, extracted payloads and private reports are excluded. Source/tool/archive boundaries remain enforced by the checked-in build scripts.

See the [default branch](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/tree/R12.0), [platform status](https://github.com/MCC45TR/uke-linux#hardware-support), [write policy](docs/RECOVERY-WRITE-POLICY.md) and [build tools](docs/HOST-TOOLS.md).

Original component licenses and human contributor attribution are retained.
