# Proto compatibility rules and wire-contract conventions (analytics.v1)

Status: decided (spike tlow-a6j.1.1). Applies to `proto/analytics/v1/*.proto`.

The contracts are the only coupling between the four teams (engine C++, server JVM,
ingest Go, reports Python). Old and new binaries talk to each other during a rollout, so
every change in v1 must be safe for both directions.

## Package and versioning

- Proto package `analytics.v1`; files live in `proto/analytics/v1/`; imports use the path
  `analytics/v1/<file>.proto` (the `proto/` directory is the proto root).
- The version is part of the package, so service names on the wire
  (`/analytics.v1.QueryService/Execute`) change with it.
- Within v1, changes are **additive only**. A breaking change creates `analytics.v2`
  (new directory, new package). v1 and v2 are served side by side until every client moves;
  only then is v1 deleted.
- Every file has `syntax = "proto3"` and one top-level concern (common, ingest, query, catalog).
  Only `common.proto` is imported by the others; the other three do not import each other.

## Language options

| Option | Value | Why |
|---|---|---|
| `go_package` | `example.com/three_languages_one_workspace/proto/analytics/v1/<file>pb;<file>pb` | The import path is under the Go module in `go.mod`, as the codegen spike (tlow-a6j.10.1) required. One Go package per proto file (`commonpb`, `ingestpb`, `querypb`, `catalogpb`): the Bazel macro creates one `go_proto_library` per `proto_library`, and two libraries with the same importpath would collide when `ingest.proto` imports `common.proto`. `analytics_proto` must set `importpath` to exactly the `go_package` path, and Gazelle must resolve to the same strings. |
| `java_package` | `com.example.analytics.v1` | Version in the package, like the proto package. |
| `java_multiple_files` | `true` | One class per message; Kotlin and Java callers import messages directly. |
| `java_outer_classname` | `CommonProto`, `IngestProto`, `QueryProto`, `CatalogProto` | Explicit, so the default (derived from file name) cannot clash with a message name. |

C++, Python, and Kotlin derive their names from the package and need no options. Changing
any of these options after release changes generated symbols in that language, so it is a
breaking change for source (not wire) compatibility and follows the v2 rule.

## Field numbering and reservation

1. A field number is never reused for a different meaning, ever, in any message, even after
   the field is deleted.
2. Deleting a field or enum value: keep the slot with `reserved <number>;` **and**
   `reserved "<name>";` in the same change.
3. Numbers 1 to 15 take one byte of tag: use them for fields that are always or usually set.
   Do not "save" low numbers for later; the next free number is fine.
4. Never change a field's type, label (`repeated` or singular), or move it into or out of a
   `oneof`. Add a new field and deprecate the old one (`[deprecated = true]`, documented in
   its comment) instead.
5. New fields must be safe to omit: receivers treat the default value as "client predates this
   field". Senders must tolerate receivers that ignore it.
6. Renaming a field is wire-safe but breaks generated code and JSON; treat it as breaking.
7. Do not use `required` (proto3 has none), `group`, or `extensions`.

## Allowed vs. breaking within v1

| Change | Allowed in v1 |
|---|---|
| Add a message, enum, service, or RPC | yes |
| Add a field (new number) | yes |
| Add an enum value | yes; receivers must handle unknown values (proto3 enums are open) |
| Add a member to an existing `oneof` | yes; receivers must handle "no member known" |
| Add a Schema column type or a ColumnData vector type | yes, same as above |
| Remove a field or enum value | only with `reserved`, and only after all producers stopped sending it |
| Change a field number, type, or cardinality | no |
| Move a field in or out of a `oneof` | no |
| Change RPC streaming mode, request or response type | no |
| Rename a package, service, RPC, message, or field | no |
| Tighten validation (reject input accepted before) | no, treat as breaking |
| Change option values listed above | no |

Automated enforcement (`buf breaking` or an alternative) is Phase 14 (tlow-6p3.2.1); until
then reviewers apply this table.

## Enums

- Zero value is `<ENUM_NAME>_UNSPECIFIED = 0`. It means "not set" and is **never valid input**:
  servers reject it with `INVALID_ARGUMENT`. This catches clients built against an older
  schema that omit the field.
- Values are prefixed with the enum name in upper snake case (`COLUMN_TYPE_INT64`, not `INT64`)
  because enum values live in the enclosing package scope in C++ and proto, so unprefixed
  names such as `COUNT` or `STRING` would collide across enums.
- Receivers must tolerate unknown values (open enums) and answer `INVALID_ARGUMENT`, not crash.

## Cell value representation (decision)

**Decision: typed, packed column vectors (`ColumnData` with a `oneof` of `Int64Vector`,
`DoubleVector`, `StringVector`), and no per-cell `Value` message.** A batch of rows is sent
as one vector per column, in schema order.

Alternatives considered:

| Option | Wire size | Engine ingest cost | Verdict |
|---|---|---|---|
| Row-wise `repeated Row { repeated Value }`, `Value` = `oneof {int64, double, string}` | Largest: a tag and length per cell, a oneof on every cell | One heap object per cell in most runtimes; the engine must transpose rows into its columnar table | Rejected |
| Typed packed column vectors (chosen) | Smallest: `repeated int64` and `repeated double` are packed (one tag per vector, raw varints or 8-byte doubles) | The vectors map nearly 1:1 onto the engine's in-memory columnar `engine/core` table: append per column, no transposing, no per-cell type dispatch | Chosen |
| Opaque bytes (Arrow IPC) | Compact | Adds an Arrow dependency in five languages | Out of scope for M1 |

