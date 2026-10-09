import io
from concurrent import futures

import grpc
import pytest

from reports.cli import report_lib  # first: merges the analytics.v1 package dirs
from analytics.v1 import common_pb2, query_pb2, query_pb2_grpc  # noqa: E402


def test_parse_agg_ok():
    assert report_lib.parse_agg("sum:revenue") == (query_pb2.AGGREGATE_FUNCTION_SUM, "revenue")
    assert report_lib.parse_agg("COUNT") == (query_pb2.AGGREGATE_FUNCTION_COUNT, "")
    assert report_lib.parse_agg("count:*") == (query_pb2.AGGREGATE_FUNCTION_COUNT, "")
    assert report_lib.parse_agg("max:name") == (query_pb2.AGGREGATE_FUNCTION_MAX, "name")


@pytest.mark.parametrize("bad", ["median:x", "sum", "sum:", ""])
def test_parse_agg_bad(bad):
    with pytest.raises(ValueError):
        report_lib.parse_agg(bad)


def test_arg_errors_exit_2():
    for argv in ([], ["--gateway=x", "--dataset=d"], ["--gateway=x", "--dataset=d", "--agg=nope:c"]):
        out, err = io.StringIO(), io.StringIO()
        assert report_lib.run(argv, out, err) == 2


def test_parser_fields():
    a = report_lib.build_parser().parse_args(
        ["--gateway=h:1", "--dataset=sales", "--agg=sum:revenue", "--group-by=region"]
    )
    assert (a.gateway, a.dataset, a.group_by) == ("h:1", "sales", "region")


def test_build_request():
    r = report_lib.build_request("sales", (query_pb2.AGGREGATE_FUNCTION_SUM, "revenue"), "region")
    assert r.dataset == "sales" and r.group_by == "region"
    assert r.aggregation.column == "revenue"
    r = report_lib.build_request("sales", (query_pb2.AGGREGATE_FUNCTION_COUNT, ""), None)
    assert not r.HasField("group_by")


def _table():
    return query_pb2.Table(
        schema=common_pb2.Schema(columns=[
            common_pb2.Column(name="region", type=common_pb2.COLUMN_TYPE_STRING),
            common_pb2.Column(name="sum(revenue)", type=common_pb2.COLUMN_TYPE_DOUBLE),
            common_pb2.Column(name="count(*)", type=common_pb2.COLUMN_TYPE_INT64),
        ]),
        row_count=2,
        columns=[
            common_pb2.ColumnData(string_values=common_pb2.StringVector(values=["east", "west"])),
            common_pb2.ColumnData(double_values=common_pb2.DoubleVector(values=[170.5, 50.25])),
            common_pb2.ColumnData(int64_values=common_pb2.Int64Vector(values=[2, 12])),
        ],
    )


GOLDEN = (
    "region      sum(revenue)    count(*)\n"
    "--------  --------------  ----------\n"
    "east               170.5           2\n"
    "west               50.25          12"
)


def test_render_golden():
    assert report_lib.render_table(_table()) == GOLDEN


class _Err(grpc.RpcError):
    def code(self):
        return grpc.StatusCode.NOT_FOUND

    def details(self):
        return "unknown dataset\n 'x'"


def test_format_error_one_line():
    assert report_lib.format_error(_Err()) == "error: NOT_FOUND: unknown dataset 'x'"


class _FakeGateway(query_pb2_grpc.QueryServiceServicer):
    def __init__(self):
        self.requests = []

    def Execute(self, request, context):
        self.requests.append(request)
        if request.dataset == "missing":
            context.abort(grpc.StatusCode.NOT_FOUND, "unknown dataset 'missing'")
        return query_pb2.QueryResponse(result=_table())


@pytest.fixture
def gateway():
    fake = _FakeGateway()
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=2))
    query_pb2_grpc.add_QueryServiceServicer_to_server(fake, server)
    port = server.add_insecure_port("localhost:0")
    server.start()
    yield fake, f"localhost:{port}"
    server.stop(None)


def test_end_to_end_ok(gateway):
    fake, addr = gateway
    out, err = io.StringIO(), io.StringIO()
    code = report_lib.run(
        [f"--gateway={addr}", "--dataset=sales", "--agg=sum:revenue", "--group-by=region"], out, err
    )
    assert code == 0 and err.getvalue() == ""
    assert out.getvalue() == GOLDEN + "\n"
    assert fake.requests[0].group_by == "region"


def test_end_to_end_status_maps_to_exit_1(gateway):
    _, addr = gateway
    out, err = io.StringIO(), io.StringIO()
    code = report_lib.run([f"--gateway={addr}", "--dataset=missing", "--agg=count"], out, err)
    assert code == 1 and out.getvalue() == ""
    assert err.getvalue() == "error: NOT_FOUND: unknown dataset 'missing'\n"


def test_unreachable_gateway_is_one_line_exit_1():
    out, err = io.StringIO(), io.StringIO()
    code = report_lib.run(["--gateway=localhost:1", "--dataset=d", "--agg=count"], out, err)
    assert code == 1
    assert err.getvalue().startswith("error: UNAVAILABLE") and err.getvalue().count("\n") == 1
