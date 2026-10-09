"""Entry point: `cli --gateway=host:port --dataset=name --agg=sum:revenue [--group-by=col]`."""

import sys

from reports.cli import report_lib

if __name__ == "__main__":
    sys.exit(report_lib.run(sys.argv[1:]))