Why it fits this system:

- `engine/core` is an in-memory columnar table and aggregations scan one column at a time.
  Ingest as columns means the type check happens once per column per batch, not per cell.
- Query results use the same `ColumnData`, so the Go agent, the Kotlin gateway, and the Python CLI
  share one vector layout.
  Python can build rows from `zip(*columns)` only at print time.
- The cost is that a client must pivot CSV rows into column vectors before sending. The Go
  agent does this per batch of at most 8192 rows, which is cheap and bounded.

Rules:

- `LoadHeader.schema` fixes types for the stream. A `RowBatch` has exactly one `ColumnData`
  per schema column, in schema order; each holds `row_count` values and its vector type
  matches the column type. Any violation is `INVALID_ARGUMENT` and the whole load is rejected.
- `row_count` is explicit so that a table with zero columns or an empty vector is unambiguous.
- No NULLs in v1: every cell has a value. Adding NULL support later is additive (an optional
  validity bitmap field next to each vector) and does not change existing vectors.
- Strings are UTF-8; invalid UTF-8 is rejected by protobuf parsing in every language.
- `double` carries IEEE-754 NaN and infinities as is; aggregations define their behavior, not the wire.
- Message size: the gRPC default limit is 4 MiB. Batches target 8192 rows or about 1 MiB,
  whichever is smaller. Servers need not raise the limit.
- Streaming: `IngestService.Load` is client-streaming: header first (dataset and schema), then
  batches, response `rows_loaded` at the end. Rows are appended atomically (all or nothing).
  Query results are one unary `Table`; M1 datasets are small, so no server streaming.

## Aggregation result shape

`QueryRequest` carries one `Aggregation { function, column }` and an `optional string group_by`.
`optional` is used so that "unset" is distinct from the empty string. Result `Table` column
layout: group column first (if any), then the aggregate named `<function>(<column>)` in lower
case (`count(*)` with no column). Types: COUNT is INT64, AVG is DOUBLE, SUM, MIN, MAX keep the
input type (so SUM over INT64 is INT64). Rows are ordered ascending by group key, which makes
output deterministic for the golden test. Overflow behavior of integer SUM is the engine's
choice and is not part of the wire contract in v1.

## Errors, status codes, deadlines

The gRPC status code and message are the contract. Messages are human-readable, one line,
and must not be parsed.

| Where | Condition | Status | `ErrorReason` (optional detail) |
|---|---|---|---|
| Engine, Gateway | Unknown column in aggregation or `group_by` | `INVALID_ARGUMENT` | `UNKNOWN_COLUMN` |
| Engine | Aggregation function UNSPECIFIED, SUM or AVG on STRING, missing column for non-COUNT | `INVALID_ARGUMENT` | `INVALID_AGGREGATION` |
| Engine (Load) | Header or batch does not match the schema, or the schema differs from the stored one | `INVALID_ARGUMENT` | `SCHEMA_MISMATCH` |
| Engine (Load) | Stream does not start with exactly one header, empty message, empty dataset name, UNSPECIFIED column type | `INVALID_ARGUMENT` | `MALFORMED_REQUEST` |
| Gateway (also Catalog) | Unknown dataset | `NOT_FOUND` | `UNKNOWN_DATASET` |
| Gateway | Engine unreachable | `UNAVAILABLE` | `ENGINE_UNREACHABLE` |

- The gateway forwards engine statuses unchanged, except that it maps its own failure to
  connect to `UNAVAILABLE`. It must not turn an engine `INVALID_ARGUMENT` into `UNKNOWN`/`INTERNAL`.
- `UNAVAILABLE` is retryable (idempotent reads, and `Load` is safe to retry only because a
  failed load changes nothing); the others are not.
- `ErrorDetail` (in `common.proto`) is optional. A server may pack it into the `details` of
  `google.rpc.Status`; clients must work without it. It is defined now so the field set can
  grow additively. The CLI maps any non-OK status to a one-line stderr message and exit code 1.
- Deadlines: servers set none. Clients must set one on every call: suggested 30 s for
  `QueryService.Execute`, 5 s for catalog calls, and a size-based deadline for `Load`.
  The gateway propagates the incoming deadline to the engine. `DEADLINE_EXCEEDED` and
  `CANCELLED` are produced by gRPC itself and are not part of the service contract.
- Servers should validate eagerly and fail before doing work; a failed call has no side effects.

## Catalog

`CatalogService` (`GetDataset`, `ListDatasets`) returns `DatasetInfo { name, schema }`, reusing
`Schema` from `common.proto`. The M1 gateway uses the catalog in-process through the Java
types; the service exists so the registry can be split out later without a contract change.

## Review checklist for a proto change

1. New field numbers only; none reused; deletions have `reserved` number and name.
2. New enum has `_UNSPECIFIED = 0` and a prefixed value naming.
3. Comment says what the field means, whether it is optional, and which status an invalid value gets.
4. Receivers handle the old sender (field absent) and the new sender (unknown field ignored).
5. Each consuming team (engine, server, ingest, reports) has seen the change.
