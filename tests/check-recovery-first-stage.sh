#!/usr/bin/env bash
# Compile the actual production selector; no device or filesystem mount is used.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_path="$component/src/upstream/orangefox-android16/system/core/init/first_stage_init.cpp"
work=$(mktemp -d "$component/build/first-stage-control-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
{
    printf '%s\n' '#include "first-stage-mocks.h"'
    awk '
      /^bool ForceNormalBoot\(const std::string& cmdline, const std::string& bootconfig\) \{/ { if(seen++) exit 1; copying=1 }
      copying { print; if($0=="}") { copying=0; completed++ } }
      END { if(seen!=1 || completed!=1 || copying) exit 2 }
    ' "$source_path"
    awk '
      /^static void MaybeResumeFromHibernation\(const std::string& bootconfig\) \{/ { if(seen++) exit 1; copying=1 }
      copying { print; if($0=="}") { copying=0; completed++ } }
      END { if(seen!=1 || completed!=1 || copying) exit 2 }
    ' "$source_path"
    printf '%s\n' 'void InvokeResumeControl(const std::string& config) { MaybeResumeFromHibernation(config); }'
} > "$work/selector.cpp"
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ -x $compiler ]]
flags=(-std=c++20 -stdlib=libstdc++ -Wall -Wextra -Werror -Wl,--wrap=open
    -I"$component/tests/ure" -I"$component/src/upstream/orangefox-android16/system/libbase/include")
"$compiler" "${flags[@]}" "$component/tests/ure/recovery_first_stage.cpp" \
    "$work/selector.cpp" -o "$work/test"
"$work/test"
sed '/if (IsRecoveryMode()) return false;/d' "$work/selector.cpp" > "$work/mutant.cpp"
"$compiler" "${flags[@]}" "$component/tests/ure/recovery_first_stage.cpp" \
    "$work/mutant.cpp" -o "$work/mutant"
if "$work/mutant" > "$work/mutant.log" 2>&1; then
    echo 'First-stage control accepted the unguarded production selector' >&2; exit 1
fi
rg -q 'Recovery root was discarded for a normal-boot marker' "$work/mutant.log"
echo 'The original normal-boot selector fails the recovery-root control.'
sed '/if (IsRecoveryMode()) return;/d' "$work/selector.cpp" > "$work/resume-mutant.cpp"
"$compiler" "${flags[@]}" "$component/tests/ure/recovery_first_stage.cpp" \
    "$work/resume-mutant.cpp" -o "$work/resume-mutant"
if "$work/resume-mutant" > "$work/resume-mutant.log" 2>&1; then
    echo 'First-stage control accepted unguarded hibernation resume' >&2; exit 1
fi
rg -q 'Recovery attempted hibernation resume before refusal' "$work/resume-mutant.log"
echo 'Unguarded recovery resume fails the intercepted sysfs control.'
