#!/usr/bin/env bash
# Compile pinned upstream host packages and preserve unknown edits in fixtures.
set -euo pipefail
umask 077
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d "$component/build/host-builder-fixture-XXXXXX")
for source in soong blueprint; do
    git clone --shared --no-checkout "$component/src/upstream/orangefox-android16/build/$source" "$scratch/$source" >/dev/null 2>&1
    git -C "$scratch/$source" checkout --detach >/dev/null 2>&1
    bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/$source" apply "$source"
    bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/$source" check "$source"
done
go="$component/src/upstream/orangefox-android16/prebuilts/go/linux-x86/bin/go"
[[ $(sha256sum "$go" | cut -d' ' -f1) == c4859c0d97fe48a45d348c8ceba892a5c2ca1d7f3e429cf3ea4f2c0dae5cc406 ]]
cat > "$scratch/blueprint/bootstrap/uke_policy_test.go" <<'GO_TEST_EOF'
package bootstrap
import ("runtime"; "testing")
func TestUkeRetainsRuntimePolicy(t *testing.T) {
    before := runtime.GOMAXPROCS(0)
    if before != 2 { t.Fatal("Fixture requires its explicit two-core policy") }
    _, err := RunBlueprint(Args{}, StopBeforePrepareBuildActions, nil, nil)
    if err == nil { t.Fatal("Empty module-list fixture did not return its ordinary validation error") }
    if runtime.GOMAXPROCS(0) != before { t.Fatal("Actual Blueprint entry point overrode the admitted runtime policy") }
}
GO_TEST_EOF
cat > "$scratch/go-job.sh" <<'GO_JOB_EOF'
set -euo pipefail
go=$1
work=$2
export GOWORK=off GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off
export GOCACHE="$work/go-cache" GOMAXPROCS=2 GOMEMLIMIT=1024MiB
cd "$work/blueprint"
"$go" test -p 2 ./bootstrap ./microfactory
GO_JOB_EOF
bash "$component/scripts/with-host-budget.sh" soong-probe bash "$scratch/go-job.sh" "$go" "$scratch" > "$scratch/go-test.log" 2>&1
cat "$scratch/go-test.log"
for source in soong blueprint; do
    file=ui/build/soong.go
    [[ $source == soong ]] || file=bootstrap/command.go
    printf '\n// unknown policy fixture\n' >> "$scratch/$source/$file"
    before=$(sha256sum "$scratch/$source/$file" | cut -d' ' -f1)
    if bash "$component/scripts/prepare-reviewed-patches.sh" "$scratch/$source" apply "$source" > "$scratch/refused-$source.log" 2>&1; then
        echo 'Unknown host builder edit was accepted' >&2; exit 1
    fi
    [[ $(sha256sum "$scratch/$source/$file" | cut -d' ' -f1) == "$before" ]]
done
printf 'Exact pinned host stacks, actual Blueprint policy retention, upstream package compilation/tests and unknown-edit preservation passed.\n'
