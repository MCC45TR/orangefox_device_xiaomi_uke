# Uke recovery source audit

## OrangeFox baseline

The pinned official [fox_16.0 core](https://gitlab.com/OrangeFox/bootable/Recovery/-/tree/6ff71bed1506fec1893247f0c74d7ae87c594eed) sets `FOX_INTERNAL_RELEASE := R12.0` in `orangefox.mk`. The official sync instructions describe Android 16. The [wiki changelog](https://wiki.orangefox.tech/changelog) still carried R11.3 during the 28 September research. Source release, published release and device build success are different records.

Manifest commit `6bbb43ed568c9ee2127fb64333808388e583e459` includes recovery/vendor/system-core/libvterm and a Mondrian/SM84xx target. Replace that target with Uke only after stock boot analysis. The full Android manifest has not been resolved, downloaded or built. Network recovery/web/NAS dependencies must not be assumed present in this source branch. `FOX_VERSION` is obsolete; follow current release and numeric maintainer-version configuration.

## Uke donor

[Thanick50/ofox_device_xiaomi_uke](https://github.com/Thanick50/ofox_device_xiaomi_uke/tree/ad9bf4ed034455663a71f61eb214322806f9f1fd), branch `real-fox_14.1`, is archived for comparison. The repository description reports **broken f2fs data format**.

| Setting | Observation | Required action |
|---|---|---|
| Recovery partition size | 100 MiB declared | Compare stock image/GPT evidence |
| Exclude recovery kernel | Enabled | Determine the actual boot chain and target image |
| Boot header | v4 declared | Parse stock headers independently |
| FIXED_DECRYPT | Enabled | Treat as configuration, not decryption proof |
| Platform/security date | 99.87.36 / 2127-12-31 | Investigate compatibility purpose; never claim actual security currency |
| Missing dependencies | Allowed | Reject incomplete successful-looking builds |
| GPU platform name | qcom-adreno735 | Distinguish shared product settings from physical Adreno732 |
| Vendor modules | Touch, DRM, charging and others | Match stock kernel ABI, dependencies and load order |
| Temperature path | thermal_zone48 | Discover the correct channel by type/name |
| TW_NO_HAPTICS | Enabled | Does not prove hardware absence |
| Settings directory | persist | Use a project area, not calibration storage |
| userdata/metadata fstab | F2FS, mifs and wrapped-key entries | Verify real filesystems and formatting behavior |

First experiments use file-backed filesystem fixtures and pinned mkfs/fsck tools. Stock image extraction follows. No data-format script or security binary from this donor has been run on a device.

## First build gate

Resolve the full source manifest, pin the host toolchain, derive image limits and module ABI, and build a clean Uke target. Unpack the result, inspect ELF architecture and shared-library closure, reject Python payloads, validate header/size/hash/profile identity and repeat the build offline. A successful image build still leaves display, touch, USB, FBE and restore tests open.
