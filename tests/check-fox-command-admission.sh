#!/usr/bin/env bash
# Host-only: compile actual command admission and reject its removed guard.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$component"
source_file=src/upstream/orangefox-android16/bootable/recovery/fox_fifo/fox_remote_state.cpp
[[ -f $source_file ]]
work=$(mktemp -d "$component/build/fox-command-admission-XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/fox_fifo"
cp -- "$source_file" "$work/fox_fifo/fox_remote_state.cpp"
cat > "$work/data.hpp" <<'EOF'
#pragma once
#include <cassert>
#include <string>
extern int action_running;
class DataManager {
public:
    static int GetIntValue(const std::string& name) {
        assert(name == "tw_action_thread_running");
        return action_running;
    }
};
EOF
cat > "$work/fox_fifo/fox_remote_state.hpp" <<'EOF'
#pragma once
class Fox_Remote_State {
public:
    static bool CommandActive(bool);
    static bool CanAcceptCommand(bool);
    static bool ShouldForceRender();
    static bool ScreenStreamActive();
    static bool ServerRunning();
    static bool ControlEnabled();
    static int ActiveJob();
};
EOF
cat > "$work/fox_fifo/fox_channel.hpp" <<'EOF'
#pragma once
extern bool fox_active;
class Fox_Channel { public: static bool IsActive() { return fox_active; } };
EOF
cat > "$work/fox_fifo/fox_screen_service.hpp" <<'EOF'
#pragma once
class Fox_Screen_Service { public: static bool ShouldForceRender() { return false; } };
EOF
cat > "$work/fox_fifo/fox_screen_stream.hpp" <<'EOF'
#pragma once
class Fox_Screen_Stream { public:
    static bool ShouldForceRender() { return false; }
    static bool IsActive() { return false; }
};
EOF
compiler="$component/src/upstream/orangefox-android16/prebuilts/clang/host/linux-x86/clang-r547379/bin/clang++"
[[ $(sha256sum "$compiler" | cut -d' ' -f1) == 55d80d777d85327543868817fb836231691eae2e13030ab31a01a769a820bf2f ]]
flags=(-std=c++17 -Wall -Wextra -Werror -UNDEBUG -I "$work")
for flavor in native sanitized; do
    instrumentation=()
    if [[ $flavor == sanitized ]]; then
        instrumentation=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
    fi
    "$compiler" "${flags[@]}" "${instrumentation[@]}" "$work/fox_fifo/fox_remote_state.cpp" \
        tests/ure/fox-command-admission.cpp -o "$work/positive-$flavor"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$work/positive-$flavor"
done
sed '/DataManager::GetIntValue("tw_action_thread_running") != 0;/c\           false;' \
    "$work/fox_fifo/fox_remote_state.cpp" > "$work/fox_fifo/mutant.cpp"
cmp -s "$work/fox_fifo/fox_remote_state.cpp" "$work/fox_fifo/mutant.cpp" && {
    echo 'Admission mutant did not remove the production guard' >&2; exit 1;
}
"$compiler" "${flags[@]}" "$work/fox_fifo/mutant.cpp" \
    tests/ure/fox-command-admission.cpp -o "$work/negative"
if "$work/negative" > "$work/negative.log" 2>&1; then
    echo 'Removed action-owner admission guard unexpectedly passed' >&2; exit 1
fi
grep -q '^FAIL: action ownership admission lost$' "$work/negative.log"
echo 'Actual Fox command admission passed all ownership states; removed guard rejected.'
