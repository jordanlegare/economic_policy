#!/usr/bin/env python3
"""Build a 20-sector U.S. domestic direct-requirements matrix from BEA I-O data.

This is an external refresh/audit tool, not a runtime dependency. It deliberately
fails closed unless a BEA API key is supplied. The production engine must not
label the U.S. network empirical until the generated CSV/header/provenance files
have been reviewed and committed.

Method
------
1. Query BEA InputOutput metadata and discover a summary/underlying-summary
   *domestic direct requirements* table plus a compatible Use table.
2. Retrieve the requested annual vintage.
3. Map BEA commodity/industry codes into the simulator's 20 two-digit sectors.
4. Output-weight detailed downstream industries using gross output from the Use
   table and aggregate upstream direct-requirement coefficients:

       A[J][I] = sum_{j in J} X_j * sum_{i in I} a_ij / sum_{j in J} X_j

   where J is a model downstream sector, I is a model upstream sector, X_j is
   BEA gross output and a_ij is a domestic direct requirement.
5. Freeze the exact matrix to CSV and a generated C++ header with provenance.
6. Generate a separate contract header binding the exact CSV/header hashes,
   source year and selected BEA table IDs. A manually reviewed certification
   marker must match that contract before the runtime can activate BEA.

BEA publishes the InputOutput API but requires an API key. Do not substitute a
scraped HTML table or an unrelated historical matrix when the API is unavailable.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

API_URL = "https://apps.bea.gov/api/data/"
MODEL_CODES = [
    "11", "21", "22", "23", "31-33", "42", "44-45", "48-49", "51", "52",
    "53", "54", "55", "56", "61", "62", "71", "72", "81", "91",
]
MODEL_NAMES = [
    "Agriculture, forestry, fishing & hunting",
    "Mining, quarrying, oil & gas",
    "Utilities",
    "Construction",
    "Manufacturing",
    "Wholesale trade",
    "Retail trade",
    "Transportation & warehousing",
    "Information & cultural industries",
    "Finance & insurance",
    "Real estate, rental & leasing",
    "Professional, scientific & technical services",
    "Management of companies & enterprises",
    "Administrative, support & waste services",
    "Educational services",
    "Health care & social assistance",
    "Arts, entertainment & recreation",
    "Accommodation & food services",
    "Other services (except public administration)",
    "Public administration",
]


@dataclass(frozen=True)
class TableChoice:
    key: str
    description: str


def request_json(api_key: str, method: str, **params: str) -> dict:
    query = {
        "UserID": api_key,
        "Method": method,
        "DatasetName": "InputOutput",
        "ResultFormat": "JSON",
        **params,
    }
    url = API_URL + "?" + urllib.parse.urlencode(query)
    req = urllib.request.Request(url, headers={"User-Agent": "economic-policy-calibration/1.0"})
    with urllib.request.urlopen(req, timeout=120) as response:
        payload = response.read()
    parsed = json.loads(payload.decode("utf-8-sig"))
    results = parsed.get("BEAAPI", {}).get("Results", {})
    error = results.get("Error")
    if error:
        raise RuntimeError(f"BEA API error: {error}")
    return parsed


def result_rows(payload: dict) -> list[dict]:
    results = payload.get("BEAAPI", {}).get("Results", {})
    for key in ("Data", "ParamValue", "ParameterValues"):
        value = results.get(key)
        if isinstance(value, list):
            return value
        if isinstance(value, dict):
            for nested in value.values():
                if isinstance(nested, list):
                    return nested
    raise RuntimeError("BEA response did not contain a recognized row collection")


def metadata_tables(api_key: str) -> list[TableChoice]:
    payload = request_json(api_key, "GetParameterValues", ParameterName="TableID")
    out: list[TableChoice] = []
    for row in result_rows(payload):
        key = str(row.get("Key") or row.get("TableID") or row.get("key") or "").strip()
        desc = str(row.get("Desc") or row.get("Description") or row.get("desc") or "").strip()
        if key and desc:
            out.append(TableChoice(key, desc))
    if not out:
        raise RuntimeError("No InputOutput TableID metadata returned by BEA")
    return out


def choose_table(tables: Iterable[TableChoice], purpose: str) -> TableChoice:
    purpose = purpose.lower()
    ranked: list[tuple[int, TableChoice]] = []
    for table in tables:
        text = table.description.lower()
        if purpose == "direct":
            if "direct requirement" not in text or "domestic" not in text:
                continue
        elif purpose == "use":
            if "use of commodities by industries" not in text:
                continue
        else:
            raise ValueError(purpose)
        # Prefer the 71-industry summary level; underlying summary is a useful
        # second choice. Avoid 15-sector data because it cannot resolve all 20
        # simulator sectors.
        score = 0
        if "underlying summary" in text:
            score += 90
        elif "summary" in text:
            score += 100
        elif "sector" in text:
            score -= 100
        if "after redefinitions" in text:
            score += 5
        ranked.append((score, table))
    if not ranked:
        raise RuntimeError(f"Could not discover a BEA {purpose} table at adequate detail")
    ranked.sort(key=lambda item: (-item[0], int(item[1].key) if item[1].key.isdigit() else 10**9))
    choice = ranked[0][1]
    if ranked[0][0] < 0:
        raise RuntimeError(f"Only sector-level BEA {purpose} tables were available; refusing lossy 15-to-20 mapping")
    return choice


def numeric(value: object) -> float:
    text = str(value or "").replace(",", "").strip()
    if text in {"", "--", "NA", "N/A", "(NA)"}:
        return 0.0
    return float(text)


def model_sector(code: str, description: str) -> int | None:
    raw = re.sub(r"[^A-Za-z0-9]", "", code.upper())
    desc = description.lower()

    # Government/public-administration codes in BEA are not always NAICS-like.
    if raw.startswith("G") or "government" in desc or "public administration" in desc:
        return 19

    # Common BEA combined codes at sector/summary levels.
    aliases = {
        "31G": 4, "44RT": 6, "48TW": 7,
    }
    if raw in aliases:
        return aliases[raw]

    digits = "".join(ch for ch in raw if ch.isdigit())
    if len(digits) < 2:
        return None
    prefix = digits[:2]
    direct = {
        "11": 0, "21": 1, "22": 2, "23": 3,
        "42": 5, "51": 8, "52": 9, "53": 10, "54": 11, "55": 12,
        "56": 13, "61": 14, "62": 15, "71": 16, "72": 17, "81": 18,
        "91": 19, "92": 19,
    }
    if prefix in {"31", "32", "33"}:
        return 4
    if prefix in {"44", "45"}:
        return 6
    if prefix in {"48", "49"}:
        return 7
    return direct.get(prefix)


def fetch_table(api_key: str, table: TableChoice, year: int) -> list[dict]:
    payload = request_json(api_key, "GetData", TableID=table.key, Year=str(year))
    rows = result_rows(payload)
    if not rows:
        raise RuntimeError(f"BEA table {table.key} returned no data for {year}")
    return rows


def output_by_industry(use_rows: list[dict]) -> dict[str, tuple[float, int]]:
    output: dict[str, tuple[float, int]] = {}
    for row in use_rows:
        row_desc = str(row.get("RowDescr") or "")
        row_code = str(row.get("RowCode") or "")
        if "total industry output" not in row_desc.lower() and row_code.upper() not in {"T008", "T018", "T019"}:
            continue
        col_code = str(row.get("ColCode") or "").strip()
        col_desc = str(row.get("ColDescr") or "")
        sector = model_sector(col_code, col_desc)
        if sector is None:
            continue
        value = numeric(row.get("DataValue"))
        if value > 0:
            output[col_code] = (value, sector)
    if not output:
        raise RuntimeError("Could not identify Total Industry Output rows in the BEA Use table")
    return output


def aggregate_matrix(direct_rows: list[dict], output: dict[str, tuple[float, int]]) -> list[list[float]]:
    numerator = [[0.0 for _ in MODEL_CODES] for _ in MODEL_CODES]
    denominator = [0.0 for _ in MODEL_CODES]
    for _, (value, sector) in output.items():
        denominator[sector] += value

    mapped_cells = 0
    for row in direct_rows:
        col_code = str(row.get("ColCode") or "").strip()
        if col_code not in output:
            continue
        row_code = str(row.get("RowCode") or "").strip()
        row_desc = str(row.get("RowDescr") or "")
        upstream = model_sector(row_code, row_desc)
        if upstream is None:
            continue
        gross_output, downstream = output[col_code]
        coeff = numeric(row.get("DataValue"))
        if coeff < 0:
            continue
        numerator[downstream][upstream] += coeff * gross_output
        mapped_cells += 1

    if mapped_cells == 0:
        raise RuntimeError("No domestic direct-requirement cells mapped to the model sectors")
    missing = [MODEL_CODES[i] for i, value in enumerate(denominator) if value <= 0]
    if missing:
        raise RuntimeError("Missing BEA gross-output denominator for model sectors: " + ", ".join(missing))

    matrix = [[numerator[j][i] / denominator[j] for i in range(len(MODEL_CODES))]
              for j in range(len(MODEL_CODES))]
    for j, row in enumerate(matrix):
        total = sum(row)
        if not 0.0 <= total < 1.0:
            raise RuntimeError(f"Implausible direct-requirements row sum for {MODEL_CODES[j]}: {total:.6f}")
    return matrix


def render_csv(matrix: list[list[float]]) -> str:
    lines = ["downstream_code,downstream_name," + ",".join(MODEL_CODES)]
    for code, name, row in zip(MODEL_CODES, MODEL_NAMES, matrix):
        quoted = '"' + name.replace('"', '""') + '"'
        lines.append(",".join([code, quoted] + [f"{value:.12f}" for value in row]))
    return "\n".join(lines) + "\n"


def render_header(
    matrix: list[list[float]],
    year: int,
    direct_table_id: str,
    use_table_id: str,
    csv_sha256: str,
) -> str:
    rows = []
    for row in matrix:
        rows.append("    {{" + ", ".join(f"{value:.12f}" for value in row) + "}}")
    return (
        "#pragma once\n\n"
        "#include \"trade_network.hpp\"\n"
        "#include \"generated/trade_io_us_bea_contract.hpp\"\n\n"
        "#include <string_view>\n\n"
        "#if __has_include(\"generated/trade_io_us_bea_certified.hpp\")\n"
        "#include \"generated/trade_io_us_bea_certified.hpp\"\n"
        "#define CAD_BEA_US_IO_CERTIFICATION_MARKER_PRESENT 1\n"
        "#else\n"
        "#define CAD_BEA_US_IO_CERTIFICATION_MARKER_PRESENT 0\n"
        "#endif\n\n"
        "namespace cad::generated {\n\n"
        f"// Generated from U.S. BEA InputOutput domestic direct requirements, {year}.\n"
        "// Orientation: [downstream][upstream]. Do not edit by hand.\n"
        f"inline constexpr int kBeaUsIoYear = {year};\n"
        f"inline constexpr std::string_view kBeaUsIoDirectRequirementsTableId = \"{direct_table_id}\";\n"
        f"inline constexpr std::string_view kBeaUsIoUseTableId = \"{use_table_id}\";\n"
        f"inline constexpr std::string_view kBeaUsIoCsvSha256 = \"{csv_sha256}\";\n\n"
        "inline constexpr TradeInputOutputMatrix kBeaUsIoMatrix{{\n"
        + ",\n".join(rows)
        + "\n}};\n\n"
        "#if CAD_BEA_US_IO_CERTIFICATION_MARKER_PRESENT\n"
        "static_assert(kBeaUsIoYear == kCertifiedBeaUsIoYear, \"Certified BEA year mismatch\");\n"
        "static_assert(kBeaUsIoDirectRequirementsTableId == kCertifiedBeaUsIoDirectRequirementsTableId, \"Certified BEA direct-requirements table mismatch\");\n"
        "static_assert(kBeaUsIoUseTableId == kCertifiedBeaUsIoUseTableId, \"Certified BEA Use table mismatch\");\n"
        "static_assert(kBeaUsIoCsvSha256 == kCertifiedBeaUsIoCsvSha256, \"Certified BEA CSV SHA-256 mismatch\");\n"
        "static_assert(kBeaUsIoHeaderSha256 == kCertifiedBeaUsIoHeaderSha256, \"Certified BEA header SHA-256 mismatch\");\n"
        "static_assert(kBeaUsIoArtifactFingerprint == kCertifiedBeaUsIoArtifactFingerprint, \"Certified BEA artifact fingerprint mismatch\");\n"
        "#endif\n\n"
        "}  // namespace cad::generated\n"
    )


def render_contract_header(header_sha256: str, artifact_fingerprint: str) -> str:
    return (
        "#pragma once\n\n"
        "#include <string_view>\n\n"
        "namespace cad::generated {\n\n"
        "// Generated with trade_io_us_bea.hpp. Do not edit by hand.\n"
        f"inline constexpr std::string_view kBeaUsIoHeaderSha256 = \"{header_sha256}\";\n"
        f"inline constexpr std::string_view kBeaUsIoArtifactFingerprint = \"{artifact_fingerprint}\";\n\n"
        "}  // namespace cad::generated\n"
    )


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def bea_artifact_fingerprint(
    year: int,
    direct_table_id: str,
    use_table_id: str,
    csv_sha256: str,
    header_sha256: str,
) -> str:
    payload = {
        "csv_sha256": csv_sha256,
        "direct_requirements_table_id": str(direct_table_id),
        "header_sha256": header_sha256,
        "use_table_id": str(use_table_id),
        "year": int(year),
    }
    return sha256_text(json.dumps(payload, sort_keys=True, separators=(",", ":")))


def write_or_verify(path: Path, content: str, verify: bool) -> None:
    if verify:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise RuntimeError(f"Generated artifact drift: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--year", type=int, default=2024)
    parser.add_argument("--api-key", default=None, help="BEA API key; prefer BEA_API_KEY env")
    parser.add_argument("--csv", default="data/calibration/bea_us_io_matrix.csv")
    parser.add_argument("--header", default="include/generated/trade_io_us_bea.hpp")
    parser.add_argument("--contract-header", default="include/generated/trade_io_us_bea_contract.hpp")
    parser.add_argument("--provenance", default="data/calibration/bea_us_io_provenance.json")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    api_key = args.api_key or os.environ.get("BEA_API_KEY")
    if not api_key:
        print("BEA_API_KEY is required; refusing to invent a U.S. matrix", file=sys.stderr)
        return 2

    tables = metadata_tables(api_key)
    direct_table = choose_table(tables, "direct")
    use_table = choose_table(tables, "use")
    direct_rows = fetch_table(api_key, direct_table, args.year)
    use_rows = fetch_table(api_key, use_table, args.year)
    output = output_by_industry(use_rows)
    matrix = aggregate_matrix(direct_rows, output)

    csv_text = render_csv(matrix)
    csv_sha256 = sha256_text(csv_text)
    header_text = render_header(
        matrix,
        args.year,
        direct_table.key,
        use_table.key,
        csv_sha256,
    )
    header_sha256 = sha256_text(header_text)
    artifact_fingerprint = bea_artifact_fingerprint(
        args.year,
        direct_table.key,
        use_table.key,
        csv_sha256,
        header_sha256,
    )
    contract_text = render_contract_header(header_sha256, artifact_fingerprint)
    contract_sha256 = sha256_text(contract_text)

    provenance = {
        "agency": "U.S. Bureau of Economic Analysis",
        "dataset": "InputOutput",
        "year": args.year,
        "direct_requirements_table_id": direct_table.key,
        "direct_requirements_table": direct_table.description,
        "use_table_id": use_table.key,
        "use_table": use_table.description,
        "model_sector_count": len(MODEL_CODES),
        "method": "output-weighted aggregation of BEA domestic direct requirements to 20 model sectors",
        "orientation": "downstream_by_upstream",
        "csv_sha256": csv_sha256,
        "header_sha256": header_sha256,
        "contract_header_sha256": contract_sha256,
        "artifact_fingerprint": artifact_fingerprint,
        "certification_contract": {
            "year": args.year,
            "direct_requirements_table_id": direct_table.key,
            "use_table_id": use_table.key,
            "csv_sha256": csv_sha256,
            "header_sha256": header_sha256,
            "artifact_fingerprint": artifact_fingerprint,
        },
        "api": API_URL,
        "note": "API key is never written to provenance. Review mapping, row sums, source vintage, selected table IDs and exact artifact hashes before manually committing trade_io_us_bea_certified.hpp.",
    }
    provenance_text = json.dumps(provenance, indent=2, sort_keys=True) + "\n"

    write_or_verify(Path(args.csv), csv_text, args.verify)
    write_or_verify(Path(args.header), header_text, args.verify)
    write_or_verify(Path(args.contract_header), contract_text, args.verify)
    write_or_verify(Path(args.provenance), provenance_text, args.verify)
    print(json.dumps(provenance, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
