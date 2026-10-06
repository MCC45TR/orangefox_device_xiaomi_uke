// SPDX-License-Identifier: Apache-2.0
package blueprint

import (
	"bytes"
	"encoding/gob"
	"encoding/json"
	"fmt"
	"reflect"
	"runtime"
	"strings"
	"testing"

	"github.com/google/blueprint/proptools"
)

func retentionHashes(m *moduleInfo) []uint64 { return m.providerInitialValueHashes() }

type retentionFixture struct{ Value string }

func (r retentionFixture) AddJSONData(d *map[string]interface{}) { (*d)["retention"] = r.Value }
func (r retentionFixture) JSONActions() []JSONAction             { return []JSONAction{{Desc: r.Value}} }
func (r retentionFixture) GetDebugString() string                { return "debug:" + r.Value }

var retentionKeys []ProviderKey[retentionFixture]
var retentionMapKey = NewProvider[map[string]int]()
var retentionAnyKey = NewProvider[retentionAnyFixture]()

type retentionNilFixture struct{ Value string }

var retentionTypedNilKey = NewProvider[*retentionNilFixture]()

type retentionAnyFixture struct{ Value any }

func init() {
	// Deliberately cover a large registry and high, noncontiguous provider IDs.
	for i := 0; i < 512; i++ {
		retentionKeys = append(retentionKeys, NewProvider[retentionFixture]())
	}
}

func retentionSetup() (*Context, *moduleInfo) {
	c := NewContext()
	logic, properties := newProviderTestModule()
	logic.(*providerTestModule).Properties.Name = "retention"
	m := &moduleInfo{logicModule: logic, properties: properties, factory: newProviderTestModule, startedGenerateBuildActions: true}
	group := &moduleGroup{name: "retention", modules: moduleList{m}}
	m.group = group
	c.moduleGroups = append(c.moduleGroups, group)
	c.buildActionsReady = true
	c.buildActionsCache = BuildActionCache{}
	return c, m
}

func retentionMustPanic(t *testing.T, contains string, f func()) {
	t.Helper()
	defer func() {
		r := recover()
		if r == nil || !strings.Contains(fmt.Sprint(r), contains) {
			t.Errorf("expected panic containing %q, got %v", contains, r)
		}
	}()
	f()
}

func TestRetentionProviderBoundaryEquivalence(t *testing.T) {
	c, m := retentionSetup()
	if got := retentionHashes(m); got != nil {
		t.Fatalf("empty hash vector should be nil, got %v", got)
	}
	// Highest first, then first and middle. Iteration must still follow registry order.
	indexes := []int{511, 0, 255, 1, 510}
	dense := make([]uint64, len(providerRegistry))
	for _, index := range indexes {
		key := retentionKeys[index].provider()
		v := retentionFixture{Value: fmt.Sprint(index)}
		c.setProvider(m, key, v)
		dense[key.id], _ = proptools.CalculateHash(v)
	}
	if got := retentionHashes(m); !reflect.DeepEqual(got, dense) {
		t.Fatal("legacy dense hash layout changed")
	}
	expectedInput := &BuildActionCacheInput{PropertiesHash: 17, ProvidersHash: [][]uint64{nil, dense}}
	actualInput := &BuildActionCacheInput{PropertiesHash: 17, ProvidersHash: [][]uint64{nil, retentionHashes(m)}}
	expectedHash, _ := proptools.CalculateHash(expectedInput)
	actualHash, _ := proptools.CalculateHash(actualInput)
	if actualHash != expectedHash {
		t.Fatalf("cache input hash changed: %x != %x", actualHash, expectedHash)
	}
	m.finishedGenerateBuildActions = true
	for i, key := range retentionKeys {
		value, ok := c.provider(m, key.provider())
		expected := false
		for _, index := range indexes {
			if index == i {
				expected = true
			}
		}
		if ok != expected || c.hasProvider(m, key.provider()) != expected {
			t.Fatalf("ID %d: unexpected provider presence %v", key.id, ok)
		}
		if expected && value != (retentionFixture{Value: fmt.Sprint(i)}) {
			t.Fatalf("wrong provider value for ID %d: %v", key.id, value)
		}
	}
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 0 {
		t.Fatal(errs)
	}
	t.Logf("registry=%d populated=%d retained_provider_bytes=%d", len(providerRegistry), len(indexes), retentionStorage(m))
}

