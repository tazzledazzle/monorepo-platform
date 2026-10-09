package main

import (
	"fmt"

	servicepb "example.com/three_languages_one_workspace/tools/build_defs/tests/proto_fixture/service"
	typespb "example.com/three_languages_one_workspace/tools/build_defs/tests/proto_fixture/types"
)

func main() {
	var _ servicepb.FixtureClient
	r := &servicepb.Reply{P: &typespb.Point{X: 7}}
	if r.P.X != 7 {
		panic("bad")
	}
	fmt.Println("go ok", r.P.X)
}
