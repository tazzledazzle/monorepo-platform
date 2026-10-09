// Command hello prints a greeting: bazel run //go/cmd/hello -- Bazel
package main

import (
	"fmt"
	"os"

	"example.com/three_languages_one_workspace/go/greeter"
)

func main() {
	name := ""
	if len(os.Args) > 1 {
		name = os.Args[1]
	}
	fmt.Println(greeter.Greet(name))
}
