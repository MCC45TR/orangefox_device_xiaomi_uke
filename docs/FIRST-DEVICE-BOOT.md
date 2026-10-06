# First-device recovery boot scope

The reference build profile is **POCO Pad X1 with Global
OS3.0.303.0.WOZMIXM** and an unlocked bootloader. The dedicated recovery image
has no embedded kernel and relies on the installed boot/vendor_boot/DTBO stack.
The temporary boot image includes the reference profile's stock kernel. Source,
host and generic VM checks do not establish physical boot or a risk-free flash.
Preserve the working recovery and the recorded active slot before an experiment.

The first available tablet inventory was taken in an existing TWRP session after
the owner installed AxionOS. Its active vendor stack reports
OS2.0.205.0.VOZMIXM and its running kernel differs from the reference profile.
Read-only backups of the active boot chain and working TWRP were verified on the
host. Current vendor module metadata passes the actual-loader host policy tests;
this does not establish the new recovery's runtime compatibility. Evaluate the
kernel-less recovery separately. Do not boot the stock-kernel temporary image
against a changed vendor stack without an independent compatibility review.

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
recovery as a workaround. A verified copy of the currently working recovery is
the direct fallback after a custom ROM change. A stock fallback additionally
requires a matching installed firmware chain. Neither establishes the
bootloader's exit path or pending BCB clearance.
