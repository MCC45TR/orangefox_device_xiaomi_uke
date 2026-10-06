// SPDX-License-Identifier: Apache-2.0
package proptools

import (
	"strings"
	"testing"
)

func TestRetentionMapValuesAffectHash(t *testing.T) {
	cases := []struct {
		name          string
		before, after any
	}{
		{"scalar_value", map[string]int{"same": 1}, map[string]int{"same": 2}},
		{"nested_value", map[string]map[string]int{"outer": {"inner": 1}}, map[string]map[string]int{"outer": {"inner": 2}}},
		{"nested_key", map[string]map[string]int{"outer": {"before": 1}}, map[string]map[string]int{"outer": {"after": 1}}},
		{"swapped_values", map[string]int{"a": 1, "b": 2}, map[string]int{"a": 2, "b": 1}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			a, b := mustHash(t, tc.before), mustHash(t, tc.after)
			t.Logf("before=%016x after=%016x", a, b)
			if a == b {
				t.Fatal("map value mutation was omitted from hash")
			}
		})
	}
}

func TestRetentionUnsupportedMapValues(t *testing.T) {
	for _, v := range []any{map[string]chan int{"channel": make(chan int)}, map[string]func(){"function": func() {}}, map[string]map[string]chan int{"nested": {"channel": make(chan int)}}} {
		_, err := CalculateHash(v)
		if err == nil || !strings.Contains(err.Error(), "in map:") {
			t.Errorf("unsupported map value %T: expected contextual hashing error, got %v", v, err)
		}
	}
}

func TestRetentionNestedMapDeterminism(t *testing.T) {
	a := map[string]any{"last": map[string]int{"z": 2, "a": 1}, "first": []map[string]int{{"q": 9}, {"b": 3, "a": 4}}, "scalar": 5}
	b := map[string]any{"scalar": 5, "first": []map[string]int{{"q": 9}, {"a": 4, "b": 3}}, "last": map[string]int{"a": 1, "z": 2}}
	expected := mustHash(t, a)
	for i := 0; i < 128; i++ {
		if got := mustHash(t, b); got != expected {
			t.Fatalf("insertion/iteration order changed nested-map hash: %x != %x", got, expected)
		}
		if got := mustHash(t, a); got != expected {
			t.Fatalf("rehashing changed nested-map hash: %x != %x", got, expected)
		}
	}
}