func TestRetentionNilAndDuplicateSemantics(t *testing.T) {
	c, m := retentionSetup()
	c.setProvider(m, retentionTypedNilKey.provider(), (*retentionNilFixture)(nil))
	m.finishedGenerateBuildActions = true
	value, ok := c.provider(m, retentionTypedNilKey.provider())
	if !ok || value != (*retentionNilFixture)(nil) {
		t.Fatal("typed nil provider must remain set")
	}
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 0 {
		t.Fatal(errs)
	}
	m.finishedGenerateBuildActions = false
	retentionMustPanic(t, "already set", func() { c.setProvider(m, retentionTypedNilKey.provider(), &retentionNilFixture{}) })
	// Preserve the existing untyped-nil sentinel behavior, including internal-error verification.
	c, m = retentionSetup()
	c.setProvider(m, retentionKeys[0].provider(), nil)
	m.finishedGenerateBuildActions = true
	if _, ok := c.provider(m, retentionKeys[0].provider()); ok {
		t.Fatal("untyped nil must remain unset")
	}
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 1 || !strings.Contains(errs[0].Error(), "unset somehow") {
		t.Fatalf("nil-sentinel verification changed: %v", errs)
	}
	m.finishedGenerateBuildActions = false
	c.setProvider(m, retentionKeys[0].provider(), retentionFixture{Value: "replacement"})
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 0 {
		t.Fatal(errs)
	}
}

func TestRetentionProviderMutationDetection(t *testing.T) {
	c, m := retentionSetup()
	v := map[string]int{"same-key": 1}
	c.setProvider(m, retentionMapKey.provider(), v)
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 0 {
		t.Fatal(errs)
	}
	v["same-key"] = 2
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 1 || !strings.Contains(errs[0].Error(), "modified after being set") {
		t.Fatalf("map mutation must be detected: %v", errs)
	}
}

func TestRetentionConfigurableAndUnhashableRejection(t *testing.T) {
	c, m := retentionSetup()
	retentionMustPanic(t, "Providers can't contain Configurable", func() {
		c.setProvider(m, retentionAnyKey.provider(), retentionAnyFixture{Value: proptools.Configurable[string]{}})
	})
	c, m = retentionSetup()
	before := runtime.NumGoroutine()
	for i := 0; i < 24; i++ {
		c, m = retentionSetup()
		retentionMustPanic(t, "Can't set value of provider", func() { c.setProvider(m, retentionAnyKey.provider(), retentionAnyFixture{Value: make(chan int)}) })
	}
	if added := runtime.NumGoroutine() - before; added > 2 {
		t.Fatalf("hashing panics leaked %d configurable-check goroutines", added)
	}
	// An explicitly allowed configurable field must retain its upstream exemption.
	c, m = retentionSetup()
	v := struct {
		Value proptools.Configurable[string] `blueprint:"allow_configurable_in_provider"`
	}{}
	c.setProvider(m, retentionAnyKey.provider(), retentionAnyFixture{Value: v})
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 0 {
		t.Fatal(errs)
	}
}

func TestRetentionVariantCloneIndependence(t *testing.T) {
	c, original := retentionSetup()
	c.setProvider(original, retentionKeys[511].provider(), retentionFixture{Value: "shared"})
	variants, errs := c.createVariations(original, &mutatorInfo{name: "split", transitionMutator: &transitionMutatorImpl{}}, nil, []string{"left", "right"})
	if len(errs) > 0 {
		t.Fatal(errs)
	}
	for _, variant := range variants {
		variant.finishedGenerateBuildActions = true
		v, ok := c.provider(variant, retentionKeys[511].provider())
		if !ok || v != (retentionFixture{Value: "shared"}) {
			t.Fatal("variant lost inherited provider")
		}
	}
	variants[0].finishedGenerateBuildActions = false
	c.setProvider(variants[0], retentionKeys[0].provider(), retentionFixture{Value: "left-only"})
	variants[0].finishedGenerateBuildActions = true
	original.finishedGenerateBuildActions = true
	if _, ok := c.provider(variants[1], retentionKeys[0].provider()); ok {
		t.Fatal("sibling provider storage was aliased")
	}
	if _, ok := c.provider(original, retentionKeys[0].provider()); ok {
		t.Fatal("parent provider storage was aliased")
	}
	if retentionHashes(variants[1])[retentionKeys[0].id] != 0 || retentionHashes(original)[retentionKeys[0].id] != 0 {
		t.Fatal("variant hash storage was aliased")
	}
}

