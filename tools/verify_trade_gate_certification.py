#!/usr/bin/env python3
"""Verify the frozen observed-data trade calibration certificate.

Offline verification is deterministic and is what CI runs. --online additionally
checks the public source pages used to refresh the certificate; it is intentionally
not required for every build because upstream availability is outside the model's
control.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "data" / "calibration" / "trade_gate_certification.csv"
IO_MAPPING = ROOT / "data" / "calibration" / "io_2024_model_mapping.csv"
SNAPSHOT = ROOT / "data" / "calibration" / "current.snapshot.csv"
RELEVANT = {0, 1, 4}
EXPECTED_GATES = {
    "Applied tariff lines",
    "Input-output propagation",
    "Rules-of-origin utilization",
    "Trade elasticities",
    "Price pass-through",
}
ONLINE_SOURCES = {
    "boc_tariffs": (
        "https://www.bankofcanada.ca/publications/mpr/mpr-2026-07-15/tariff-assumptions/",
        ("5.0", "1.5", "Average tariff rates"),
    ),
    "statcan_io": (
        "https://www150.statcan.gc.ca/t1/tbl1/en/tv.action?pid=3610000101",
        ("36-10-0001-01", "2024"),
    ),
    "finance_cusma": (
        "https://www.canada.ca/en/department-finance/corporate/transparency/briefing-materials/2026/c15-eng.html",
        ("97.5", "CUSMA"),
    ),
    "trade_elasticities_sector": (
        "https://academic.oup.com/jeea/article/18/6/2869/5698020",
        ("Trade Elasticities", "12.51"),
    ),
    "trade_elasticity_scalar": (
        "https://www.elibrary.imf.org/view/journals/001/2012/297/article-A001-en.xml",
        ("Canada", "0.65"),
    ),
    "boc_pass_through": (
        "https://www.bankofcanada.ca/2026/06/staff-working-paper-2026-22/",
        ("6%", "one quarter"),
    ),
    "section338_future": (
        "https://www.whitehouse.gov/presidential-actions/2026/07/imposing-additional-duties-to-offset-canadian-discrimination-against-the-commerce-of-the-united-states-with-respect-to-motor-vehicles/",
        ("August 19, 2026", "50 percent"),
    ),
}


def fail(message: str) -> None:
    raise ValueError(message)


def read_manifest() -> None:
    with MANIFEST.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    by_gate = {row["gate"]: row for row in rows}
    if set(by_gate) != EXPECTED_GATES:
        fail(f"certificate gate set drifted: {sorted(by_gate)}")
    for gate in EXPECTED_GATES:
        row = by_gate[gate]
        if row.get("status") != "certified":
            fail(f"{gate} is not certified")
        if not row.get("source_id") or not row.get("method"):
            fail(f"{gate} lacks source/method provenance")


def read_io_mapping() -> None:
    with IO_MAPPING.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    indexes = {int(row["model_sector"]) for row in rows if row.get("status") == "mapped"}
    if indexes != set(range(20)):
        fail(f"IO mapping must cover all 20 model sectors; got {sorted(indexes)}")
    if any(row.get("source_table") != "36-10-0001-01" or row.get("reference_year") != "2024" for row in rows):
        fail("IO mapping source/vintage drifted")


def parameter_value(params: dict[str, list[str]], name: str) -> float:
    row = params.get(name)
    if not row:
        fail(f"snapshot missing parameter {name}")
    return float(row[2])


def read_snapshot() -> None:
    meta: dict[str, str] = {}
    params: dict[str, list[str]] = {}
    sectors: dict[int, list[str]] = {}
    sources: set[str] = set()
    measures: dict[str, list[str]] = {}
    with SNAPSHOT.open(newline="", encoding="utf-8") as fh:
        for row in csv.reader(line for line in fh if line.strip() and not line.startswith("#")):
            if not row:
                continue
            if row[0] == "META" and len(row) >= 3:
                meta[row[1]] = row[2]
            elif row[0] == "PARAM" and len(row) >= 9:
                params[row[1]] = row
            elif row[0] == "SECTOR" and len(row) >= 16:
                sectors[int(row[1])] = row
            elif row[0] == "SOURCE" and len(row) >= 8:
                sources.add(row[1])
            elif row[0] == "MEASURE" and len(row) >= 11:
                measures[row[1]] = row

    if meta.get("as_of") != "2026-08-12":
        fail(f"snapshot must be frozen as of 2026-08-12, got {meta.get('as_of')}")
    if set(sectors) != set(range(20)):
        fail("snapshot must contain all 20 model sectors")

    io = params.get("input_output_calibrated")
    io_coverage = params.get("input_output_model_sector_coverage")
    if not io or float(io[2]) < 0.5 or io[4] != "official-derived" or io[5] != "statcan_io":
        fail("2024 Statistics Canada IO mapping is not activated")
    if not io_coverage or float(io_coverage[2]) < 20 or io_coverage[4] != "official-derived" or io_coverage[5] != "statcan_io":
        fail("Statistics Canada IO mapping does not cover all 20 model sectors")

    us_tariff = params.get("us_effective_tariff_goods")
    ca_tariff = params.get("canada_effective_tariff_goods")
    if not us_tariff or abs(float(us_tariff[2]) - 5.0) > 1e-9 or us_tariff[4] != "official-derived":
        fail("U.S.-on-Canada goods tariff anchor drifted")
    if not ca_tariff or abs(float(ca_tariff[2]) - 1.5) > 1e-9 or ca_tariff[4] != "official-derived":
        fail("Canada-on-U.S. goods tariff anchor drifted")

    origin = params.get("cusma_origin_utilization_proxy")
    if not origin or abs(float(origin[2]) - 97.5) > 1e-9 or origin[4] != "official-derived":
        fail("CUSMA compliance/origin proxy drifted")

    trade_scalar = params.get("trade_elasticity")
    if not trade_scalar or abs(float(trade_scalar[2]) - 0.65) > 1e-9:
        fail("production trade elasticity must be 0.65")
    if trade_scalar[4] != "empirically-estimated" or trade_scalar[5] != "imf_trade_flows_mr_2012" or trade_scalar[8].lower() != "true":
        fail("production trade elasticity is not a direct empirical model input")

    pass_anchor = params.get("tariff_price_pass_through_anchor")
    if not pass_anchor or abs(float(pass_anchor[2]) - 0.24) > 1e-9 or pass_anchor[4] != "empirically-estimated":
        fail("tariff price pass-through anchor drifted")

    for idx in RELEVANT:
        row = sectors[idx]
        if float(row[6]) < 0 or float(row[7]) < 0 or row[13] != "official-derived":
            fail(f"sector {idx} tariff calibration missing")
        if float(row[8]) <= 0 or float(row[9]) < 0 or row[14] != "empirically-estimated":
            fail(f"sector {idx} trade elasticity missing")
        if float(row[10]) < 0 or float(row[10]) > 1 or float(row[11]) < 0 or row[15] != "empirical-research-anchor":
            fail(f"sector {idx} pass-through anchor missing")
        if abs(float(row[12]) - 97.5) > 1e-9:
            fail(f"sector {idx} CUSMA compliance proxy drifted")

    for idx, row in sectors.items():
        if idx not in RELEVANT and (abs(float(row[6])) > 1e-12 or abs(float(row[7])) > 1e-12):
            fail(f"non-merchandise sector {idx} must have zero applied tariff in the coarse model map")

    required_sources = {
        "boc_mpr_tariffs_2026_07", "statcan_io", "finance_cusma_compliance_2026",
        "statcan_cusma_compliance_q4_2025", "carrere_grujovic_robert_nicoud_2020",
        "imf_trade_flows_mr_2012", "boc_tariff_passthrough_2026", "usitc_hts",
        "cbsa_tariff", "finance_countertariffs", "us_section338_20260720",
    }
    missing = required_sources - sources
    if missing:
        fail(f"snapshot missing provenance sources: {sorted(missing)}")

    future = measures.get("us_section338_20260819")
    if not future or future[5] != "2026-08-19" or not future[10].startswith("future"):
        fail("Section 338 measure must remain future-dated before 2026-08-19")


def online_check() -> None:
    for name, (url, markers) in ONLINE_SOURCES.items():
        req = urllib.request.Request(url, headers={"User-Agent": "CanadaPolicyStudio-calibration/2.0"})
        with urllib.request.urlopen(req, timeout=40) as response:
            text = response.read().decode("utf-8", errors="ignore")
        lowered = text.lower()
        for marker in markers:
            if marker.lower() not in lowered:
                fail(f"online source {name} missing expected marker {marker!r}")
        print(f"online source pass: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify", action="store_true", help="run deterministic frozen-artifact verification")
    parser.add_argument("--online", action="store_true", help="also check public source pages")
    args = parser.parse_args()
    try:
        read_manifest()
        read_io_mapping()
        read_snapshot()
        if args.online:
            online_check()
    except Exception as exc:
        print(f"trade calibration verification failed: {exc}", file=sys.stderr)
        return 2
    print("trade calibration certificate verified: 5/5 gates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
