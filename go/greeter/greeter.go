// Package greeter is the Go counterpart of //cpp:greeter and //java:lib.
package greeter

// Greet returns a greeting for name, falling back to "world" when empty.
func Greet(name string) string {
	if name == "" {
		name = "world"
	}
	return "Hello, " + name + "!"
}
