package agent

import (
	"context"
	"io"
	"net"
	"os"
	"strings"
	"testing"

	"example.com/three_languages_one_workspace/proto/analytics/v1/commonpb"
	"example.com/three_languages_one_workspace/proto/analytics/v1/ingestpb"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/status"
	"google.golang.org/grpc/test/bufconn"
)

type fakeIngest struct {
	ingestpb.UnimplementedIngestServiceServer
	header  *ingestpb.LoadHeader
	batches []*ingestpb.RowBatch
	failMsg string
}

func (f *fakeIngest) Load(s grpc.ClientStreamingServer[ingestpb.LoadRequest, ingestpb.LoadResponse]) error {
	var rows uint64
	for i := 0; ; i++ {
		req, err := s.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		if i == 0 {
			if req.GetHeader() == nil {
				return status.Error(codes.InvalidArgument, "first message must be header")
			}
			f.header = req.GetHeader()
			if f.failMsg != "" {
				return status.Error(codes.InvalidArgument, f.failMsg)
			}
			continue
		}
		if req.GetBatch() == nil {
			return status.Error(codes.InvalidArgument, "expected batch")
		}
		f.batches = append(f.batches, req.GetBatch())
		rows += uint64(req.GetBatch().RowCount)
	}
	return s.SendAndClose(&ingestpb.LoadResponse{RowsLoaded: rows})
}

func dial(t *testing.T, f *fakeIngest) *grpc.ClientConn {
	lis := bufconn.Listen(1 << 20)
	srv := grpc.NewServer()
	ingestpb.RegisterIngestServiceServer(srv, f)
	go srv.Serve(lis)
	t.Cleanup(srv.Stop)
	conn, err := grpc.NewClient("passthrough:///bufnet",
		grpc.WithContextDialer(func(context.Context, string) (net.Conn, error) { return lis.Dial() }),
		grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	return conn
}

func TestLoadSalesCSV(t *testing.T) {
	f := &fakeIngest{}
	conn := dial(t, f)
	data, err := os.ReadFile("testdata/sales.csv")
	if err != nil {
		t.Fatal(err)
	}
	n, err := LoadFrom(context.Background(), conn, "sales", strings.NewReader(string(data)))
	if err != nil {
		t.Fatal(err)
	}
	if n != 4 {
		t.Fatalf("rows loaded %d", n)
	}
	if f.header.Dataset != "sales" || len(f.header.Schema.Columns) != 4 {
		t.Fatalf("header %v", f.header)
	}
	want := []commonpb.ColumnType{
		commonpb.ColumnType_COLUMN_TYPE_STRING, commonpb.ColumnType_COLUMN_TYPE_STRING,
		commonpb.ColumnType_COLUMN_TYPE_INT64, commonpb.ColumnType_COLUMN_TYPE_DOUBLE,
	}
	for i, c := range f.header.Schema.Columns {
		if c.Type != want[i] {
			t.Errorf("col %s type %v", c.Name, c.Type)
		}
	}
	if len(f.batches) != 1 || len(f.batches[0].Columns) != 4 {
		t.Fatalf("batches %v", f.batches)
	}
	if got := f.batches[0].Columns[3].GetDoubleValues().Values; got[0] != 100.5 || len(got) != 4 {
		t.Fatalf("revenue %v", got)
	}
}

func TestLoadPropagatesStatus(t *testing.T) {
	f := &fakeIngest{failMsg: "schema mismatch"}
	conn := dial(t, f)
	_, err := LoadFrom(context.Background(), conn, "sales", strings.NewReader("a\n1\n"))
	if status.Code(err) != codes.InvalidArgument || status.Convert(err).Message() != "schema mismatch" {
		t.Fatalf("err %v", err)
	}
}

func TestLoadRaggedCSVSendsNothing(t *testing.T) {
	f := &fakeIngest{}
	conn := dial(t, f)
	_, err := LoadFrom(context.Background(), conn, "sales", strings.NewReader("a,b\n1,2\n3\n"))
	if err == nil || f.header != nil {
		t.Fatalf("err %v header %v", err, f.header)
	}
}