func TestRetentionSerializationAndJSONOrder(t *testing.T) {
	c, m := retentionSetup()
	c.setProvider(m, retentionKeys[511].provider(), retentionFixture{Value: "last"})
	c.setProvider(m, retentionKeys[0].provider(), retentionFixture{Value: "first"})
	j := jsonModuleFromModuleInfo(m)
	if j.Module["retention"] != "last" {
		t.Fatalf("JSON provider override order changed: %v", j.Module)
	}
	actions := jsonModuleWithActionsFromModuleInfo(m, nil).Module["Actions"].([]JSONAction)
	if !reflect.DeepEqual(actions, []JSONAction{{Desc: "first"}, {Desc: "last"}}) {
		t.Fatalf("JSON action order changed: %v", actions)
	}
	var debug struct{ Providers []struct{ Debug string } }
	if err := json.Unmarshal(getModuleDebugJson(m), &debug); err != nil {
		t.Fatal(err)
	}
	if len(debug.Providers) != 2 || debug.Providers[0].Debug != "debug:first" || debug.Providers[1].Debug != "debug:last" {
		t.Fatalf("debug provider order/type changed: %v", debug)
	}
	key := &BuildActionCacheKey{Id: "retention", InputHash: 19}
	m.buildActionCacheKey = key
	c.cacheModuleBuildActions(m)
	data := c.buildActionsCache[*key]
	if len(data.Providers) != 2 || data.Providers[0].Id.id != retentionKeys[0].id || data.Providers[1].Id.id != retentionKeys[511].id {
		t.Fatalf("cache provider order/IDs changed: %v", data.Providers)
	}
	var buf bytes.Buffer
	if err := gob.NewEncoder(&buf).Encode(data); err != nil {
		t.Fatal(err)
	}
	var restored BuildActionCachedData
	if err := gob.NewDecoder(&buf).Decode(&restored); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(restored, *data) {
		t.Fatal("gob cache round trip changed provider IDs/values")
	}
}

func retentionStorage(m *moduleInfo) uint64 {
	v := reflect.ValueOf(m).Elem()
	var bytes uint64
	for _, name := range []string{"providers", "providerInitialValueHashes"} {
		f := v.FieldByName(name)
		if f.IsValid() && f.Kind() == reflect.Slice {
			bytes += uint64(f.Type().Size()) + uint64(f.Cap())*uint64(f.Type().Elem().Size())
		}
	}
	return bytes
}

func TestRetentionStorageMeasurement(t *testing.T) {
	for _, count := range []int{1, 8, 32, 512} {
		c, m := retentionSetup()
		for i := 0; i < count; i++ {
			c.setProvider(m, retentionKeys[511-i].provider(), retentionFixture{Value: "constant"})
		}
		t.Logf("registry=%d populated=%d retained_provider_bytes=%d module_info_bytes=%d", len(providerRegistry), count, retentionStorage(m), reflect.TypeOf(*m).Size())
	}
}

func TestRetentionProviderBecomesUnhashable(t *testing.T) {
	c, m := retentionSetup()
	v := &retentionAnyFixture{Value: "initial"}
	c.setProvider(m, retentionAnyKey.provider(), retentionAnyFixture{Value: v})
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 0 {
		t.Fatal(errs)
	}
	v.Value = make(chan int)
	if errs := c.VerifyProvidersWereUnchanged(); len(errs) != 1 || !strings.Contains(errs[0].Error(), "no longer hashable afterwards") {
		t.Fatalf("unhashable mutation must remain detectable: %v", errs)
	}
}

func TestRetentionNestedConfigurableRejection(t *testing.T) {
	c, m := retentionSetup()
	v := retentionAnyFixture{Value: map[string]any{"nested": []any{proptools.Configurable[string]{}}}}
	retentionMustPanic(t, "Providers can't contain Configurable", func() { c.setProvider(m, retentionAnyKey.provider(), v) })
}
