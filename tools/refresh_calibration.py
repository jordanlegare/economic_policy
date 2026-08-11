#!/usr/bin/env python3
"""Build an auditable Canada-U.S. calibration snapshot.

The builder intentionally separates:
  * observed / official-derived facts,
  * official legal tariff measures,
  * empirically estimated behavioural parameters, and
  * assumptions.

Network refreshes use only named government sources in source_registry.csv.
The script degrades to a partial snapshot when credentialed U.S. APIs or
reviewed tariff-line inputs are unavailable; --strict turns incompleteness into
an error for release workflows.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import io
import json
import os
from pathlib import Path
import re
import sys
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REGISTRY = ROOT / "data" / "calibration" / "source_registry.csv"
DEFAULT_OUTPUT = ROOT / "data" / "calibration" / "current.snapshot.csv"
SECTOR_CODES = [
    "11", "21", "22", "23", "31-33", "42", "44-45", "48-49", "51", "52",
    "53", "54", "55", "56", "61", "62", "71", "72", "81", "91",
]
SECTOR_NAMES = [
    "Agriculture forestry fishing & hunting", "Mining quarrying oil & gas", "Utilities",
    "Construction", "Manufacturing", "Wholesale trade", "Retail trade",
    "Transportation & warehousing", "Information & cultural industries", "Finance & insurance",
    "Real estate rental & leasing", "Professional scientific & technical services",
    "Management of companies & enterprises", "Administrative support & waste services",
    "Educational services", "Health care & social assistance", "Arts entertainment & recreation",
    "Accommodation & food services", "Other services except public administration",
    "Public administration",
]


def fetch(url: str, timeout: int = 30) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "CanadaPolicyStudio-calibration/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return response.read()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_registry(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fh:
        return {row["source_id"]: row for row in csv.DictReader(fh)}


def csv_from_statcan_zip(blob: bytes) -> list[dict[str, str]]:
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        csv_names = [name for name in archive.namelist() if name.lower().endswith(".csv")]
        if not csv_names:
            raise ValueError("Statistics Canada ZIP contained no CSV")
        # The main cube is normally the largest CSV in the archive.
        name = max(csv_names, key=lambda n: archive.getinfo(n).file_size)
        raw = archive.read(name)
    text = raw.decode("utf-8-sig", errors="replace")
    return list(csv.DictReader(io.StringIO(text)))


def first_field(row: dict[str, str], *needles: str) -> str:
    for key, value in row.items():
        normalized = key.lower().replace("_", " ")
        if all(needle.lower() in normalized for needle in needles):
            return value or ""
    return ""


def value_field(row: dict[str, str]) -> float | None:
    for key in ("VALUE", "Value", "value"):
        if key in row:
            try:
                return float((row[key] or "").replace(",", ""))
            except ValueError:
                return None
    return None


def statcan_total(rows: list[dict[str, str]], year: str, trade: str,
                  partner: str | None = None) -> float:
    candidates = []
    for row in rows:
        ref = first_field(row, "ref_date") or first_field(row, "ref date")
        if ref != year:
            continue
        trade_value = first_field(row, "trade")
        if trade_value.lower() != trade.lower():
            continue
        product = (first_field(row, "north american product classification")
                   or first_field(row, "product"))
        if "all sections" not in product.lower():
            continue
        if partner:
            partner_value = (first_field(row, "principal trading partner")
                             or first_field(row, "trading partner"))
            if partner.lower() not in partner_value.lower():
                continue
        value = value_field(row)
        if value is not None:
            candidates.append(value)
    if not candidates:
        raise ValueError(f"Unable to locate {year} {trade} all-sections total")
    # Statistics Canada cubes can repeat formatted/coordinate variants; values
    # should agree. Choosing max prevents a partial geography from winning.
    return max(candidates)


def fetch_trade_calibration(registry: dict[str, dict[str, str]], year: str) -> tuple[dict, list]:
    source_records = []
    annual = fetch(registry["statcan_trade_annual"]["as_of_url"])
    world = fetch("https://www150.statcan.gc.ca/n1/tbl/csv/12100173-eng.zip")
    annual_rows = csv_from_statcan_zip(annual)
    world_rows = csv_from_statcan_zip(world)

    exports_us_thousand = statcan_total(annual_rows, year, "Export", "United States")
    imports_us_thousand = statcan_total(annual_rows, year, "Import", "United States")
    exports_world_thousand = statcan_total(world_rows, year, "Export")
    imports_world_thousand = statcan_total(world_rows, year, "Import")

    params = {
        "canada_exports_to_us_cad": exports_us_thousand / 1_000_000.0,
        "canada_imports_from_us_cad": imports_us_thousand / 1_000_000.0,
        "exports_to_us_share": 100.0 * exports_us_thousand / exports_world_thousand,
        "imports_from_us_share": 100.0 * imports_us_thousand / imports_world_thousand,
    }
    source_records.extend([
        ("statcan_trade_annual", year, sha256(annual), "downloaded-and-parsed"),
        ("statcan_trade_world", year, sha256(world), "downloaded-and-parsed"),
    ])
    return params, source_records


def census_api_available() -> bool:
    return bool(os.environ.get("CENSUS_API_KEY"))


def bea_api_available() -> bool:
    return bool(os.environ.get("BEA_API_KEY"))


def load_line_calibration(path: Path | None) -> tuple[list[dict], dict]:
    """Aggregate a reviewed HS-line calibration file to the 20 model sectors.

    Expected columns:
      direction: ca_exports | us_exports
      hs: harmonized code
      sector_index: 0..19 (from reviewed HS->sector concordance)
      trade_value: value in a consistent currency/unit within direction
      applied_tariff: total applied ad-valorem tariff percent at snapshot date
      origin_eligible: 0/1
      origin_preference_used: 0/1

    The applied_tariff column must already merge the base schedule, CUSMA/USMCA
    qualification and legally effective Chapter-99/surtax overlays. Keeping this
    step explicit prevents legal text parsing from being mistaken for legal advice.
    """
    sectors = [{"ca_trade": 0.0, "us_trade": 0.0, "ca_duty": 0.0, "us_duty": 0.0,
                "origin_eligible": 0.0, "origin_used": 0.0} for _ in range(20)]
    if not path or not path.exists():
        return sectors, {"complete": False, "reason": "reviewed HS-line calibration not supplied"}
    with path.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    valid = 0
    for row in rows:
        try:
            idx = int(row["sector_index"])
            value = float(row["trade_value"])
            tariff = float(row["applied_tariff"])
        except (KeyError, ValueError):
            continue
        if idx < 0 or idx >= 20 or value < 0 or tariff < 0:
            continue
        direction = row.get("direction", "")
        if direction == "ca_exports":
            sectors[idx]["ca_trade"] += value
            sectors[idx]["ca_duty"] += value * tariff
        elif direction == "us_exports":
            sectors[idx]["us_trade"] += value
            sectors[idx]["us_duty"] += value * tariff
        else:
            continue
        eligible = float(row.get("origin_eligible") or 0)
        used = float(row.get("origin_preference_used") or 0)
        sectors[idx]["origin_eligible"] += value * max(0.0, min(1.0, eligible))
        sectors[idx]["origin_used"] += value * max(0.0, min(1.0, used))
        valid += 1
    ca_total = sum(s["ca_trade"] for s in sectors)
    us_total = sum(s["us_trade"] for s in sectors)
    complete = valid > 0 and ca_total > 0 and us_total > 0 and all(
        s["ca_trade"] + s["us_trade"] > 0 for s in sectors
    )
    for s in sectors:
        s["ca_export_share"] = 100 * s["ca_trade"] / ca_total if ca_total else 0
        s["us_export_share"] = 100 * s["us_trade"] / us_total if us_total else 0
        s["us_effective_tariff"] = s["ca_duty"] / s["ca_trade"] if s["ca_trade"] else -1
        s["canada_effective_tariff"] = s["us_duty"] / s["us_trade"] if s["us_trade"] else -1
        eligible = s["origin_eligible"]
        s["origin_utilization"] = 100 * s["origin_used"] / eligible if eligible else -1
    return sectors, {"complete": complete, "valid_rows": valid}


def load_behavioral_estimates(path: Path | None) -> dict[int, dict[str, float]]:
    if not path or not path.exists():
        return {}
    estimates = {}
    with path.open(newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            try:
                idx = int(row["sector_index"])
                estimates[idx] = {
                    "elasticity": float(row["trade_elasticity"]),
                    "elasticity_se": float(row["trade_elasticity_se"]),
                    "pass_through": float(row["price_pass_through"]),
                    "pass_through_se": float(row["price_pass_through_se"]),
                }
            except (KeyError, ValueError):
                continue
    return estimates


def write_snapshot(output: Path, registry: dict[str, dict[str, str]], as_of: str,
                   trade_params: dict, source_records: list, line_sectors: list[dict],
                   line_meta: dict, behavior: dict[int, dict[str, float]]) -> None:
    generated = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    source_state = {source_id: (vintage, digest, status)
                    for source_id, vintage, digest, status in source_records}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["META", "schema_version", "1"])
        w.writerow(["META", "snapshot_id", f"ca-us-{as_of}-refresh"])
        w.writerow(["META", "as_of", as_of])
        w.writerow(["META", "generated_at", generated])
        for key, unit in (("canada_exports_to_us_cad", "CAD_bn"),
                          ("canada_imports_from_us_cad", "CAD_bn"),
                          ("exports_to_us_share", "percent"),
                          ("imports_from_us_share", "percent")):
            w.writerow(["PARAM", key, f"{trade_params[key]:.9f}", unit, "observed" if "cad" in key else "official-derived",
                        "statcan_trade_annual", "2025", "0", "true"])
        io_ready = False
        w.writerow(["PARAM", "input_output_calibrated", int(io_ready), "binary", "official-derived",
                    "statcan_io", "2024", "0", "false"])
        # Behavioural parameters remain absent unless supplied from an empirical estimation run.
        w.writerow(["PARAM", "trade_elasticity", "0.65", "elasticity", "assumption",
                    "model_default", "unsourced-model-default", "0.15", "false"])

        source_ids = ["statcan_trade_annual", "statcan_trade_monthly", "statcan_io", "statcan_supply_use",
                      "us_census_exports", "us_census_imports", "usitc_hts", "cbsa_tariff",
                      "finance_countertariffs", "cusma_canada_schedule", "cusma_rules_origin",
                      "bea_input_output", "bea_industry", "us_section338_20260720", "us_metals_20260608"]
        for source_id in source_ids:
            source = registry.get(source_id)
            if not source:
                continue
            vintage, digest, status = source_state.get(source_id, ("", "", "registered-not-fetched"))
            if source_id.startswith("us_census") and not census_api_available():
                status = "missing-CENSUS_API_KEY"
            if source_id.startswith("bea_") and not bea_api_available():
                status = "missing-BEA_API_KEY"
            w.writerow(["SOURCE", source_id, source["agency"], source["dataset"], vintage,
                        source["as_of_url"], digest, status])

        # Measures are included with legal dates. Future measures remain visible but do not
        # alter applied rates until the reviewed line file says they are effective.
        w.writerow(["MEASURE", "ca_countertariffs_steel_aluminum_auto", "Canada",
                    "Canadian counter tariffs remaining on U.S. steel aluminum and automobiles",
                    "2025-03-13", "2025-03-13", "", "25%",
                    "specified U.S.-origin steel aluminum and motor-vehicle tariff items",
                    "finance_countertariffs", "in-force"])
        w.writerow(["MEASURE", "us_metals_20260608", "United States",
                    "Section 232 metals tariff adjustment", "2026-06-01", "2026-06-08", "2027-12-31",
                    "product/content-specific", "specified steel aluminum and copper products",
                    "us_metals_20260608", "in-force-requires-reviewed-line-merge"])
        w.writerow(["MEASURE", "us_section338_20260819", "United States",
                    "Section 338 Canada proclamations", "2026-07-20", "2026-08-19", "", "50% additional",
                    "certain Canadian products specified in proclamation annexes",
                    "us_section338_20260720", "future" if as_of < "2026-08-19" else "effective-requires-reviewed-line-merge"])

        for idx, (code, name) in enumerate(zip(SECTOR_CODES, SECTOR_NAMES)):
            s = line_sectors[idx]
            b = behavior.get(idx, {})
            tariff_complete = bool(line_meta.get("complete"))
            elasticity = b.get("elasticity", 0.0)
            elasticity_se = b.get("elasticity_se", 0.0)
            pass_through = b.get("pass_through", -1.0)
            pass_se = b.get("pass_through_se", 0.0)
            w.writerow(["SECTOR", idx, code, name,
                        f"{s.get('ca_export_share', 0):.8f}", f"{s.get('us_export_share', 0):.8f}",
                        f"{s.get('us_effective_tariff', -1):.8f}" if tariff_complete else "NA",
                        f"{s.get('canada_effective_tariff', -1):.8f}" if tariff_complete else "NA",
                        f"{elasticity:.8f}", f"{elasticity_se:.8f}",
                        f"{pass_through:.8f}" if pass_through >= 0 else "NA", f"{pass_se:.8f}",
                        f"{s.get('origin_utilization', -1):.8f}" if tariff_complete else "NA",
                        "official-derived" if tariff_complete else "missing",
                        "empirically-estimated" if idx in behavior else "missing",
                        "empirically-estimated" if idx in behavior and pass_through >= 0 else "missing"])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--as-of", default=dt.date.today().isoformat())
    parser.add_argument("--trade-year", default="2025")
    parser.add_argument("--hs-lines", type=Path, help="reviewed line-level bilateral trade/tariff merge")
    parser.add_argument("--behavioral-estimates", type=Path, help="sector elasticity/pass-through estimates")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    registry = read_registry(args.registry)
    try:
        trade_params, source_records = fetch_trade_calibration(registry, args.trade_year)
    except Exception as exc:
        print(f"calibration refresh failed fetching Statistics Canada trade data: {exc}", file=sys.stderr)
        return 2

    line_sectors, line_meta = load_line_calibration(args.hs_lines)
    behavior = load_behavioral_estimates(args.behavioral_estimates)
    write_snapshot(args.output, registry, args.as_of, trade_params, source_records,
                   line_sectors, line_meta, behavior)

    complete = bool(line_meta.get("complete")) and len(behavior) == 20
    print(json.dumps({
        "snapshot": str(args.output),
        "official_trade": True,
        "reviewed_tariff_lines": bool(line_meta.get("complete")),
        "behavioral_sectors": len(behavior),
        "census_api_key": census_api_available(),
        "bea_api_key": bea_api_available(),
        "release_ready": complete,
    }, indent=2))
    if args.strict and not complete:
        print("strict calibration requires reviewed tariff lines and estimates for all 20 sectors", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
