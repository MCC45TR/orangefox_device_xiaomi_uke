# Installed touch startup

The experimental Uke recovery owns one native touch session after the first
complete, awake OrangeFox page frame. The GUI launches `uke-touch-supervisor`
once, collects bounded status messages and closes its ownership pipe during
shutdown. Page changes and theme reloads cannot start additional sessions.

## Reviewed input profile

The current input profile is specific to the reviewed Android 17 installed
kernel `6.1.175-android14-11-ga3b9c44908dd-ab13320413` and the pinned installed
ODM, vendor and vendor_dlkm files. It does not qualify a different ROM, firmware
or kernel. The original Global OS3.0.303.0 source baseline remains a separate
build input; it does not establish the installed OS3.0.304.0 stack's complete
compatibility.

The supervisor validates active-slot linear mappings and read-only, no-replay
provider mounts, then hashes the reviewed panel firmware and exactly two input
modules. It prepends `/odm/firmware` to the kernel's volatile firmware search
path while preserving existing entries. It uses retained module descriptors
with `finit_module`; there is no dependency autoload, ABI override or storage
module reload. Existing input modules are left loaded.

The installed Xiaomi touch service and its linker/dlopen providers are checked
by size, SHA-256 and ELF identity. Proprietary service, firmware and module
bytes are not distributed by this implementation. Packaging must compare the
source-built provider pins with the new extracted recovery payload.

## Process and write containment

The service runs in the supervisor's private mount namespace. Writable storage
aliases are refused; data, metadata, cache, persist, pstore, block-device paths
and sockets are covered by RAM-only mounts. Device-mapper control and pmsg are
masked before execution. The process receives a minimal environment, no
inherited management descriptors and `no_new_privs`. Existing service processes
are refused; unrelated processes are never killed.

Startup has a 30-second deadline. Once admitted, the session follows the GUI
lifetime rather than the diagnostic trial's 15-minute timer. Output is drained
with a 1-MiB RAM retention cap. Owner loss or shutdown terminates and reaps the
exact owned process group; there is no restart loop. This containment is not a
claim that every OEM behavior or root capability has been independently proven.

## Evidence boundaries

The owner observed working menu touch after a RAM-only firmware-path correction
and a normal display off/on cycle on the previous boot-test image. That result
qualifies the temporary combination only. The permanent startup implementation
has 118 host controls in each production-path, owned-GUI and pinned Clang
ASan/UBSan run. These controls do not load the OEM service or tablet modules.

A newly built image still needs its own cold-start touch and shutdown test.
Successful input does not admit Format Data, repartition, ROM/OTA installation,
Android FBE, Linux LUKS, stock restoration, persisted settings or first-stage
storage modules. Their separate safeguards and acceptance requirements remain.

Canonical engineering records live in
[uke-linux-docs](https://github.com/MCC45TR/uke-linux-docs/tree/main/docs/lessons)
(repository access is required).
