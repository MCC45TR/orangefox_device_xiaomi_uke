#!/usr/bin/env bash
# Host-only source-branch oracle. No image execution or device access.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${UKE_HOST_BUDGET_ACTIVE:-0} != 1 ]]; then
    exec bash "$component/scripts/with-host-budget.sh" native bash "${BASH_SOURCE[0]}" "$@"
fi
[[ ( $# == 1 && $1 == --self-test ) || ( $# == 2 && $1 != --self-test ) ]] || {
    echo 'Usage: check-recovery-boot-chain.sh --self-test | IMAGE REPORT_JSON' >&2; exit 2;
}
if [[ $# == 2 ]]; then
    [[ ! -L $2 && ( ! -e $2 || -f $2 ) && ! $1 -ef $2 ]] || {
        echo 'Report must be a regular file and must not replace the image or a symbolic link.' >&2; exit 1;
    }
fi
source_root="$component/referances/bootloader/abl-aef4e86045bd/QcomModulePkg"
boot="$source_root/Library/BootLib/BootLinux.c"
avb="$source_root/Library/avb/VerifiedBoot.c"
[[ -f $boot && ! -L $boot && -f $avb && ! -L $avb ]] || {
    echo 'The exact-pinned Qualcomm ABL reference is required; boot-chain evidence is unavailable.' >&2; exit 1;
}
[[ $(sha256sum "$boot" | cut -d' ' -f1) == 2b578b0344d7c67fcc1126d3a9453ebb2d59f8a0214721c9a5f87aef06f07a01 &&
   $(sha256sum "$avb" | cut -d' ' -f1) == 3815bae51061fd1f46f5634fbd06e7165c07414bd3a5c41518b38393500d0ebc ]]
headers="$source_root/Include/Library"
while read -r expected name; do
    [[ -f $headers/$name && ! -L $headers/$name && $(sha256sum "$headers/$name" | cut -d' ' -f1) == "$expected" ]]
done <<'PINS'
99db5f96ca3157af25b7e0aa8b083325d1d1922e5f7d84b162a5253ad0338780 BootImage.h
54fb89e06d9998d7f06f8ee9bba9dfeeacc011b2a176913680497874d1096511 BootLinux.h
cc161aa961aa5970e2508f8e5505ba9ce6d6c617471af053b12240f024974ea8 LinuxLoaderLib.h
7f9c76d5ea7d948094207678ce1358f3f3dd6433abef07a8e685ae1ffbbb9fd4 LocateDeviceTree.h
698d71785386a26f21c8a221711e639d3f64410352062c11045a1d055d21a14f UpdateDeviceTree.h
PINS
work=$(mktemp -d "$component/build/recovery-boot-chain-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
# Fixed ranges are valid only after complete source-hash authentication above.
# Compile the original branch conditions and partition additions, not a model
# of their comparisons. Generated excerpts stay in this private host fixture.
{
    printf 'static bool source_selects_recovery(const InfoState* Info, bool Recovery) { return\n'
    sed -n '1633,1637p' "$boot" | sed '$s/?$/;/'
    printf '}\nstatic bool source_substitutes_init_boot(const InfoState* Info) {\n'
    sed -n '1670,1671p' "$boot"
    printf 'return true; } return false; }\nstatic bool source_requires_vendor_boot(const HeaderState* BootImgHdr) {\n'
    sed -n '1709p' "$avb"
    printf 'return true; } return false; }\nstatic std::vector<std::string> source_requested_partitions(const InfoState* Info) {\n'
    printf 'std::vector<std::string> RequestedPartitionAll; const bool HibernationResume=false;\n'
    sed -n '1456,1460p;1463p;1492,1493p;1496,1497p' "$avb"
    printf '} } } return RequestedPartitionAll; }\n'
} > "$work/abl-branches.hpp"
# Keep the original EDK II parser as C. Only its platform types and diagnostics
# are stubbed; its size validation and arithmetic macros are unmodified.
cat > "$work/abl-parser.c" <<'SHIM'
#include <stdint.h>
#include <string.h>
typedef void VOID;
typedef char CHAR8;
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int BOOLEAN;
typedef int EFI_STATUS;
#define TRUE 1
#define FALSE 0
#define ZERO 0
#define MAX_UINT32 UINT32_MAX
#define MAX_UINT64 UINT64_MAX
#define EFI_SUCCESS 0
#define EFI_NO_MEDIA 1
#define EFI_BAD_BUFFER_SIZE 2
#define DEBUG(...) ((void)0)
#define CompareMem memcmp
#include "BootImage.h"
SHIM
sed -n '/^#define ADD_OF(/p' "$headers/LinuxLoaderLib.h" >> "$work/abl-parser.c"
sed -n '/^#define ROUND_TO_PAGE(/p' "$headers/BootLinux.h" >> "$work/abl-parser.c"
sed -n '/^#define CHECK_ADD64(/p' "$headers/UpdateDeviceTree.h" >> "$work/abl-parser.c"
sed -n '/^#define DTBO_MAX_SIZE_ALLOWED /p' "$headers/LocateDeviceTree.h" >> "$work/abl-parser.c"
printf 'EFI_STATUS\n' >> "$work/abl-parser.c"
awk '/^CheckImageHeader \(/ {copying=1} copying {print} copying && /^}$/ {done=1; exit} END {if(!done)exit 1}' \
    "$boot" >> "$work/abl-parser.c"
cat >> "$work/abl-parser.c" <<'SHIM'
int abl_legacy_extent(const void *header, uint32_t *bytes) {
    uint32_t page=0;
    return CheckImageHeader((void *)header,4096,NULL,0,bytes,&page,TRUE,NULL);
}
SHIM
ccache cc -std=gnu11 -O2 -I"$headers" -c "$work/abl-parser.c" -o "$work/abl-parser.o"
ccache c++ -std=c++20 -O2 -Wall -Wextra -Werror -I"$work" \
    "$component/tests/ure/recovery_boot_chain.cpp" "$work/abl-parser.o" -o "$work/check-boot-chain"
"$work/check-boot-chain" --self-test
if [[ $# == 2 ]]; then
    status=0
    "$work/check-boot-chain" "$1" > "$work/result.json" || status=$?
    [[ -s $work/result.json ]] || exit "$status"
    jq --arg image "$(sha256sum -- "$1" | cut -d' ' -f1)" \
        --arg checker "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
        --arg harness "$(sha256sum "$component/tests/ure/recovery_boot_chain.cpp" | cut -d' ' -f1)" \
        '.image_sha256=$image | .checker_sha256=$checker | .harness_sha256=$harness' "$work/result.json" > "$2"
    exit "$status"
else
    jq -n --arg checker "$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)" \
        --arg harness "$(sha256sum "$component/tests/ure/recovery_boot_chain.cpp" | cut -d' ' -f1)" \
        '{schema_version:1,evidence_class:"host-pinned-qualcomm-abl-branches",passed:true,
          source_commit:"aef4e86045bd151a54d33bab8ae5e8e62462a8f4",checker_sha256:$checker,harness_sha256:$harness,
          original_branch_conditions:true,original_legacy_header_parser:true,original_overflow_macros:true,
          validation:{oem_abl_binary_match:false,independent_boot_accepted:false,physical_device:false}}'
fi
