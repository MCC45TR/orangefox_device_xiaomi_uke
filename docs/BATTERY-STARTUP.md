# Battery startup on uke

The recovery reads the kernel's standard Battery power supply. It shows `--`
when that provider is missing, unreadable or invalid, and starts showing a real
percentage when a valid provider appears. It never substitutes an old percentage
or a made-up default. CPU thermal observation is separate from battery reporting.

The latest connected-tablet investigation found no power-supply devices. ADSP
was offline and the PMIC GLINK battery child had not been created. The installed
active-slot ADSP firmware headers match the previous successful RAM trial, but
firmware presence alone does not start the provider.

An automatic startup fix is not accepted yet. Starting ADSP can also activate
the inherited `charger_partition` helper, whose worker independently writes raw
storage. Read-only mounts do not prevent that worker. The reviewed replacement
QTI driver removes that dependency, but its genuine build inputs have not been
matched to the customized running kernel. That kernel's boot log explicitly
ignores module-version disagreements, so a successful insertion alone would
not establish compatibility.

The required repair sequence is to match corresponding kernel/ABI inputs,
rebuild and inspect the persistence-free driver, verify first-stage exclusions,
validate current-slot firmware and its load path, then perform one bounded ADSP
start and wait for actual power-supply registration. Charge/discharge and cold
recovery startup need their own device checks. The current release must not be
described as having this hardware repair.

For sanitized source and device findings, see
[the battery-startup record](https://github.com/MCC45TR/uke-linux-docs/blob/main/docs/lessons/2026-10-10-RECOVERY-BATTERY-STARTUP.md).
