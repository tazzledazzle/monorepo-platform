// Package agent reads CSV files and streams them to the engine's IngestService.
package agent

import (
	"encoding/csv"
	"fmt"
	"io"
	"math"
	"strconv"
	"strings"

	"example.com/three_languages_one_workspace/proto/analytics/v1/commonpb"
	"example.com/three_languages_one_workspace/proto/analytics/v1/ingestpb"
)

// Batch limits from docs/proto-compatibility.md: 8192 rows or about 1 MiB, whichever is smaller.
const (
	MaxBatchRows  = 8192
	MaxBatchBytes = 1 << 20
)

// Table is a parsed CSV: column names, inferred types, and rows as raw strings.
type Table struct {
	Names []string
	Types []commonpb.ColumnType
	Rows  [][]string
}

// ReadCSV parses CSV with a header row and infers a type per column.
//
// Inference rule (per column, over all data rows):
//   - INT64 if every cell parses as a base-10 int64 (after trimming spaces);
//   - otherwise DOUBLE if every cell parses as a finite float64 (so a column mixing
//     ints and decimals is DOUBLE; "NaN"/"Inf" literals do not count);
//   - otherwise STRING (a column mixing numbers and text, or containing an empty
//     cell, is STRING, because v1 has no NULLs). Strings are sent verbatim.
//
// A column of a header-only file is STRING. A ragged row, an empty header name,
// a duplicate header name, or an empty file is an error.
func ReadCSV(r io.Reader) (*Table, error) {
	cr := csv.NewReader(r)
	cr.FieldsPerRecord = 0 // fixed by the header; ragged rows are errors
	header, err := cr.Read()
	if err == io.EOF {
		return nil, fmt.Errorf("empty CSV: missing header row")
	}
	if err != nil {
		return nil, fmt.Errorf("reading header: %w", err)
	}
	seen := map[string]bool{}
	names := make([]string, len(header))
	for i, h := range header {
		if i == 0 {
			h = strings.TrimPrefix(h, "\uFEFF")
		}
		if h == "" {
			return nil, fmt.Errorf("header column %d has an empty name", i+1)
		}
		if seen[h] {
			return nil, fmt.Errorf("duplicate header column %q", h)
		}
		seen[h] = true
		names[i] = h
	}
	t := &Table{Names: names}
	for {
		rec, err := cr.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("reading CSV: %w", err)
		}
		t.Rows = append(t.Rows, rec)
	}
	t.Types = inferTypes(len(names), t.Rows)
	return t, nil
}

func inferTypes(n int, rows [][]string) []commonpb.ColumnType {
	types := make([]commonpb.ColumnType, n)
	for c := 0; c < n; c++ {
		isInt, isFloat := true, true
		for _, row := range rows {
			s := strings.TrimSpace(row[c])
			if isInt {
				if _, err := strconv.ParseInt(s, 10, 64); err != nil {
					isInt = false
				}
			}
			if isFloat {
				if f, err := strconv.ParseFloat(s, 64); err != nil || math.IsNaN(f) || math.IsInf(f, 0) {
					isFloat = false
				}
			}
			if !isInt && !isFloat {
				break
			}
		}
		switch {
		case len(rows) == 0:
			types[c] = commonpb.ColumnType_COLUMN_TYPE_STRING
		case isInt:
			types[c] = commonpb.ColumnType_COLUMN_TYPE_INT64
		case isFloat:
			types[c] = commonpb.ColumnType_COLUMN_TYPE_DOUBLE
		default:
			types[c] = commonpb.ColumnType_COLUMN_TYPE_STRING
		}
	}
	return types
}

// Schema returns the proto schema for the table.
func (t *Table) Schema() *commonpb.Schema {
	s := &commonpb.Schema{}
	for i, n := range t.Names {
		s.Columns = append(s.Columns, &commonpb.Column{Name: n, Type: t.Types[i]})
	}
	return s
}

// rowBytes estimates the wire size of one row.
func (t *Table) rowBytes(row []string) int {
	n := 0
	for i, ty := range t.Types {
		if ty == commonpb.ColumnType_COLUMN_TYPE_STRING {
			n += len(row[i]) + 2
		} else {
			n += 8
		}
	}
	return n
}

// Batches pivots rows into column vectors, at most MaxBatchRows rows or about
// MaxBatchBytes per batch.
func (t *Table) Batches() ([]*ingestpb.RowBatch, error) {
	var out []*ingestpb.RowBatch
	for start := 0; start < len(t.Rows); {
		end, size := start, 0
		for end < len(t.Rows) && end-start < MaxBatchRows {
			rb := t.rowBytes(t.Rows[end])
			if end > start && size+rb > MaxBatchBytes {
				break
			}
			size += rb
			end++
		}
		b, err := t.pivot(start, end)
		if err != nil {
			return nil, err
		}
		out = append(out, b)
		start = end
	}
	return out, nil
}

func (t *Table) pivot(start, end int) (*ingestpb.RowBatch, error) {
	cols := make([]*commonpb.ColumnData, len(t.Names))
	for c, ty := range t.Types {
		switch ty {
		case commonpb.ColumnType_COLUMN_TYPE_INT64:
			v := make([]int64, 0, end-start)
			for r := start; r < end; r++ {
				x, err := strconv.ParseInt(strings.TrimSpace(t.Rows[r][c]), 10, 64)
				if err != nil {
					return nil, fmt.Errorf("row %d column %q: %w", r+2, t.Names[c], err)
				}
				v = append(v, x)
			}
			cols[c] = &commonpb.ColumnData{Values: &commonpb.ColumnData_Int64Values{Int64Values: &commonpb.Int64Vector{Values: v}}}
		case commonpb.ColumnType_COLUMN_TYPE_DOUBLE:
			v := make([]float64, 0, end-start)
			for r := start; r < end; r++ {
				x, err := strconv.ParseFloat(strings.TrimSpace(t.Rows[r][c]), 64)
				if err != nil {
					return nil, fmt.Errorf("row %d column %q: %w", r+2, t.Names[c], err)
				}
				v = append(v, x)
			}
			cols[c] = &commonpb.ColumnData{Values: &commonpb.ColumnData_DoubleValues{DoubleValues: &commonpb.DoubleVector{Values: v}}}
		default:
			v := make([]string, 0, end-start)
			for r := start; r < end; r++ {
				v = append(v, t.Rows[r][c])
			}
			cols[c] = &commonpb.ColumnData{Values: &commonpb.ColumnData_StringValues{StringValues: &commonpb.StringVector{Values: v}}}
		}
	}
	return &ingestpb.RowBatch{RowCount: uint32(end - start), Columns: cols}, nil
}
