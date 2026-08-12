#!/usr/bin/env python3
"""Fail-closed governance checks for structural-parameter promotions.

A parameter may be labelled direct empirical/official in the production registry
only when the evidence ledger contains a direct mapping for the same parameter
and source. Reference-only estimates may inform sensitivity ranges or calibrated
anchors, but they cannot silently become production estimates.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

DIRECT_KINDS = {"empirical_estimate", "official_assessment", "realized_residual_estimate"}
NON_PROMOTING_KINDS = {"assumed", "calibrated", "mandate", "derived"}


def data_rows(path: Path):
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.reader(line for line in handle if line.strip() and not line.startswith("#")):
            yield row


def load_registry(path: Path) -> dict[str, dict[str, str]]:
    out = {}
    for row in data_rows(path):
        if row[0] != "PARAM":
            continue
        if len(row) < 13:
            raise SystemExit(f"Malformed structural registry row: {row}")
        out[row[1]] = {
            "baseline": row[2],
            "kind": row[4],
            "source_id": row[5],
            "vintage": row[6],
            "distribution": row[9],
            "sampled": row[11],
            "notes": row[12],
        }
    return out


def load_evidence(path: Path) -> dict[str, list[dict[str, str]]]:
    out: dict[str, list[dict[str, str]]] = {}
    for row in data_rows(path):
        if row[0] != "EVIDENCE":
            continue
        if len(row) < 9:
            raise SystemExit(f"Malformed structural evidence row: {row}")
        out.setdefault(row[1], []).append({
            "tier": row[2],
            "mapping": row[3],
            "anchor": row[4],
            "source_id": row[5],
            "sample_period": row[6],
            "method": row[7],
            "notes": row[8],
        })
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", default="data/calibration/structural_parameter_registry.csv")
    parser.add_argument("--evidence", default="data/calibration/empirical_structural_evidence.csv")
    args = parser.parse_args()

    registry = load_registry(Path(args.registry))
    evidence = load_evidence(Path(args.evidence))
    if not registry:
        raise SystemExit("Structural registry is empty")

    direct_count = 0
    for name, parameter in registry.items():
        kind = parameter["kind"]
        if kind not in DIRECT_KINDS | NON_PROMOTING_KINDS:
            raise SystemExit(f"Unknown structural provenance kind for {name}: {kind}")
        if not parameter["source_id"] or not parameter["vintage"]:
            raise SystemExit(f"Missing source/vintage for structural parameter {name}")

        records = evidence.get(name, [])
        if kind in DIRECT_KINDS:
            direct = [record for record in records if record["mapping"] == "direct"]
            if not direct:
                raise SystemExit(
                    f"Illegal direct promotion: {name} is {kind} in production but has no direct evidence mapping"
                )
            if not any(record["source_id"] == parameter["source_id"] for record in direct):
                raise SystemExit(
                    f"Source mismatch for direct production parameter {name}: registry={parameter['source_id']}"
                )
            for record in direct:
                if not record["sample_period"] or not record["method"]:
                    raise SystemExit(f"Incomplete direct evidence metadata for {name}")
            direct_count += 1
        else:
            # Reference-only evidence is allowed for assumed/calibrated entries,
            # but an entry cannot call itself empirical in prose while using a
            # non-promoting provenance kind.
            if kind == "calibrated" and "empirical_estimate" in parameter["notes"].lower():
                raise SystemExit(f"Ambiguous calibrated/empirical claim in notes for {name}")

    # Known falsification boundary: the measured residual correlation is useful
    # as a production dependence-shape anchor, but its evidence remains
    # reference-only until shock variances themselves are identified as realized
    # structural innovations.
    correlation = registry.get("output_inflation_shock_correlation")
    if correlation:
        if correlation["kind"] != "calibrated":
            raise SystemExit("output_inflation_shock_correlation must remain calibrated, not direct empirical")
        records = evidence.get("output_inflation_shock_correlation", [])
        if not records or any(record["mapping"] == "direct" for record in records):
            raise SystemExit("output/inflation residual correlation must remain reference-only evidence")

    print(
        f"Structural promotion gate passed: {len(registry)} active registry parameters; "
        f"{direct_count} direct empirical/official production mappings; reference-only evidence not promoted."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
