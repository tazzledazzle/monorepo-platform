package main

import (
	"os"
	"strings"
	"testing"
)

func TestUsageExit2(t *testing.T) {
	f, _ := os.CreateTemp(t.TempDir(), "err")
	if code := run([]string{"--dataset=x"}, f, f); code != 2 {
		t.Fatalf("code %d", code)
	}
	b, _ := os.ReadFile(f.Name())
	if !strings.Contains(string(b), "usage:") {
		t.Fatalf("stderr %q", b)
	}
}

func TestBadFileExit1(t *testing.T) {
	f, _ := os.CreateTemp(t.TempDir(), "err")
	if code := run([]string{"--engine=localhost:1", "--dataset=x", "/nonexistent.csv"}, f, f); code != 1 {
		t.Fatalf("code %d", code)
	}
}
