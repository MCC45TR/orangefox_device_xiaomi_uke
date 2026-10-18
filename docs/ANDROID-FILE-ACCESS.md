# Read-only Android files

**18 October 2026 source status:** metadata/FBE support is enabled for the next
Uke test build. It uses the dedicated recovery Vold library and existing keys.
This is not physical decryption acceptance for Android 15, 16 or 17.

Recovery imports only the reviewed installed `/data` and `/metadata` fstab pair,
retaining the exact block identities and read-only, no-replay options. Plain
userdata can mount directly. Metadata-encrypted userdata requires its existing
key, a compatible registered KeyMint provider and a read-only `dm-default-key`
mapping. A successful mount exposes Android files at `/data`; internal media
becomes available through the normal file browser after CE unlock.

Use the recovery's normal decryption screen for a PIN, pattern or password.
Do not put a credential in ADB command arguments. An installation without a
screen credential can still use metadata encryption and FBE. Missing or invalid
credential metadata is not treated as a default password.

## Existing-key behavior

- Metadata and DE wrappers use the bounded existing-key reader and direct
  KeyMint client. Secret-protected CE keys retain authenticated decryption.
- The recovery path does not generate, upgrade, rename, fixate or delete keys,
  create Android directories, repair permissions, run fsck or format userdata.
- Wrapped-key conversion and fscrypt installation affect the active kernel
  session. Linux read-only mounts do not prove that an OEM TEE has no side effects.
- The metadata mapper is read-only. A pre-existing mapper is refused; failed
  filesystem mounting removes only the mapper created by this operation.
- Locksettings is queried read-only with SQLite's ordinary WAL handling and a
  bounded busy wait. No stale database-only copy or immutable-mode fallback is
  used. If the existing WAL/shared-memory state cannot be read, access fails.
- Generic FDE stubs, adopted-storage decryption, writable mounts, formatting,
  installation and other legacy mutations remain refused by the common policy.

## Remaining device gates

The installed firmware's KeyMint/TEE/RPMB, Gatekeeper/Weaver and CE Keystore
provider are not yet accepted in the complete recovery. Providers are not
automatically started by the new path. An absent provider returns failure;
there is no fallback to formatting or replacing metadata keys. An unbound
application ID is only an authenticated candidate when no storage-binding seed
has been supplied. A bound installation needs its actual seed; an empty Android
property does not establish that policy.

Host checks cover the real admission, mount and key-read functions with inert
boundaries, SQLite WAL behavior, preserved write refusals and unchanged normal
Android source branches. The target build, matching ROM/kernel/provider stack,
actual mapper and DE/CE access require separate evidence. Preserve the existing
ESP/Linux volumes and recovery fallback during that testing.
