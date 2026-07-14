package main

import "testing"

func TestSafeVersion(t *testing.T) {
	for _, value := range []string{"dev", "0.1.0", "preview-2", "build_3"} {
		if got, err := safeVersion(value); err != nil || got != value {
			t.Fatalf("safeVersion(%q) = %q, %v", value, got, err)
		}
	}
	for _, value := range []string{"", "../release", "release candidate", "v1/2"} {
		if _, err := safeVersion(value); err == nil {
			t.Fatalf("safeVersion(%q) unexpectedly succeeded", value)
		}
	}
}
