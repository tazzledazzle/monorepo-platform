"""Report CLI logic: argument parsing, request building, rendering, status mapping.

Kept free of process concerns so `main.py` stays a thin wrapper and tests can drive
`run()` with a fake gateway.
"""

import argparse
import sys
from typing import Callable, Optional, Sequence

import grpc
from tabulate import tabulate

from analytics.v1 import common_pb2, query_pb2, query_pb2_grpc

DEADLINE_SECONDS = 30.0

_FUNCTIONS = {
    "sum": query_pb2.AGGREGATE_FUNCTION_SUM,
    "avg": query_pb2.AGGREGATE_FUNCTION_AVG,
    "count": query_pb2.AGGREGATE_FUNCTION_COUNT,
    "min": query_pb2.AGGREGATE_FUNCTION_MIN,
    "max": query_pb2.AGGREGATE_FUNCTION_MAX,
}


def parse_agg(text: str) -> tuple[int, str]:
    """Parses `func:column` (or `count`, `count:*`) into (AggregateFunction, column).

    Raises ValueError with a one-line message on bad input.
    """
    func, sep, column = text.partition(":")
    func = func.strip().lower()
    if func not in _FUNCTIONS:
        raise ValueError(
            f"unknown aggregate function {func!r}; expected one of {', '.join(sorted(_FUNCTIONS))}"
        )
    column = column.strip()
    if column == "*":
        column = ""
    if func != "count" and not column:
        raise ValueError(f"aggregate {func!r} needs a column, e.g. {func}:revenue")
    if func == "count" and sep == "" :
        column = ""
    return _FUNCTIONS[func], column


def _agg_arg(text: str) -> tuple[int, str]:
    try:
        return parse_agg(text)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="cli", description="Run one aggregation through the gateway and print a table."
    )
    p.add_argument("--gateway", required=True, metavar="HOST:PORT", help="gateway address")
    p.add_argument("--dataset", required=True, help="dataset name")
    p.add_argument(
        "--agg", required=True, type=_agg_arg, metavar="FUNC[:COLUMN]",
        help="sum|avg|min|max need a column (sum:revenue); count takes none (count or count:*)",
    )
    p.add_argument("--group-by", default=None, metavar="COLUMN", help="optional grouping column")
    return p


def build_request(dataset: str, agg: tuple[int, str], group_by: Optional[str]) -> query_pb2.QueryRequest:
    req = query_pb2.QueryRequest(
        dataset=dataset,
        aggregation=query_pb2.Aggregation(function=agg[0], column=agg[1]),
    )
    if group_by:  # optional field: leave unset (not "") for no grouping
        req.group_by = group_by
    return req


def _column_values(col: common_pb2.ColumnData) -> list:
    kind = col.WhichOneof("values")
    return list(getattr(col, kind).values) if kind else []


def _cell(v) -> str:
    return repr(v) if isinstance(v, float) else str(v)


def render_table(table: query_pb2.Table) -> str:
    """Renders a result Table with tabulate's `simple` format, deterministic cell text.

    Numbers are right-aligned, strings left-aligned; floats print with their shortest
    round-trip repr; no tabulate number guessing.
    """
    headers = [c.name for c in table.schema.columns]
    columns = [_column_values(c) for c in table.columns]
    rows = [[_cell(columns[c][r]) for c in range(len(columns))] for r in range(table.row_count)]
    align = [
        "left" if c.type == common_pb2.COLUMN_TYPE_STRING else "right" for c in table.schema.columns
    ]
    return tabulate(rows, headers=headers, tablefmt="simple", disable_numparse=True, colalign=align)


def format_error(err: grpc.RpcError) -> str:
    """One-line stderr message for a non-OK gRPC status."""
    code = err.code().name if hasattr(err, "code") and err.code() else "UNKNOWN"
    details = (err.details() or "").strip() if hasattr(err, "details") else ""
    msg = " ".join(details.split())
    return f"error: {code}: {msg}" if msg else f"error: {code}"


def run(
    argv: Sequence[str],
    stdout=None,
    stderr=None,
    channel_factory: Callable[[str], grpc.Channel] = grpc.insecure_channel,
) -> int:
    """Runs the CLI; returns the process exit code (0 ok, 1 RPC/status error, 2 usage)."""
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as e:  # argparse printed usage to real stderr; exit code is 2
        return int(e.code or 0)
    request = build_request(args.dataset, args.agg, args.group_by)
    with channel_factory(args.gateway) as channel:
        stub = query_pb2_grpc.QueryServiceStub(channel)
        try:
            response = stub.Execute(request, timeout=DEADLINE_SECONDS)
        except grpc.RpcError as e:
            print(format_error(e), file=stderr)
            return 1
    print(render_table(response.result), file=stdout)
    return 0
