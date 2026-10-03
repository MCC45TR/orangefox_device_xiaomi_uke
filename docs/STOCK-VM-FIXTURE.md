# Observed stock namespace VM fixture

`tests/fixtures/stock-adb-2026-10-03.json` derives selected, non-unit-specific
observations from the owner's locked POCO Pad X1 Android inventory,
`STOCK-ADB-20261003-01`. Source document and JSON checksums are retained. The
installed firmware is **OS2.0.205.0.VOZMIXM**, Android 15; the current experimental
recovery build targets **OS3.0.303.0.WOZMIXM**, Android 16. This fixture does not
establish compatibility between those firmware stacks.

The observations contain 121 partition labels across six Linux disk names:
sda (32), sdb (6), sdc (6), sdd (3), sde (67) and sdf (7). They identify
super as sda31 and userdata as sda32. Numeric UFS logical-unit identifiers,
physical GPT sectors, device capacities and GPT GUIDs were not observed through
the unprivileged shell. Filesystem capacity must not be substituted for UFS
capacity. Xiaomi Pad 7 requires its own stock inventory/profile verification.

The host-only C++ generator creates six sparse **regular files** with synthetic
GPT ranges and GUIDs. Firmware entries are blank 1 MiB placeholders; userdata
is blank 512 MiB. Super preserves the observed 11,274,289,152-byte size and
eight active logical allocations, totaling 7,936,512,000 bytes. Their captured
starts are super-relative **512-byte sectors**, not physical GPT LBAs. Pinned
AOSP lpmake generates new metadata with three metadata slots, then the
generator embeds it in the synthetic super entry. This is reconstructed test
metadata, not a captured stock metadata backup.

The runner attaches all six files read-only to QEMU's generic ARM64 virt
machine, without host block devices, USB passthrough or a NIC. Shipping native
storage discovery enumerates every label, partition index and disk group. The
shipping AOSP liblpdump library checks allocations and extent starts. A VM-only
direct entry-point adapter replaces the normal Binder client because this guest
has no Android service manager. It is excluded from release payloads; Binder,
snapshot service, FBE and firmware image contents are not tested here.

```sh
UKE_QEMU_SYSTEM_AARCH64=/path/to/reviewed/qemu-system-aarch64 \
  bash tests/check-stock-namespace-vm.sh /path/to/generic/arm64/Image
```

First prepare the VM adapters and build the native host library. The runner
emits a sanitized receipt in the ignored `reports/private/` directory, bound to
its runner, fixture, generator, generic kernel and native CLI checksums.
These files must never be flashed. Neither this fixture nor a passing guest
authorizes default-partition restoration or userdata resizing on the tablet.

Initial trials exposed guest setup issues: resolving Toybox before proc was
mounted, trying the Android Binder lpdump client without its service, and
expecting explicit zero-size fields that protobuf omits. The corrected runner
uses an absolute first mount command, a direct-library VM adapter and the
pinned tool's actual JSON/text conventions. Failed trials are excluded from
acceptance; these were guest setup issues, not product storage failures.
