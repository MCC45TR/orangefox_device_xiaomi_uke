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
cp "$component/tests/blueprint/provider_retention_test.go" "$scratch/blueprint/"
cp "$component/tests/blueprint/hash_regression_test.go" "$scratch/blueprint/proptools/"
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
cat > "$scratch/blueprint/uke_graph_policy_test.go" <<'GO_TEST_EOF'
package blueprint

import (
    "fmt"
    "runtime"
    "slices"
    "strings"
    "sync/atomic"
    "testing"
    "time"
)

func TestUkeActiveGraphWorkers(t *testing.T) {
    original := runtime.GOMAXPROCS(0)
    defer runtime.GOMAXPROCS(original)
    for _, admitted := range []int{1, 2, 4} {
        runtime.GOMAXPROCS(admitted)
        if parallelVisitLimit() != admitted {
            t.Fatalf("graph workers did not follow the %d-worker admission", admitted)
        }
        modules := make([]*moduleInfo, 64)
        for i := range modules { modules[i] = &moduleInfo{} }
        entered := make(chan struct{}, len(modules))
        release := make(chan struct{})
        finished := make(chan []error, 1)
        var active, completed atomic.Int32
        go func() {
            finished <- parallelVisit(slices.Values(modules), unorderedVisitorImpl{}, parallelVisitLimit(),
                func(_ *moduleInfo, _ chan<- pauseSpec) bool {
                    active.Add(1)
                    entered <- struct{}{}
                    <-release
                    active.Add(-1)
                    completed.Add(1)
                    return false
                })
        }()
        deadline := time.After(10 * time.Second)
        for i := 0; i < admitted; i++ {
            select {
            case <-entered:
            case <-deadline: close(release); t.Fatal("admitted workers never entered the actual visitor")
            }
        }
        if got := active.Load(); got != int32(admitted) {
            close(release)
            t.Fatalf("actual active visitors = %d, admitted = %d", got, admitted)
        }
        close(release)
        select {
        case errs := <-finished:
            if len(errs) != 0 || completed.Load() != int32(len(modules)) {
                t.Fatalf("bounded visitor lost modules or failed: completed=%d errors=%v", completed.Load(), errs)
            }
        case <-deadline: t.Fatal("bounded visitor did not finish")
        }
    }
}

func TestUkeBoundedProviderValidation(t *testing.T) {
    ctx := NewContext()
    if errs := ctx.VerifyProvidersWereUnchanged(); len(errs) != 1 || errs[0] != ErrBuildActionsNotReady {
        t.Fatal("unprepared provider validation lost its refusal")
    }
    ctx.RegisterModuleType("provider_module", newProviderTestModule)
    ctx.RegisterBottomUpMutator("provider_deps_mutator", providerTestDepsMutator)
    ctx.RegisterBottomUpMutator("provider_mutator", providerTestMutator)
    var source strings.Builder
    for i := 0; i < 64; i++ { fmt.Fprintf(&source, "provider_module { name: \"module-%02d\" }\n", i) }
    ctx.MockFileSystem(map[string][]byte{"Android.bp": []byte(source.String())})
    if _, errs := ctx.ParseBlueprintsFiles("Android.bp", nil); len(errs) != 0 { t.Fatal(errs) }
    if _, errs := ctx.ResolveDependencies(nil); len(errs) != 0 { t.Fatal(errs) }
    if _, errs := ctx.PrepareBuildActions(nil); len(errs) != 0 { t.Fatal(errs) }
    if errs := ctx.VerifyProvidersWereUnchanged(); len(errs) != 0 { t.Fatal(errs) }
    id := providerTestGenerateBuildActionsInfoProvider.id
    for m := range ctx.iterateAllVariants() {
        index, found := m.providerIndex(id)
        if !found { t.Fatal("generated provider was not retained") }
        m.providers[index].value.(*providerTestGenerateBuildActionsInfo).Value += " changed"
    }
    if errs := ctx.VerifyProvidersWereUnchanged(); len(errs) != 64 {
        t.Fatalf("bounded validation failed to find all changed providers: %v", errs)
    }
    first := ctx.moduleGroupFromName("module-00", nil).moduleByVariantName("")
    index, found := first.providerIndex(id)
    if !found { t.Fatal("generated provider was not retained") }
    first.providers[index].value = make(chan bool)
    errs := ctx.VerifyProvidersWereUnchanged()
    if len(errs) != 64 || !strings.Contains(fmt.Sprint(errs), "no longer hashable") {
        t.Fatalf("unhashable-provider refusal was lost: %v", errs)
    }
    first.providers[index].value = nil
    errs = ctx.VerifyProvidersWereUnchanged()
    if len(errs) != 64 || !strings.Contains(fmt.Sprint(errs), "unset somehow") {
        t.Fatalf("unset-provider refusal was lost: %v", errs)
    }
}
GO_TEST_EOF
cat > "$scratch/go-job.sh" <<'GO_JOB_EOF'
set -euo pipefail
go=$1
work=$2
export GOWORK=off GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off
export GOCACHE="$work/go-cache" GOMAXPROCS=2 GOMEMLIMIT=1024MiB
cd "$work/blueprint"
"$go" test -p 2 . ./bootstrap ./microfactory ./proptools
"$go" test -race -p 2 -run 'TestUke|TestRetention|Test_parallelVisit|TestProviders|TestInvalidProvidersUsage' . ./proptools
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
printf 'Exact pinned host stacks, bounded workers, sparse provider/cache/clone compatibility, map mutation checks, dependency/pause regressions, race checks and unknown-edit preservation passed.\n'
