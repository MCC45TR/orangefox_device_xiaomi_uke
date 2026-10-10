# Install and test OrangeFox for uke

This experimental recovery is for **Xiaomi Pad 7 / POCO Pad X1 (`uke`)**.
It is a hardware test preview, not a recovery qualified for everyday use.
Keep a copy of a recovery that already works on your tablet.

The current touch profile targets the installed kernel
`6.1.175-android14-11-ga3b9c44908dd-ab13320413`. A different ROM or firmware
can require different modules and firmware. The IMG contains no kernel and
uses the tablet's installed boot stack. It is not a `fastboot boot` image.

## Install the IMG

Download `orangefox-r12-uke-modded-v002.img` from the
[release page](https://github.com/MCC45TR/orangefox_device_xiaomi_uke/releases).
Your bootloader must already be unlocked. Enter fastboot, connect the tablet
and check its identity, active slot and recovery capacity:

```sh
fastboot getvar product
fastboot getvar current-slot
fastboot getvar partition-size:recovery_a
```

The product must be `uke`. The recovery partition must be at least
`0x6400000` bytes (100 MiB). The following example is **only for slot `a`**:

```sh
fastboot flash recovery_a orangefox-r12-uke-modded-v002.img
fastboot reboot recovery
```

For active slot `b`, check `partition-size:recovery_b` and flash `recovery_b`
instead. Keep the inactive slot unchanged. Do not flash this IMG to `boot`,
`init_boot`, `vendor_boot`, `dtbo` or `vbmeta`.

## About the ZIP

`orangefox-r12-uke-modded-v002.zip` contains the same recovery image.
**ZIP installation is currently unavailable:** both ADB sideload and the
recovery's Install screen refuse installation before writing. Use the IMG
instructions above for this preview. A ZIP download does not mean that its
installer has passed device testing.

## First checks

After the screen appears, test menu touch before performing any other action.
Try **Files → Menu → Extra → Files**, including repeated taps on the same
tab. Turn the screen off and on with the power button, unlock it and repeat.
The owner confirmed these actions in the corrected live session; the new
image still needs this check after installation.
Check the displayed time, battery and temperature, then reconnect USB and run
`adb devices` to confirm recovery access. Report whether touch works immediately
or only after turning the screen off and on.

Battery reporting and encrypted Android storage remain unresolved. Automatic
rotation, automatic brightness and external displays also require separate
device tests. A readable temperature value does not
prove correct sensor selection or calibrated reporting.

Recovery appearance preferences use a private directory in `persist`, so
encrypted Android storage is not needed for theme or scale settings. Saving
is automatic and coalesced. Open **Extra > Display and input > Interface scale >
Saved recovery preferences** to inspect the storage result. If storage is unavailable or a
record is damaged, changes remain limited to the current session. Persistence
across a recovery restart still requires a device test for this preview.

Managed formatting, repartitioning, ROM/OTA installation, stock restoration
and other storage writes remain blocked. Do not use a root ADB shell to bypass
those restrictions. A successful screen boot or touch test does not validate
those operations.

If recovery does not work, return to fastboot and restore your previously
working recovery to the same active recovery partition. Preserve the installed
Android boot and firmware stack.

For engineering details, see [touch startup](TOUCH-STARTUP.md),
[recovery services](RECOVERY-SERVICES.md) and
[installation transaction limits](RECOVERY-INSTALL-TRANSACTIONS.md).
