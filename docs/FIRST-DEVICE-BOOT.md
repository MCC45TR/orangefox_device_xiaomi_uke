# First-device recovery boot scope

The initial target is **POCO Pad X1 with Global OS3.0.303.0.WOZMIXM** and an
unlocked bootloader. The build uses the matched stock kernel and installed
boot/vendor_boot/DTBO stack. Source, host and generic VM checks do not establish
physical boot or a risk-free flash. Preserve the firmware-matched stock recovery
and the recorded active slot before the first experiment.

The first image retains the dedicated recovery root even if a bootloader sends
a normal-boot marker. Managed formatting, flashing, writable mounts, OTA, slot
changes and shared misc/BCB writes remain unavailable. An existing BCB request
is preserved, so Android return or recovery reentry still needs physical
validation. An authorized root ADB shell can issue raw writes independently of
these managed API restrictions.

The stock recovery load list includes two automatic storage writers:
`charger_partition` schedules raw charger-partition work, and `ufs_ffu` schedules
UFS firmware-update work. The source-built module loader mandatorily blocks
both when the packaged recovery executable is present, including aliases and
hard dependencies, regardless of the OEM list or a caller's bypass flag.

This also skips dependent charge monitoring, OEM thermal interface, Qualcomm
WLED, haptic and flash drivers. The exact dependency fixture keeps DRM, USB,
TSENS and CPU/devfreq cooling drivers independent. That source distinction is
not acceptance of their physical operation or of a replacement charging policy.
The first experiment is a brief idle screen/ADB check, not a charging, sustained
load or thermal test. Battery percentages and charging indications may be absent.

The minimal qcom init starts peripheral USB/configfs using the exact stock
`a600000.dwc3` controller property. It creates only a RAM block-label alias; it
imports no OEM mount_all, firmware writer or donor execution chain. USB role
selection uses a narrowly labeled sysfs file. A different controller property
does not authorize that path. The generic init owns FunctionFS and ADB services.

The stock vendor recovery list does not include the Novatek/Xiaomi touch chain.
Touch/pen support remains pending its exact module, firmware and service review.
ADB and display are therefore the first acceptance targets. Stock kernel HID
configuration is source evidence only: this peripheral-mode image cannot claim
simultaneous USB-host mouse/keyboard support through the same port.

Use the dedicated recovery image only for the verified active recovery slot.
The kernel-containing temporary image is exclusively for `fastboot boot`.
Do not modify boot, init_boot, vendor_boot, DTBO, vbmeta, userdata or the inactive
recovery as a workaround. A matched stock image supplies a documented fallback;
it does not prove the bootloader's exit path or pending BCB clearance.
