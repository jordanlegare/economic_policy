#!/usr/bin/env python3
"""Build Canada<->U.S. intermediate-input exposure matrices from an official OECD ICIO archive.

This tool intentionally does not download ICIO data. OECD's official CDN may
require an interactive Cloudflare challenge, so production provenance must begin
with an archive obtained directly from OECD and supplied via --input. Third-party
mirrors are not an accepted source.

The output is *bilateral intermediate sourcing share*, not a domestic direct-
requirements matrix:

  B[downstream][upstream] = bilateral intermediate purchases from the partner's
      upstream model sector / all intermediate purchases by the downstream model sector.

A reviewed crosswalk is mandatory because OECD/ISIC industries do not map
one-for-one to the simulator's 20 NAICS presentation sectors. Fractional mappings
are supported and must sum to one for every ICIO industry used from CAN or USA.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import zipfile
from pathlib import Path

MODEL_CODES = [
    "11", "21", "22", "23", "31-33", "42", "44-45", "48-49", "51", "52",
    "53", "54", "55", "56", "61", "62", "71", "72", "81", "91",
]
MODEL_INDEX = {code: i for i, code in enumerate(MODEL_CODES)}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_entity(label: str) -> tuple[str, str] | None:
    text = label.strip().strip('"')
    match = re.match(r"^([A-Z]{3})[_:.\-](.+)$", text)
    if not match:
        return None
    country, industry = match.group(1), match.group(2)
    if country not in {"CAN", "USA"}:
        return country, industry
    return country, industry


def load_crosswalk(path: Path) -> dict[str, list[tuple[int, float]]]:
    mapping: dict[str, list[tuple[int, float]]] = {}
    with path.open(newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        required = {"icio_industry", "model_sector", "weight"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise SystemExit(f"Crosswalk requires columns {sorted(required)}")
        for row in reader:
            industry = row["icio_industry"].strip()
            sector = row["model_sector"].strip()
            if sector not in MODEL_INDEX:
                raise SystemExit(f"Unknown model sector {sector} for {industry}")
            weight = float(row["weight"])
            if weight <= 0:
                raise SystemExit(f"Crosswalk weight must be positive for {industry}")
            mapping.setdefault(industry, []).append((MODEL_INDEX[sector], weight))
    for industry, targets in mapping.items():
        total = sum(weight for _, weight in targets)
        if abs(total - 1.0) > 1e-9:
            raise SystemExit(f"Crosswalk weights for {industry} sum to {total}, not 1")
    if not mapping:
        raise SystemExit("Crosswalk is empty")
    return mapping


def select_matrix_csv(archive: Path, year: int) -> tuple[str, str]:
    with zipfile.ZipFile(archive) as zf:
        names = [name for name in zf.namelist() if name.lower().endswith(".csv")]
        if not names:
            raise SystemExit("Official OECD archive contains no CSV files")
        year_token = str(year)
        ranked = sorted(names, key=lambda name: (year_token not in name, len(name), name))
        for name in ranked:
            text = zf.read(name).decode("utf-8-sig", errors="replace")
            first = text.splitlines()[0] if text else ""
            if "CAN" in first and "USA" in first:
                return name, text
    raise SystemExit(f"Could not find a wide ICIO matrix CSV for {year}")


def build(text: str, mapping: dict[str, list[tuple[int, float]]]) -> tuple[list[list[float]], list[list[float]], dict]:
    rows = list(csv.reader(io.StringIO(text)))
    if len(rows) < 2:
        raise SystemExit("ICIO matrix CSV is empty")
    header = rows[0]
    columns: list[tuple[int, str, str]] = []
    for idx, label in enumerate(header[1:], start=1):
        entity = parse_entity(label)
        if entity:
            columns.append((idx, entity[0], entity[1]))
    if not columns:
        raise SystemExit("No country-industry columns found in ICIO matrix")

    # Denominator by downstream model sector and detailed downstream industry is
    # total intermediate purchases from all country-industry rows. Final-demand
    # and value-added rows do not match the country-industry label pattern and
    # are therefore excluded by construction.
    denominator = {"CAN": [0.0] * 20, "USA": [0.0] * 20}
    ca_from_us = [[0.0] * 20 for _ in range(20)]
    us_from_ca = [[0.0] * 20 for _ in range(20)]
    used_industries: set[str] = set()
    unmapped: set[str] = set()
    mapped_cells = 0

    parsed_rows: list[tuple[str, str, list[str]]] = []
    for raw in rows[1:]:
        if not raw:
            continue
        entity = parse_entity(raw[0])
        if not entity:
            continue
        parsed_rows.append((entity[0], entity[1], raw))

    for col_idx, down_country, down_industry in columns:
        if down_country not in {"CAN", "USA"}:
            continue
        down_targets = mapping.get(down_industry)
        if not down_targets:
            unmapped.add(down_industry)
            continue
        used_industries.add(down_industry)
        total_input = 0.0
        partner_flows: list[tuple[str, float]] = []
        partner = "USA" if down_country == "CAN" else "CAN"
        for up_country, up_industry, raw in parsed_rows:
            if col_idx >= len(raw):
                continue
            try:
                value = float(raw[col_idx].replace(",", "") or 0.0)
            except ValueError:
                continue
            if value < 0:
                continue
            total_input += value
            if up_country == partner and value > 0:
                partner_flows.append((up_industry, value))
        if total_input <= 0:
            continue
        for down_sector, down_weight in down_targets:
            denominator[down_country][down_sector] += total_input * down_weight
        target_matrix = ca_from_us if down_country == "CAN" else us_from_ca
        for up_industry, value in partner_flows:
            up_targets = mapping.get(up_industry)
            if not up_targets:
                unmapped.add(up_industry)
                continue
            used_industries.add(up_industry)
            for down_sector, down_weight in down_targets:
                for up_sector, up_weight in up_targets:
                    target_matrix[down_sector][up_sector] += value * down_weight * up_weight
                    mapped_cells += 1

    if unmapped:
        preview = ", ".join(sorted(unmapped)[:20])
        raise SystemExit(f"Reviewed crosswalk is incomplete; unmapped ICIO industries: {preview}")
    if mapped_cells == 0:
        raise SystemExit("No Canada/U.S. bilateral intermediate cells were mapped")
    for country in ("CAN", "USA"):
        if any(value <= 0 for value in denominator[country]):
            missing = [MODEL_CODES[i] for i, value in enumerate(denominator[country]) if value <= 0]
            raise SystemExit(
                f"No intermediate-input denominator for {country} model sectors: " + ", ".join(missing)
            )

    for country, matrix in (("CAN", ca_from_us), ("USA", us_from_ca)):
        for downstream in range(20):
            matrix[downstream] = [
                value / denominator[country][downstream] for value in matrix[downstream]
            ]
            if sum(matrix[downstream]) >= 1.0 + 1e-9:
                raise SystemExit(
                    f"Implausible bilateral sourcing share for {country} {MODEL_CODES[downstream]}"
                )
    return ca_from_us, us_from_ca, {
        "mapped_cells": mapped_cells,
        "mapped_icio_industries": len(used_industries),
    }


def render_csv(canada: list[list[float]], us: list[list[float]]) -> str:
    lines = ["importing_country,downstream_code," + ",".join(MODEL_CODES)]
    for country, matrix in (("CAN", canada), ("USA", us)):
        for code, row in zip(MODEL_CODES, matrix):
            lines.append(",".join([country, code] + [f"{value:.12f}" for value in row]))
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="ZIP downloaded directly from the official OECD ICIO page")
    parser.add_argument("--crosswalk", required=True, help="Reviewed ICIO-industry to 20-sector crosswalk CSV")
    parser.add_argument("--year", type=int, default=2022)
    parser.add_argument("--output", default="data/calibration/oecd_ca_us_bilateral_inputs.csv")
    parser.add_argument("--provenance", default="data/calibration/oecd_ca_us_bilateral_inputs_provenance.json")
    parser.add_argument("--source-url", default="https://www.oecd.org/en/data/insights/data-explainers/2024/05/inter-country-input-output-tables.html")
    args = parser.parse_args()

    archive = Path(args.input)
    if not archive.exists() or not zipfile.is_zipfile(archive):
        raise SystemExit("--input must be an official OECD ICIO ZIP archive")
    crosswalk = load_crosswalk(Path(args.crosswalk))
    member, text = select_matrix_csv(archive, args.year)
    canada, us, stats = build(text, crosswalk)
    output_text = render_csv(canada, us)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text(output_text, encoding="utf-8")
    provenance = {
        "agency": "OECD",
        "dataset": "Inter-Country Input-Output tables",
        "year": args.year,
        "source_url": args.source_url,
        "source_archive_sha256": sha256(archive),
        "source_archive_file": archive.name,
        "matrix_member": member,
        "crosswalk_sha256": sha256(Path(args.crosswalk)),
        "method": "Canada-US bilateral intermediate-input sourcing shares aggregated with reviewed fractional crosswalk",
        "orientation": "importing_downstream_by_partner_upstream",
        "denominator": "all intermediate purchases by downstream model sector",
        **stats,
        "activation_status": "artifact requires review before production activation",
    }
    Path(args.provenance).write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(provenance, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
