#!/usr/bin/env python3
import csv
import math
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "estimate_trade_response.py"

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    source = tmp / "episodes.csv"
    output = tmp / "estimates.csv"
    with source.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=[
            "sector_index", "pre_trade", "post_trade", "tariff_change_pp",
            "control_trade_change_pct", "pre_price", "post_price",
            "control_price_change_pct", "weight"
        ])
        writer.writeheader()
        # Two internally coherent episodes. Tariff increases reduce trade and
        # raise price relative to controls, so both estimated responses are positive.
        writer.writerow({"sector_index": 4, "pre_trade": 100, "post_trade": 80,
                         "tariff_change_pp": 25, "control_trade_change_pct": 0,
                         "pre_price": 100, "post_price": 110,
                         "control_price_change_pct": 0, "weight": 1})
        writer.writerow({"sector_index": 4, "pre_trade": 120, "post_trade": 102,
                         "tariff_change_pp": 20, "control_trade_change_pct": 1,
                         "pre_price": 100, "post_price": 108,
                         "control_price_change_pct": 1, "weight": 2})

    run = subprocess.run([sys.executable, str(TOOL), str(source), "--output", str(output)],
                         check=True, capture_output=True, text=True)
    assert output.exists(), run.stdout + run.stderr
    with output.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    assert len(rows) == 1
    row = rows[0]
    assert int(row["sector_index"]) == 4
    assert float(row["trade_elasticity"]) > 0
    assert float(row["price_pass_through"]) > 0
    assert math.isfinite(float(row["trade_elasticity_se"]))
    assert math.isfinite(float(row["price_pass_through_se"]))
    assert int(row["episodes"]) == 2

    strict = subprocess.run([sys.executable, str(TOOL), str(source), "--output", str(output),
                             "--require-all-sectors"], capture_output=True, text=True)
    assert strict.returncode != 0
