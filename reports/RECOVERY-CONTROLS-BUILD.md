# Linux/ESP recovery controls: local build result

30 September 2026. The project-owned `uke-recoveryctl` passed host fixture
tests and compiled as an AArch64 Android 16 recovery executable. The final
incremental `recoveryimage` build completed with the tool inside the recovery
root after removing a utility that was not actually packaged. The
ext4/FAT and GPT utilities named in [Linux/ESP tools](../docs/LINUX-ESP-TOOLS.md)
were also present in that root. The ZIP passed `unzip -t`; the extracted target
passed the no-Python executable/script payload check. The ZIP passed
`unzip -t` again after the final build.

| Private local artifact | Size | SHA-256 |
|---|---:|---|
| `recovery.img` | 104,857,600 bytes | `1c5b89fadd9c1e59a75d20836cc731a0a3832289678c5acbb9b9ac9ec74fe074` |
| OrangeFox ZIP | 52,467,303 bytes | `5d89c6386ba8ed2a1bba1aa0b02ffb6859c952a0d6b4f1811595d3f15ce50040` |
| `uke-recoveryctl` in extracted root | 69,192 bytes | `de5969d3aceef4c7e02a884ad0905773b5d4df46af019a6c97c374327f247e8e` |

This is **not a release**. The extracted-root privacy audit still rejects
build paths and the build-account identity. `avbtool verify_image` checks the
internal recovery hash, but the footer's AVB algorithm is `NONE`, not an
authenticated signature. The image has no embedded kernel and is not a
validated `fastboot boot` artifact. ZIP installer target safety, bootloader
acceptance, rotation/touch, A/B merge, userdata decryption and both tablet
models remain untested. No image or ZIP from this run is uploaded for flashing.
