package agent

import (
	"strings"
	"testing"

	"example.com/three_languages_one_workspace/proto/analytics/v1/commonpb"
)

func TestInference(t *testing.T) {
	in := "a,b,c,d,e\n1,1,x,1,\n2,2.5,y,z,\n-3,1e3,3,4,\n"
	tb, err := ReadCSV(strings.NewReader(in))
	if err != nil {
		t.Fatal(err)
	}
	want := []commonpb.ColumnType{
		commonpb.ColumnType_COLUMN_TYPE_INT64,
		commonpb.ColumnType_COLUMN_TYPE_DOUBLE,
		commonpb.ColumnType_COLUMN_TYPE_STRING,
		commonpb.ColumnType_COLUMN_TYPE_STRING, // mixed int and text
		commonpb.ColumnType_COLUMN_TYPE_STRING, // empty cells
	}
	for i, w := range want {
		if tb.Types[i] != w {
			t.Errorf("column %s: got %v want %v", tb.Names[i], tb.Types[i], w)
		}
	}
}

func TestNonFiniteIsString(t *testing.T) {
	tb, err := ReadCSV(strings.NewReader("a\nNaN\n1.5\n"))
	if err != nil {
		t.Fatal(err)
	}
	if tb.Types[0] != commonpb.ColumnType_COLUMN_TYPE_STRING {
		t.Fatalf("got %v", tb.Types[0])
	}
}

func TestErrors(t *testing.T) {
	for name, in := range map[string]string{
		"empty":     "",
		"ragged":    "a,b\n1,2\n3\n",
		"dup":       "a,a\n1,2\n",
		"emptyname": "a,\n1,2\n",
	} {
		if _, err := ReadCSV(strings.NewReader(in)); err == nil {
			t.Errorf("%s: expected error", name)
		}
	}
}

func TestHeaderOnly(t *testing.T) {
	tb, err := ReadCSV(strings.NewReader("a,b\n"))
	if err != nil || len(tb.Rows) != 0 || tb.Types[0] != commonpb.ColumnType_COLUMN_TYPE_STRING {
		t.Fatalf("%v %v", tb, err)
	}
	b, _ := tb.Batches()
	if len(b) != 0 {
		t.Fatal("expected no batches")
	}
}

func TestBatchingByRows(t *testing.T) {
	var sb strings.Builder
	sb.WriteString("n\n")
	for i := 0; i < MaxBatchRows*2+5; i++ {
		sb.WriteString("1\n")
	}
	tb, _ := ReadCSV(strings.NewReader(sb.String()))
	bs, err := tb.Batches()
	if err != nil {
		t.Fatal(err)
	}
	if len(bs) != 3 || bs[0].RowCount != MaxBatchRows || bs[2].RowCount != 5 {
		t.Fatalf("got %d batches", len(bs))
	}
	if len(bs[0].Columns[0].GetInt64Values().Values) != MaxBatchRows {
		t.Fatal("vector length")
	}
}

func TestBatchingByBytes(t *testing.T) {
	big := strings.Repeat("x", 100000)
	var sb strings.Builder
	sb.WriteString("s\n")
	for i := 0; i < 25; i++ {
		sb.WriteString(big + "\n")
	}
	tb, _ := ReadCSV(strings.NewReader(sb.String()))
	bs, _ := tb.Batches()
	total := 0
	for _, b := range bs {
		if b.RowCount > 11 {
			t.Errorf("batch too large: %d", b.RowCount)
		}
		total += int(b.RowCount)
	}
	if len(bs) < 3 || total != 25 {
		t.Fatalf("batches=%d total=%d", len(bs), total)
	}
}
