#!/usr/bin/env python3
"""Unit tests for the USEEIO v2.5 U.S. proxy aggregation contract."""

from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_useeio_us_proxy", ROOT / "tools" / "build_useeio_us_proxy.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def test_sector_mapping() -> None:
    assert MODULE.model_sector("1111A0") == 0
    assert MODULE.model_sector("211000") == 1
    assert MODULE.model_sector("336111") == 4
    assert MODULE.model_sector("4B0000") == 6
    assert MODULE.model_sector("482000") == 7
    assert MODULE.model_sector("GSLGE") == 19
    assert MODULE.model_sector("S00500") == 19
    assert MODULE.model_sector("S00401") is None
    assert MODULE.model_sector("S00300") is None
    assert MODULE.model_sector("S00402") is None
    assert MODULE.model_sector("S00900") is None


def test_output_weighting_orientation_and_clipping() -> None:
    # One synthetic commodity for each model sector plus a second manufacturing
    # commodity. A_d is [upstream][downstream].
    detail = [
        "110000", "210000", "220000", "230000", "310000", "420000",
        "440000", "480000", "510000", "520000", "530000", "540000",
        "550000", "560000", "610000", "620000", "710000", "720000",
        "810000", "920000", "320000",
    ]
    n = len(detail)
    a_d = [[0.0] * n for _ in range(n)]
    q = [1.0] * n

    # Agriculture input into construction.
    a_d[0][3] = 0.20
    # Manufacturing has two detailed downstream commodities. With q weights
    # 1 and 3, the aggregated self requirement must be 0.50.
    a_d[4][4] = 0.20
    a_d[20][20] = 0.60
    q[4] = 1.0
    q[20] = 3.0
    # Negative adjustment-like input is not propagated.
    a_d[0][4] = -0.10

    # Give every other downstream commodity a small positive own requirement so
    # each aggregated row has a valid positive intermediate share.
    for i in range(n):
        if a_d[i][i] == 0.0:
            a_d[i][i] = 0.01

    matrix, counts, mapped_cells = MODULE.aggregate_matrix(detail, a_d, q)
    assert len(matrix) == 20
    assert counts[4] == 2
    assert abs(matrix[3][0] - 0.20) < 1e-12
    assert abs(matrix[4][4] - 0.50) < 1e-12
    assert abs(matrix[4][0]) < 1e-12
    assert mapped_cells > 0
    assert all(0.0 < sum(row) < 1.0 for row in matrix)


if __name__ == "__main__":
    test_sector_mapping()
    test_output_weighting_orientation_and_clipping()
    print("USEEIO v2.5 U.S. proxy builder tests passed")
