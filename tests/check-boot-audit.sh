#!/usr/bin/env bash
# Native container/codec inspection of synthetic installed assets; never boots them.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
binary=${UKE_RECOVERYCTL_BINARY:-$component/build/ure-host/uke-recoveryctl}
fixture=$(mktemp -d /tmp/ure-boot-audit-XXXXXX)
trap 'find "$fixture" -depth -delete' EXIT
root="$fixture/root"
mkdir -p "$root"/{etc,usr/lib/modules/fixture-1,boot/loader/entries} "$fixture/archive/usr/lib/modules/fixture-1"
printf 'ID=arch\nNAME=Arch fixture\n' > "$root/etc/os-release"
printf 'UUID=fixture-root / ext4 defaults 0 1\n' > "$root/etc/fstab"
for index in modules.dep modules.alias modules.symbols modules.builtin; do : > "$root/usr/lib/modules/fixture-1/$index"; done
truncate -s 64 "$root/boot/vmlinuz-fixture-1"
printf '\x40\x00\x00\x00\x00\x00\x00\x00' | dd of="$root/boot/vmlinuz-fixture-1" bs=1 seek=16 conv=notrunc status=none
printf 'ARM\x64' | dd of="$root/boot/vmlinuz-fixture-1" bs=1 seek=56 conv=notrunc status=none
printf 'version fixture-1\nlinux /vmlinuz-fixture-1\ninitrd /initramfs-fixture-1.img\noptions root=UUID=fixture-root\n' > "$root/boot/loader/entries/fixture.conf"
printf 'Synthetic archive entry, no executable module.\n' > "$fixture/archive/usr/lib/modules/fixture-1/fixture.txt"
(cd "$fixture/archive" && find . -print0 | LC_ALL=C sort -z | cpio -o --null --format=newc --quiet) > "$fixture/initrd.cpio"
audit() {
    "$binary" linux audit --root "$root" > "$fixture/audit.json"
    jq -e '.result=="ok" and .data.error_count==0 and .data.metadata_consistent and (.data.boot_validated|not) and (.data.physical_test_record|not)' "$fixture/audit.json" >/dev/null || { cat "$fixture/audit.json" >&2; return 1; }
}
cp "$fixture/initrd.cpio" "$root/boot/initramfs-fixture-1.img"
audit
for codec in gzip xz zstd; do
    case "$codec" in
        gzip) gzip -n -c "$fixture/initrd.cpio" > "$root/boot/initramfs-fixture-1.img";;
        xz) xz -c "$fixture/initrd.cpio" > "$root/boot/initramfs-fixture-1.img";;
        zstd) zstd -q -c "$fixture/initrd.cpio" > "$root/boot/initramfs-fixture-1.img";;
    esac
    audit
done
gzip -n -c "$fixture/initrd.cpio" > "$fixture/first.gz"
cat "$fixture/first.gz" "$fixture/first.gz" > "$root/boot/initramfs-fixture-1.img"
audit
cat "$fixture/initrd.cpio" "$fixture/first.gz" > "$root/boot/initramfs-fixture-1.img"
audit
head -c -4 "$fixture/first.gz" > "$root/boot/initramfs-fixture-1.img"
"$binary" linux audit --root "$root" > "$fixture/audit.json"
jq -e 'any(.data.findings[]; .code=="invalid-compression") and (.data.metadata_consistent|not)' "$fixture/audit.json" >/dev/null
cp "$fixture/initrd.cpio" "$root/boot/initramfs-fixture-1.img"
printf 'DTB mismatch\n' > "$root/boot/fixture.dtb"
printf 'devicetree /fixture.dtb\n' >> "$root/boot/loader/entries/fixture.conf"
"$binary" linux audit --root "$root" > "$fixture/audit.json"
jq -e 'any(.data.findings[]; .code=="invalid-dtb") and (.data.metadata_consistent|not)' "$fixture/audit.json" >/dev/null
printf '%s\n' 'Native gzip/XZ/Zstd, concatenated gzip, early-cpio/compressed-tail, truncated codec and malformed DTB fixtures passed; no kernel execution.'
