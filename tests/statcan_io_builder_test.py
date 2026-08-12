#!/usr/bin/env python3
from __future__ import annotations

import csv
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
BUILDER = ROOT / "tools" / "build_statcan_io_matrix.py"
CSV_PATH = ROOT / "data" / "calibration" / "statcan_io_2024_matrix.csv"
PROVENANCE_PATH = ROOT / "data" / "calibration" / "statcan_io_2024_provenance.json"
HEADER_PATH = ROOT / "include" / "generated" / "trade_io_2024.hpp"

spec = importlib.util.spec_from_file_location("statcan_io_builder", BUILDER)
assert spec and spec.loader
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

# Concordance rules must cover private/business, government and the explicit
# Canada-vs-U.S. wholesale-code presentation difference.
assert builder.model_sector_from_label("Crop production [BS111100]") == 0
assert builder.model_sector_from_label("Crop production [BU111100]") == 0
assert builder.model_sector_from_label("Manufacturing [BS311000]") == 4
assert builder.model_sector_from_label("Wholesale trade [BS411100]") == 5
assert builder.model_sector_from_label("Defence services [GS911100]") == 19
assert builder.model_sector_from_label("Defence services [GU911100]") == 19
assert builder.model_sector_from_label("Other federal government services [GS911A00]") == 19
assert builder.model_sector_from_label("Household final consumption") is None

with CSV_PATH.open(newline="", encoding="utf-8") as handle:
    rows = list(csv.DictReader(handle))
assert len(rows) == 20
assert [row["downstream_code"] for row in rows] == [code for code, _ in builder.SECTORS]

matrix = []
for row in rows:
    values = [float(row[f"upstream_{code}"]) for code, _ in builder.SECTORS]
    assert len(values) == 20
    assert all(value >= 0.0 for value in values)
    matrix.append(values)

provenance = json.loads(PROVENANCE_PATH.read_text(encoding="utf-8"))
assert provenance["tableId"] == "36-10-0001-01"
assert provenance["productId"] == "36100001"
assert provenance["referenceYear"] == 2024
assert provenance["geography"] == "Canada"
assert provenance["valuation"] == "Basic price"
assert provenance["orientation"] == "matrix[downstream][upstream]"
assert provenance["mappedTransactionRows"] == 40364
assert provenance["mappedGrossOutputRows"] == 213
assert provenance["mappedDetailedSupplyIndustryLabels"] == 213
assert provenance["mappedDetailedUseIndustryLabels"] == 216
assert provenance["downloadSha256"] == "3c7422bce4c6ff2d5082d6ac1afb4f31610039b71ada9973126673f705f0fc22"
assert provenance["retrievalTransport"] == "statcan-direct"
assert provenance["rawMirror"] is None

row_sums = [sum(row) for row in matrix]
assert min(row_sums) > 0.17
assert max(row_sums) < 0.73
for actual, frozen in zip(row_sums, provenance["rowSums"]):
    assert abs(actual - float(frozen)) < 1e-10
assert abs(max(row_sums) - provenance["maximumRowSum"]) < 1e-10
assert abs(min(row_sums) - provenance["minimumRowSum"]) < 1e-10

# The generated C++ matrix and the audit CSV must be identical at the frozen
# 12-decimal production precision. This catches hand edits to either artifact.
header = HEADER_PATH.read_text(encoding="utf-8")
marker = "kStatCanIo2024Matrix{{"
start = header.index(marker) + len(marker)
end = header.index("}};", start)
header_values = [float(value) for value in re.findall(r"\d+\.\d{12}", header[start:end])]
csv_values = [value for row in matrix for value in row]
assert len(header_values) == 400
assert len(csv_values) == 400
for generated, audited in zip(header_values, csv_values):
    assert abs(generated - audited) < 5e-13

print("StatCan 2024 IO aggregation artifact tests passed")
