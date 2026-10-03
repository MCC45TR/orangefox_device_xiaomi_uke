# One-shot boot requests and recovery history

The native boot manager inventories registered EFI entries, reviews an exact
target and preserves the first `BootOrder` entry as the default. Android, Linux
and Windows labels identify known loader paths. They do not establish OS,
signature, firmware or device compatibility. The current writer operates only
on explicitly marked, private regular-file fixtures. Real efivarfs writes,
EFI application execution, slot changes and reboot have no accepted Uke backend.

The recovery image continues to target the locked Global stock profile. The
separate CN/Global and POCO Pad X1/Xiaomi Pad 7 request fields exercise declared
context; they are not measurements of the installed model or firmware.

## Review and identity

Inventory parses bounded `BootOrder`, `BootNext` and `Boot####` records. The
supported load-option subset uses NV/BS/RT variable attributes, an active boot
option, a single GPT hard-drive node, a canonical absolute ESP file path and an
end node. Printable ASCII UTF-16 descriptions/paths are supported. Hidden,
non-boot, reserved-attribute, multi-instance and unsupported paths are refused.
Optional data remains opaque and is included in the complete variable hash.
This is a restricted parser, not a complete UEFI conformance validator.

Each selected/default loader must be a bounded, single-link regular file with
an AArch64 PE32+ EFI-application header. Planning binds its full SHA-256, the
complete load-option record, ESP/store directory identities, declared ESP UUID
and unchanged `BootOrder`. The loader's GPT node is not proof that the mounted
ESP is that physical partition. PE shape and a checksum do not verify a
signature, executable section correctness, kernel/root/DT closure or bootability.

The [UEFI 2.11 boot-manager contract](https://uefi.org/specs/UEFI/2.11/03_Boot_Manager.html#load-option-processing)
uses `BootNext` for one attempt and removes it before handing control to the
selected option. The fixture follows that ordering, then selects the preserved
default as its fallback decision. It never calls firmware `LoadImage`/`StartImage`.

## Durable request lifecycle

| Phase | Meaning in the regular-file fixture |
| --- | --- |
| `ARMING` / `ARMED` | Plan/intent and request owner are durable; exact `BootNext` bytes are written, synced and read back |
| `CONSUMING` / `CONSUMED` | A single attempt and private handoff-token hash are recorded; owned `BootNext` is removed before the simulated handoff decision |
| `ACKNOWLEDGED` | A matching synthetic success receipt is retained; physical success remains false |
| `FALLBACK_PENDING` / `FALLBACK_SELECTED` | A matching synthetic failure receipt selects the existing default; no new request or reboot is performed |
| `CANCELLING` / `CANCELLED` | Only the owned fixture request is removed; the default and OS files remain intact |
| `UNKNOWN` | Interrupted consumption or missing `BootNext` has no trusted outcome; it is never automatically replayed |

The journal and variable store use private ownership locks. Equal `BootNext`
bytes alone cannot authorize cancellation: the persistent request ID and plan
hash must also match. Terminal owner release first retains a private retirement
record. The same plan cannot be re-armed by selecting another journal directory.
A new attempt requires a newly generated, reviewed plan with a distinct ID.
Retirement records must be kept with the fixture store; deletion or rollback of
that store is outside this replay guarantee. Its hashes are integrity checks,
not signatures or protection from a malicious administrator.

Journal events form a bounded hash chain. Private receipts must match the
request, single attempt, handoff token, loader hash and a valid boot-ID shape.
The plaintext token is returned only to the fixture caller and is not retained
in the journal. Such a receipt is not a trusted OS health or attestation signal.

After interruption, inspect actual bytes and ownership before choosing a
recovery action. Recovery may finalize already observed arming/cancellation or
record an unknown outcome; it never repeats a consumed attempt. The journal
parent defaults to volatile `/tmp` in the GUI. Real reboot durability requires
a separately accepted persistent store and real platform tests.

## Native interface and GUI

The GUI under **Advanced → Uke Recovery Environment → OS boot manager and boot
history** lists registered entries, accepts exact selections,
reviews the complete request and exposes journal inspection/recovery. Changing
the target, model, profile, ESP, variable store or journal invalidates the review.
The stage action is shown only for an eligible private fixture store. Consumption
and acknowledgement are CLI test operations, not recovery boot actions.

```sh
uke-recoveryctl boot route-inventory --esp MOUNTED_ESP --variables PRIVATE_STORE
uke-recoveryctl boot route-plan request.json --esp MOUNTED_ESP \
  --variables PRIVATE_STORE --output plan.json
uke-recoveryctl boot route-execute plan.json --esp MOUNTED_ESP \
  --variables PRIVATE_STORE --journal NEW_PRIVATE_JOURNAL \
  --confirm REVIEWED_PLAN_SHA256
uke-recoveryctl boot route-inspect PRIVATE_JOURNAL --esp MOUNTED_ESP \
  --variables PRIVATE_STORE
uke-recoveryctl boot route-history PRIVATE_JOURNAL
```

The exact seven-field request schema is:

```json
{"schema":1,"target":"linux","boot_option":"0001",
 "fallback_option":"0000","esp_partuuid":"12345678-1234-5678-9abc-def012345678",
 "profile":"global-os3.0.303.0","model":"poco-pad-x1"}
```

These UUIDs/options are synthetic examples. Never reuse them as device identity.
`route-consume-fixture`, `route-ack-fixture` and `route-fallback-fixture` simulate
the lifecycle; `route-recover` and `route-cancel` require the exact inspected plan
hash. Unknown options, version fields, stale assets and another request's state
are rejected. Neither Android A/B nor a Windows installation is managed here.

## Acceptance boundary

Host C++/CLI and actual OrangeFox callback fixtures cover parsing, stale reviews,
default preservation, correlated acknowledgements, conflicts, cross-journal
replay, corruption and real SIGKILL around arming, consumption and retirement. Sanitized native
execution and the extracted ARM64 CLI provide separate build/emulation evidence.
QEMU user mode uses host-backed files and never exercises EFI firmware.

Actual Uke routing requires a source-reviewed Aloha port, accepted variable-store
and loader behavior, installed root/kernel/DT/ABI/signature closure, protected
stock return and exact-device forced-reboot tests. Dual/single boot acceptance
and Windows support remain open. Consult the dated build report and root
engineering lessons for results bound to a particular source/artifact revision.

## Current owner priorities

The 3 October 2026 delivery order is boot management, then LUKS/Windows,
ADB/backups, files/sessions and distribution/diagnostics. BitLocker is deferred.
Remote recovery uses ADB only: SSH/SFTP, USB networking and Wi-Fi are deferred.
The owner subsequently authorized publishing focused English commits before
the comprehensive host-only VM review. Existing dormant Dropbear packaging is
not an enabled service or acceptance of a network rescue feature.

The archived Aloha reference is pinned to
`8d37e2bfab2f9d959ac0b3d68434e8be4866bf55`; it supplies no accepted Uke port.
`EFI/UKE/android.efi` is a synthetic path convention, not a supplied or accepted
Android-return binary. The upstream
[automatic boot assessment description](https://systemd.io/AUTOMATIC_BOOT_ASSESSMENT/)
also separates an attempted boot from a health-confirmed successful one.
The official UEFI HTML/PDF acquisition attempts returned 403 here; no successful
specification archive or PDF hash is claimed. Primary implementation research
also inspected [EDK II boot-manager source](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Library/UefiBootManagerLib/BmBoot.c).
