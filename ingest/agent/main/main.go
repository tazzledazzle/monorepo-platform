// Command agent loads a CSV file into an engine dataset.
//
//	agent --engine=host:port --dataset=name file.csv
package main

import (
	"context"
	"flag"
	"fmt"
	"os"

	"example.com/three_languages_one_workspace/ingest/agent"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/status"
)

func main() { os.Exit(run(os.Args[1:], os.Stdout, os.Stderr)) }

func run(args []string, stdout, stderr *os.File) int {
	fs := flag.NewFlagSet("agent", flag.ContinueOnError)
	fs.SetOutput(stderr)
	engine := fs.String("engine", "", "engine address host:port (required)")
	dataset := fs.String("dataset", "", "target dataset name (required)")
	if err := fs.Parse(args); err != nil {
		return 2
	}
	if *engine == "" || *dataset == "" || fs.NArg() != 1 {
		fmt.Fprintln(stderr, "usage: agent --engine=host:port --dataset=name file.csv")
		return 2
	}
	conn, err := grpc.NewClient(*engine, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		fmt.Fprintf(stderr, "agent: %v\n", err)
		return 1
	}
	defer conn.Close()
	n, err := agent.Load(context.Background(), conn, *dataset, fs.Arg(0))
	if err != nil {
		if s, ok := status.FromError(err); ok {
			fmt.Fprintf(stderr, "agent: %s: %s\n", s.Code(), s.Message())
		} else {
			fmt.Fprintf(stderr, "agent: %v\n", err)
		}
		return 1
	}
	fmt.Fprintf(stdout, "loaded %d rows into %s\n", n, *dataset)
	return 0
}
