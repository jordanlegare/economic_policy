#!/usr/bin/env python3
"""Verify the committed BEA U.S. I-O artifact/certification contract offline.

The runtime only selects BEA when a generated BEA header and a certification
marker are both present. This verifier adds the missing content check: exact CSV
and generated-header hashes, BEA vintage/table IDs and the derived artifact
fingerprint must agree across provenance, the generated contract header and the
manually reviewed certification marker.

No BEA API key or network access is required. If no BEA artifact is committed,
the verifier succeeds in proxy mode. A partially committed BEA artifact fails.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

DEFAULT_CSV = Path("data/calibration/bea_us_io_matrix.csv")
DEFAULT_HEADER = Path("include/generated/trade_io_us_bea.hpp")
DEFAULT_PROVENANCE = Path("data/calibration/bea_us_io_provenance.json")
DEFAULT_CONTRACT = Path("include/generated/trade_io_us_bea_contract.hpp")
DEFAULT_MARKER = Path("include/generated/trade_io_us_bea_certified.hpp")


def sha256_path(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact_fingerprint(
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
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def cpp_string(text: str, name: str) -> str:
    match = re.search(
        rf"\b{name}\s*=\s*\"([^\"]+)\"\s*;",
        text,
    )
    if not match:
        raise RuntimeError(f"Missing C++ certification constant: {name}")
    return match.group(1)


def cpp_int(text: str, name: str) -> int:
    match = re.search(rf"\b{name}\s*=\s*([0-9]+)\s*;", text)
    if not match:
        raise RuntimeError(f"Missing C++ certification constant: {name}")
    return int(match.group(1))


def require_equal(label: str, actual: object, expected: object) -> None:
    if actual != expected:
        raise RuntimeError(f"{label} mismatch: {actual!r} != {expected!r}")


def verify_paths(
    csv_path: Path = DEFAULT_CSV,
    header_path: Path = DEFAULT_HEADER,
    provenance_path: Path = DEFAULT_PROVENANCE,
    contract_path: Path = DEFAULT_CONTRACT,
    marker_path: Path = DEFAULT_MARKER,
) -> str:
    generated_paths = [csv_path, header_path, provenance_path, contract_path]
    any_generated = any(path.exists() for path in generated_paths)

    if not any_generated:
        if marker_path.exists():
            raise RuntimeError("BEA certification marker exists without generated BEA artifacts")
        return "proxy"

    missing = [str(path) for path in generated_paths if not path.exists()]
    if missing:
        raise RuntimeError("Incomplete BEA artifact set: missing " + ", ".join(missing))

    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    header_text = header_path.read_text(encoding="utf-8")
    contract_text = contract_path.read_text(encoding="utf-8")

    csv_sha = sha256_path(csv_path)
    header_sha = sha256_path(header_path)
    contract_sha = sha256_path(contract_path)

    require_equal("BEA CSV SHA-256", csv_sha, provenance["csv_sha256"])
    require_equal("BEA header SHA-256", header_sha, provenance["header_sha256"])
    require_equal(
        "BEA contract-header SHA-256",
        contract_sha,
        provenance["contract_header_sha256"],
    )

    year = int(provenance["year"])
    direct_table_id = str(provenance["direct_requirements_table_id"])
    use_table_id = str(provenance["use_table_id"])
    expected_fingerprint = artifact_fingerprint(
        year,
        direct_table_id,
        use_table_id,
        csv_sha,
        header_sha,
    )
    require_equal(
        "BEA artifact fingerprint",
        provenance["artifact_fingerprint"],
        expected_fingerprint,
    )

    contract = provenance.get("certification_contract") or {}
    require_equal("provenance contract year", int(contract["year"]), year)
    require_equal(
        "provenance contract direct table",
        str(contract["direct_requirements_table_id"]),
        direct_table_id,
    )
    require_equal(
        "provenance contract Use table",
        str(contract["use_table_id"]),
        use_table_id,
    )
    require_equal("provenance contract CSV hash", contract["csv_sha256"], csv_sha)
    require_equal("provenance contract header hash", contract["header_sha256"], header_sha)
    require_equal(
        "provenance contract fingerprint",
        contract["artifact_fingerprint"],
        expected_fingerprint,
    )

    require_equal("generated BEA year", cpp_int(header_text, "kBeaUsIoYear"), year)
    require_equal(
        "generated direct table",
        cpp_string(header_text, "kBeaUsIoDirectRequirementsTableId"),
        direct_table_id,
    )
    require_equal(
        "generated Use table",
        cpp_string(header_text, "kBeaUsIoUseTableId"),
        use_table_id,
    )
    require_equal(
        "generated CSV hash",
        cpp_string(header_text, "kBeaUsIoCsvSha256"),
        csv_sha,
    )
    require_equal(
        "generated contract header hash",
        cpp_string(contract_text, "kBeaUsIoHeaderSha256"),
        header_sha,
    )
    require_equal(
        "generated contract fingerprint",
        cpp_string(contract_text, "kBeaUsIoArtifactFingerprint"),
        expected_fingerprint,
    )

    if not marker_path.exists():
        return "uncertified"

    marker_text = marker_path.read_text(encoding="utf-8")
    require_equal(
        "certified BEA year",
        cpp_int(marker_text, "kCertifiedBeaUsIoYear"),
        year,
    )
    require_equal(
        "certified direct table",
        cpp_string(marker_text, "kCertifiedBeaUsIoDirectRequirementsTableId"),
        direct_table_id,
    )
    require_equal(
        "certified Use table",
        cpp_string(marker_text, "kCertifiedBeaUsIoUseTableId"),
        use_table_id,
    )
    require_equal(
        "certified CSV hash",
        cpp_string(marker_text, "kCertifiedBeaUsIoCsvSha256"),
        csv_sha,
    )
    require_equal(
        "certified header hash",
        cpp_string(marker_text, "kCertifiedBeaUsIoHeaderSha256"),
        header_sha,
    )
    require_equal(
        "certified artifact fingerprint",
        cpp_string(marker_text, "kCertifiedBeaUsIoArtifactFingerprint"),
        expected_fingerprint,
    )
    return "certified"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--provenance", type=Path, default=DEFAULT_PROVENANCE)
    parser.add_argument("--contract-header", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--marker", type=Path, default=DEFAULT_MARKER)
    args = parser.parse_args()

    try:
        state = verify_paths(
            args.csv,
            args.header,
            args.provenance,
            args.contract_header,
            args.marker,
        )
    except (KeyError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"BEA certification verification failed: {exc}", file=sys.stderr)
        return 1

    if state == "proxy":
        print("BEA certification: no committed BEA artifact; EPA USEEIO proxy remains active")
    elif state == "uncertified":
        print("BEA certification: generated BEA artifact is internally consistent but not certified")
    else:
        print("BEA certification: exact artifact contract is certified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
