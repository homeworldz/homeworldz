//go:build windows

package main

import (
	"reflect"
	"testing"
)

func TestParseTasklistFindsFirestormRelaunch(t *testing.T) {
	output := []byte("\"FirestormOS-Releasex64.exe\",\"37028\",\"Console\",\"1\",\"1,234 K\"\r\n" +
		"\"unrelated.exe\",\"12\",\"Console\",\"1\",\"10 K\"\r\n")
	got, err := parseTasklist(output, "FirestormOS-Releasex64.exe")
	if err != nil {
		t.Fatal(err)
	}
	if want := []int{37028}; !reflect.DeepEqual(got, want) {
		t.Fatalf("PIDs = %v, want %v", got, want)
	}
}
