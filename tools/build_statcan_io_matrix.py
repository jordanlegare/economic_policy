#!/usr/bin/env python3
"""Aggregate StatCan 2024 detail input-output cells to the simulator's 20 sectors.

Source table: Statistics Canada 36-10-0001-01, Canada, basic prices, 2024.
Output orientation: matrix[downstream_model_sector][upstream_model_sector].
Coefficient: domestic inter-industry purchase Z_ij / downstream gross output X_j.

Only industry-to-industry cells enter the domestic production network. Imports,
taxes, value added and final demand remain outside this 20x20 matrix.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import urllib.request
import zipfile

TABLE_PID = "36100001"
TABLE_ID = "36-10-0001-01"
REFERENCE_YEAR = "2024"
API_URL = f"https://www150.statcan.gc.ca/t1/wds/rest/getFullTableDownloadCSV/{TABLE_PID}/en"
SOURCE_URL = "https://www150.statcan.gc.ca/t1/tbl1/en/tv.action?pid=3610000101"
DOI = "https://doi.org/10.25318/3610000101-eng"

SECTORS = [
    ("11", "Agriculture, forestry, fishing & hunting"),
    ("21", "Mining, quarrying, oil & gas"),
    ("22", "Utilities"),
    ("23", "Construction"),
    ("31-33", "Manufacturing"),
    ("42", "Wholesale trade"),
    ("44-45", "Retail trade"),
    ("48-49", "Transportation & warehousing"),
    ("51", "Information & cultural industries"),
    ("52", "Finance & insurance"),
    ("53", "Real estate, rental & leasing"),
    ("54", "Professional, scientific & technical services"),
    ("55", "Management of companies & enterprises"),
    ("56", "Administrative, support & waste services"),
    ("61", "Educational services"),
    ("62", "Health care & social assistance"),
    ("71", "Arts, entertainment & recreation"),
    ("72", "Accommodation & food services"),
    ("81", "Other services (except public administration)"),
    ("91", "Public administration"),
]
INDEX = {code: i for i, (code, _) in enumerate(SECTORS)}

# StatCan's symmetric IO industry codes distinguish business/private industries
# (BS supply / BU use) from government industries (GS supply / GU use). Both
# carry NAICS-like leading digits used for aggregation to the model sectors.
INDUSTRY_CODE = re.compile(
    r"\[(?:B|G)[SU][A-Z]*([0-9]{2,6}[A-Z0-9]*)\]", re.IGNORECASE
)


def normalize(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", (name or "").lower())


def model_sector_from_label(label: str) -> int | None:
    text = (label or "").lower()
    # Canadian NAICS uses sector 41 for wholesale trade; the simulator retains
    # the U.S.-style presentation code 42 for the bilateral delegation table.
    if "wholesale trade" in text:
        return INDEX["42"]

    match = INDUSTRY_CODE.search(label or "")
    if not match:
        return None
    code = match.group(1)
    prefix = code[:2]
    if prefix in {"31", "32", "33"}:
        return INDEX["31-33"]
    if prefix == "41":
        return INDEX["42"]
    if prefix in {"44", "45"}:
        return INDEX["44-45"]
    if prefix in {"48", "49"}:
        return INDEX["48-49"]
    return INDEX.get(prefix)


def find_column(fieldnames: list[str], *candidates: str) -> str | None:
    exact = {normalize(name): name for name in fieldnames}
    for candidate in candidates:
        key = normalize(candidate)
        if key in exact:
            return exact[key]
    for name in fieldnames:
        compact = normalize(name)
        if any(normalize(candidate) in compact for candidate in candidates):
            return name
    return None


def parse_number(text: str) -> float | None:
    value = (text or "").strip().replace(",", "")
    if value in {"", "..", "...", "x", "F", "E"}:
        return None
    try:
        number = float(value)
    except ValueError:
        return None
    return number if math.isfinite(number) else None


def choose_data_member(archive: zipfile.ZipFile) -> str:
    candidates = [
        info
        for info in archive.infolist()
        if info.filename.lower().endswith(".csv")
        and "metadata" not in info.filename.lower()
    ]
    if not candidates:
        raise RuntimeError("StatCan archive contains no data CSV")
    return max(candidates, key=lambda info: info.file_size).filename


def request_json(url: str) -> dict:
    request = urllib.request.Request(
        url, headers={"User-Agent": "economic-policy-research-calibration/1.0"}
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url: str, destination: Path) -> str:
    request = urllib.request.Request(
        url, headers={"User-Agent": "economic-policy-research-calibration/1.0"}
    )
    digest = hashlib.sha256()
    with urllib.request.urlopen(request, timeout=180) as response, destination.open("wb") as out:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
            out.write(block)
    return digest.hexdigest()


def build_from_csv(binary_stream, source_name: str) -> tuple[list[list[float]], dict]:
    text = io.TextIOWrapper(binary_stream, encoding="utf-8-sig", newline="")
    reader = csv.DictReader(text)
    if not reader.fieldnames:
        raise RuntimeError("StatCan CSV has no header")
    fields = list(reader.fieldnames)

    ref_col = find_column(fields, "REF_DATE", "Reference period")
    geo_col = find_column(fields, "GEO", "Geography")
    valuation_col = find_column(fields, "Valuation")
    supply_col = find_column(fields, "Supply")
    use_col = find_column(fields, "Use")
    value_col = find_column(fields, "VALUE", "Value")
    required = {"REF_DATE": ref_col, "Supply": supply_col, "Use": use_col, "VALUE": value_col}
    missing = [label for label, column in required.items() if not column]
    if missing:
        raise RuntimeError(f"Missing required StatCan columns {missing}; columns={fields}")

    transactions = [[0.0 for _ in SECTORS] for _ in SECTORS]
    gross_output = [0.0 for _ in SECTORS]
    mapped_transaction_rows = 0
    mapped_total_rows = 0
    detailed_supply_labels: set[str] = set()
    detailed_use_labels: set[str] = set()
    skipped_nonbasic = 0
    skipped_noncanada = 0
    skipped_other_year = 0

    for row in reader:
        if str(row.get(ref_col, "")).strip() != REFERENCE_YEAR:
            skipped_other_year += 1
            continue
        if geo_col and str(row.get(geo_col, "")).strip().lower() != "canada":
            skipped_noncanada += 1
            continue
        if valuation_col:
            valuation = str(row.get(valuation_col, "")).strip().lower()
            if valuation and "basic" not in valuation:
                skipped_nonbasic += 1
                continue

        value = parse_number(row.get(value_col, ""))
        if value is None:
            continue
        supply_label = str(row.get(supply_col, ""))
        use_label = str(row.get(use_col, ""))
        supply_sector = model_sector_from_label(supply_label)
        use_sector = model_sector_from_label(use_label)
        use_normalized = normalize(use_label)

        if supply_sector is not None:
            detailed_supply_labels.add(supply_label)
        if use_sector is not None:
            detailed_use_labels.add(use_label)

        # X_j: sum the detailed industry's total use/gross output into model j.
        if supply_sector is not None and use_normalized.startswith("totaluse"):
            gross_output[supply_sector] += value
            mapped_total_rows += 1
            continue

        # Z_ij: domestic output supplied by upstream industry i and used by
        # downstream industry j. Non-industry rows never enter this block.
        if supply_sector is not None and use_sector is not None:
            transactions[use_sector][supply_sector] += value
            mapped_transaction_rows += 1

    missing_output = [SECTORS[i][0] for i, value in enumerate(gross_output) if value <= 0.0]
    if missing_output:
        raise RuntimeError(f"No 2024 gross-output denominator for model sectors: {missing_output}")

    matrix = [[0.0 for _ in SECTORS] for _ in SECTORS]
    for downstream in range(len(SECTORS)):
        denominator = gross_output[downstream]
        for upstream in range(len(SECTORS)):
            matrix[downstream][upstream] = transactions[downstream][upstream] / denominator

    row_sums = [sum(row) for row in matrix]
    if min(row_sums) <= 0.0:
        raise RuntimeError(f"At least one model sector has zero domestic intermediate inputs: {row_sums}")
    if max(row_sums) >= 1.0:
        raise RuntimeError(f"Domestic intermediate-input share must remain below gross output: {row_sums}")
    if mapped_transaction_rows < 1000:
        raise RuntimeError(
            f"Only {mapped_transaction_rows} mapped 2024 inter-industry cells; expected detail-level table"
        )

    metadata = {
        "tableId": TABLE_ID,
        "productId": TABLE_PID,
        "referenceYear": int(REFERENCE_YEAR),
        "valuation": "Basic price",
        "geography": "Canada",
        "orientation": "matrix[downstream][upstream]",
        "coefficientDefinition": "domestic inter-industry purchase Z_ij divided by downstream gross output X_j",
        "sourceCsvMember": source_name,
        "mappedTransactionRows": mapped_transaction_rows,
        "mappedGrossOutputRows": mapped_total_rows,
        "mappedDetailedSupplyIndustryLabels": len(detailed_supply_labels),
        "mappedDetailedUseIndustryLabels": len(detailed_use_labels),
        "grossOutputThousandsCad": gross_output,
        "rowSums": row_sums,
        "maximumRowSum": max(row_sums),
        "minimumRowSum": min(row_sums),
        "skippedRows": {
            "otherYear": skipped_other_year,
            "nonCanada": skipped_noncanada,
            "nonBasicValuation": skipped_nonbasic,
        },
        "sectorConcordanceNotes": [
            "Canadian NAICS wholesale trade 41 is presented as model code 42 for bilateral Canada/U.S. UI compatibility.",
            "Government GS/GU 91-series IO industries aggregate to model public administration 91.",
            "Manufacturing 31, 32 and 33 aggregate to model 31-33; retail 44/45 and transportation 48/49 are similarly combined.",
        ],
    }
    return matrix, metadata


def render_matrix_csv(matrix: list[list[float]]) -> str:
    output = io.StringIO()
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(
        ["downstream_code", "downstream_name", *[f"upstream_{code}" for code, _ in SECTORS]]
    )
    for (code, name), row in zip(SECTORS, matrix):
        writer.writerow([code, name, *[f"{value:.12f}" for value in row]])
    return output.getvalue()


def render_header(matrix: list[list[float]], metadata: dict) -> str:
    rows = ["    {{" + ", ".join(f"{value:.12f}" for value in row) + "}}" for row in matrix]
    return "\n".join(
        [
            "#pragma once",
            "",
            "#include <array>",
            "",
            "namespace cad {",
            "namespace generated {",
            "",
            f'inline constexpr const char* kStatCanIoTableId = "{TABLE_ID}";',
            f"inline constexpr int kStatCanIoReferenceYear = {REFERENCE_YEAR};",
            f"inline constexpr double kStatCanIoMaximumDomesticIntermediateShare = {metadata['maximumRowSum']:.12f};",
            "inline constexpr std::array<std::array<double, 20>, 20> kStatCanIo2024Matrix{{",
            ",\n".join(rows),
            "}};",
            "",
            "}  // namespace generated",
            "}  // namespace cad",
            "",
        ]
    )


def write_outputs(output_dir: Path, matrix: list[list[float]], metadata: dict) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "statcan_io_2024_matrix.csv").write_text(
        render_matrix_csv(matrix), encoding="utf-8"
    )
    (output_dir / "trade_io_2024.hpp").write_text(
        render_header(matrix, metadata), encoding="utf-8"
    )
    (output_dir / "statcan_io_2024_provenance.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-zip", type=Path, help="Already downloaded StatCan full-table ZIP")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    temporary_path: Path | None = None
    zip_path = args.source_zip
    transport_metadata: dict[str, str] = {}
    try:
        if zip_path is None:
            payload = request_json(API_URL)
            if payload.get("status") != "SUCCESS" or not payload.get("object"):
                raise RuntimeError(f"StatCan WDS download lookup failed: {payload}")
            source_url = str(payload["object"])
            fd, raw_path = tempfile.mkstemp(prefix="statcan-36100001-", suffix=".zip")
            os.close(fd)
            temporary_path = Path(raw_path)
            zip_path = temporary_path
            digest = download(source_url, zip_path)
            transport_metadata = {
                "webDataService": API_URL,
                "downloadUrl": source_url,
                "downloadSha256": digest,
            }
        else:
            transport_metadata = {
                "downloadUrl": str(zip_path),
                "downloadSha256": hashlib.sha256(zip_path.read_bytes()).hexdigest(),
            }

        with zipfile.ZipFile(zip_path) as archive:
            member = choose_data_member(archive)
            with archive.open(member) as stream:
                matrix, metadata = build_from_csv(stream, member)

        metadata.update(transport_metadata)
        metadata["source"] = (
            "Statistics Canada, Input-output tables, detail level, Table 36-10-0001-01"
        )
        metadata["sourceUrl"] = SOURCE_URL
        metadata["doi"] = DOI
        write_outputs(args.output_dir, matrix, metadata)
        print(
            json.dumps(
                {
                    "status": "ok",
                    "mappedTransactionRows": metadata["mappedTransactionRows"],
                    "mappedGrossOutputRows": metadata["mappedGrossOutputRows"],
                    "maximumRowSum": metadata["maximumRowSum"],
                    "minimumRowSum": metadata["minimumRowSum"],
                    "outputDir": str(args.output_dir),
                },
                sort_keys=True,
            )
        )
        return 0
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
