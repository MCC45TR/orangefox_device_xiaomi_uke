# OEM module metadata fixture

These text-only files come from the verified `vendor_boot.img` in the Global
OS3.0.303.0.WOZMIXM fastboot archive. They retain the OEM recovery load order,
aliases and complete dependency graph so host tests can detect indirect loads.
No module binary, proprietary firmware, device dump or unit identifier is here.
Tests create ordinary placeholder files and intercept every module syscall.

- Firmware archive SHA-256: `f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d`.
- Vendor boot image SHA-256: `c2811677d6aa07753b615747c4f2dba110dd4519cf52ff3a89e41e00a5b02bcc`.
- Image and capacity catalog: `manifests/stock-boot-programming-global.json`.
- Acquisition source: the OEM download URL in `manifests/firmware.lock.json`.
- Text content digests: `SHA256SUMS`.

The lists are dependency/load metadata, not executable scripts or authorization
to load any driver. The matched modules remain in the OEM source archive. A
successful loader fixture does not establish kernel ABI, charging, thermal,
display, touch, USB or physical storage safety.
