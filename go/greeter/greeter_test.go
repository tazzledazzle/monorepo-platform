package greeter

import "testing"

func TestGreet(t *testing.T) {
	cases := map[string]string{
		"Bazel": "Hello, Bazel!",
		"":      "Hello, world!",
	}
	for in, want := range cases {
		if got := Greet(in); got != want {
			t.Errorf("Greet(%q) = %q, want %q", in, got, want)
		}
	}
}
