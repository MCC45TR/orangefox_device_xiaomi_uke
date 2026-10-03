# Recovery sensor readiness

The owner's locked POCO Pad X1 stock observations are scoped to Android 15,
OS2.0.205.0.VOZMIXM. This recovery candidate uses Global OS3.0.303.0.WOZMIXM
inputs. Neither matching recovery services nor a sensor stream has been observed.
The corresponding Xiaomi Pad 7 profile requires independent evidence.

## Observed supplier and configuration boundaries

| Function | Stock evidence | Recovery consequence |
|---|---|---|
| Orientation | HAL names LSM6DSO; installed SSC defaults map x to -x, y to -y and z to +z | Use these strings as candidates for fixed-pose validation, not a confirmed Linux mount matrix |
| Alternative IMU | QMI8658 shares a static platform candidate | Do not instantiate both suppliers or infer which is active from file presence |
| Automatic brightness | The stock display dump selects the front STK3BCx non-wakeup ALS | A future bridge must use the verified front lux stream; rear SIP1328 is not the configured primary source |
| Magnetometer | QMC6308 platform filename; x to -y, y to +x and z to +z | The supplier suffix, axis direction and correction ownership remain unaccepted |
| IIO | Exposed stock symlinks identify PMIC ADC devices | Their presence does not supply recovery acceleration or ambient illuminance |

The calibration audit acquired 67 ODM sensor JSON files, one loader list and
1,100 typed parameter descriptions. Six platform candidates include the observed
MTP platform and SoC 643. This static match does not prove effective persisted
registry selection, service startup or dynamic sensor data. Raw SSC bus types,
instances, slave fields and IRQs are not AP I2C bus numbers, Linux regulator
phandles or verified TLMM pins. No speculative device-tree translation is added.

## Current interface behavior

Automatic rotation and brightness are unavailable. The Extra display category
shows their availability and links to existing manual screen controls. There is
no enabled checkbox or background polling loop pretending that a descriptor is
a usable stream. Tablet scale and monitor image size are independent settings.
Landscape and portrait guest rendering exercise geometry, not sensor rotation.

A future implementation needs an installed-profile service/firmware dependency
inventory, read-only permission checks for explicitly reviewed nodes, a recovery
stream with known units and monotonic timestamps, and measured fixed-pose/lux
validation. Apply orientation exactly once. Rotation needs hysteresis and a
manual lock; brightness needs smoothing, bounded rates and a manual override.
The calibrated/raw boundary and persisted state ownership must remain explicit.

## Calibration preservation

Private persist calibration was not readable in the supplied inventory and is
not copied into this project or payload. Diagnostics retain their narrow
allowlist. In the pinned OEM audio source, reading `fsm_re25_show()` triggers
forced calibration and a save path. A sysfs read can therefore mutate state;
recursive collection of all readable attributes is unsuitable. That node is
not probed. Do not activate sensor streams, calibration routines or generic I2C
scans merely to populate the availability page.

## Source records

- Parent `docs/research/CALIBRATION-BRINGUP-AUDIT-2026-10-03.md`, record
  STOCK-ADB-20261003-03, SHA-256
  `6ed8c89261ed7f2b5b5ca6ea5d9393fb86dd3b5b043f4a64e822de0f76193cbd`.
- Parent `docs/research/STOCK-ADB-INVENTORY-2026-10-03.md`, record
  STOCK-ADB-20261003-01, current corrected Markdown SHA-256
  `1a32b434ab651680dd868844ee8e3df532e7c0f4e4fe829b73f09145dedefa6f`.

These hashes identify the read versions, not future edits. Original private
receipts, calibration values and unit identifiers remain outside public files.
