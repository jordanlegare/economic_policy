#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


builder = load_module("build_bea_io_matrix", ROOT / "tools" / "build_bea_io_matrix.py")
verifier = load_module("verify_bea_io_certification", ROOT / "tools" / "verify_bea_io_certification.py")


def marker_text(year: int, direct_id: str, use_id: str, csv_sha: str, header_sha: str, fingerprint: str) -> str:
    return f'''#pragma once

#include <string_view>

namespace cad::generated {{
inline constexpr int kCertifiedBeaUsIoYear = {year};
inline constexpr std::string_view kCertifiedBeaUsIoDirectRequirementsTableId = "{direct_id}";
inline constexpr std::string_view kCertifiedBeaUsIoUseTableId = "{use_id}";
inline constexpr std::string_view kCertifiedBeaUsIoCsvSha256 = "{csv_sha}";
inline constexpr std::string_view kCertifiedBeaUsIoHeaderSha256 = "{header_sha}";
inline constexpr std::string_view kCertifiedBeaUsIoArtifactFingerprint = "{fingerprint}";
}}  // namespace cad::generated
'''


def build_fixture(root: Path):
    matrix = [[0.0 for _ in builder.MODEL_CODES] for _ in builder.MODEL_CODES]
    for i in range(len(matrix)):
        matrix[i][i] = 0.10 + i * 0.001

    year = 2024
    direct_id = "56"
    use_id = "57"
    csv_text = builder.render_csv(matrix)
    csv_sha = builder.sha256_text(csv_text)
    header_text = builder.render_header(matrix, year, direct_id, use_id, csv_sha)
    header_sha = builder.sha256_text(header_text)
    fingerprint = builder.bea_artifact_fingerprint(year, direct_id, use_id, csv_sha, header_sha)
    contract_text = builder.render_contract_header(header_sha, fingerprint)
    contract_sha = builder.sha256_text(contract_text)

    generated = root / "generated"
    generated.mkdir(parents=True)
    csv_path = root / "bea.csv"
    header_path = generated / "trade_io_us_bea.hpp"
    provenance_path = root / "provenance.json"
    contract_path = generated / "trade_io_us_bea_contract.hpp"
    marker_path = generated / "trade_io_us_bea_certified.hpp"

    csv_path.write_text(csv_text, encoding="utf-8")
    header_path.write_text(header_text, encoding="utf-8")
    contract_path.write_text(contract_text, encoding="utf-8")
    provenance = {
        "year": year,
        "direct_requirements_table_id": direct_id,
        "use_table_id": use_id,
        "csv_sha256": csv_sha,
        "header_sha256": header_sha,
        "contract_header_sha256": contract_sha,
        "artifact_fingerprint": fingerprint,
        "certification_contract": {
            "year": year,
            "direct_requirements_table_id": direct_id,
            "use_table_id": use_id,
            "csv_sha256": csv_sha,
            "header_sha256": header_sha,
            "artifact_fingerprint": fingerprint,
        },
    }
    provenance_path.write_text(json.dumps(provenance), encoding="utf-8")
    return csv_path, header_path, provenance_path, contract_path, marker_path, provenance


def compile_header(root: Path) -> subprocess.CompletedProcess[str]:
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    assert compiler, "C++ compiler required for certification contract test"
    source = root / "check.cpp"
    source.write_text(
        '#include "generated/trade_io_us_bea.hpp"\n'
        'int main() { return cad::generated::kBeaUsIoMatrix[0][0] >= 0.0 ? 0 : 1; }\n',
        encoding="utf-8",
    )
    return subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-fsyntax-only",
            "-I",
            str(root),
            "-I",
            str(ROOT / "include"),
            str(source),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        paths = build_fixture(root)
        csv_path, header_path, provenance_path, contract_path, marker_path, provenance = paths

        state = verifier.verify_paths(csv_path, header_path, provenance_path, contract_path, marker_path)
        assert state == "uncertified"

        marker_path.write_text(
            marker_text(
                provenance["year"],
                provenance["direct_requirements_table_id"],
                provenance["use_table_id"],
                provenance["csv_sha256"],
                provenance["header_sha256"],
                provenance["artifact_fingerprint"],
            ),
            encoding="utf-8",
        )
        assert verifier.verify_paths(csv_path, header_path, provenance_path, contract_path, marker_path) == "certified"
        good_compile = compile_header(root)
        assert good_compile.returncode == 0, good_compile.stderr

        bad_marker = marker_path.read_text(encoding="utf-8").replace(
            provenance["artifact_fingerprint"],
            "0" * 64,
        )
        marker_path.write_text(bad_marker, encoding="utf-8")
        try:
            verifier.verify_paths(csv_path, header_path, provenance_path, contract_path, marker_path)
            raise AssertionError("mismatched certification fingerprint was accepted")
        except RuntimeError:
            pass
        bad_compile = compile_header(root)
        assert bad_compile.returncode != 0, "mismatched certification marker compiled successfully"
        assert "Certified BEA artifact fingerprint mismatch" in bad_compile.stderr

        marker_path.unlink()
        header_path.write_text(header_path.read_text(encoding="utf-8") + "// tamper\n", encoding="utf-8")
        try:
            verifier.verify_paths(csv_path, header_path, provenance_path, contract_path, marker_path)
            raise AssertionError("tampered BEA header was accepted")
        except RuntimeError:
            pass

        fp2 = builder.bea_artifact_fingerprint(
            provenance["year"] + 1,
            provenance["direct_requirements_table_id"],
            provenance["use_table_id"],
            provenance["csv_sha256"],
            provenance["header_sha256"],
        )
        assert fp2 != provenance["artifact_fingerprint"]

    print("BEA I-O certification contract tests passed")


if __name__ == "__main__":
    main()
