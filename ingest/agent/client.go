package agent

import (
	"context"
	"fmt"
	"io"
	"os"
	"time"

	"example.com/three_languages_one_workspace/proto/analytics/v1/ingestpb"
	"google.golang.org/grpc"
)

// Load streams the CSV file to the engine: header first, then batches. It returns
// the engine's rows_loaded. A deadline of 30s plus 1s per 100k rows is applied.
func Load(ctx context.Context, conn grpc.ClientConnInterface, dataset, path string) (uint64, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, err
	}
	defer f.Close()
	return LoadFrom(ctx, conn, dataset, f)
}

// LoadFrom is Load for an arbitrary reader.
func LoadFrom(ctx context.Context, conn grpc.ClientConnInterface, dataset string, r io.Reader) (uint64, error) {
	t, err := ReadCSV(r)
	if err != nil {
		return 0, err
	}
	batches, err := t.Batches()
	if err != nil {
		return 0, err
	}
	timeout := 30*time.Second + time.Duration(len(t.Rows)/100000)*time.Second
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	stream, err := ingestpb.NewIngestServiceClient(conn).Load(ctx)
	if err != nil {
		return 0, err
	}
	header := &ingestpb.LoadRequest{Payload: &ingestpb.LoadRequest_Header{
		Header: &ingestpb.LoadHeader{Dataset: dataset, Schema: t.Schema()},
	}}
	reqs := []*ingestpb.LoadRequest{header}
	for _, b := range batches {
		reqs = append(reqs, &ingestpb.LoadRequest{Payload: &ingestpb.LoadRequest_Batch{Batch: b}})
	}
	for _, req := range reqs {
		if err := stream.Send(req); err != nil {
			if err == io.EOF {
				// The server ended the stream early; the real status comes from CloseAndRecv.
				break
			}
			return 0, fmt.Errorf("send: %w", err)
		}
	}
	resp, err := stream.CloseAndRecv()
	if err != nil {
		return 0, err
	}
	return resp.GetRowsLoaded(), nil
}
