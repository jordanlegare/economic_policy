#!/usr/bin/env python3
"""Black-box fail-closed tests for the standalone HTTP boundary."""

from __future__ import annotations

import http.client
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

PORT = 18082
HOST = "127.0.0.1"


def request(method: str, path: str, body: str | None = None) -> tuple[int, str]:
    connection = http.client.HTTPConnection(HOST, PORT, timeout=5)
    headers = {"Content-Type": "application/json"} if body is not None else {}
    connection.request(method, path, body=body, headers=headers)
    response = connection.getresponse()
    payload = response.read().decode("utf-8")
    status = response.status
    connection.close()
    return status, payload


def expect_status(method: str, path: str, body: str | None, expected: int) -> str:
    status, payload = request(method, path, body)
    if status != expected:
        raise AssertionError(
            f"{method} {path}: expected HTTP {expected}, got {status}: {payload[:400]}"
        )
    return payload


def wait_ready(process: subprocess.Popen[bytes]) -> None:
    deadline = time.time() + 15
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited early with status {process.returncode}")
        try:
            status, _ = request("GET", "/api/room")
            if status == 200:
                return
        except OSError:
            pass
        time.sleep(0.1)
    raise RuntimeError("server did not become ready")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    executable = Path(os.environ.get("CAD_POLICY_STUDIO", root / "build-default" / "cad-policy-studio"))
    if not executable.exists():
        raise FileNotFoundError(f"missing test executable: {executable}")

    with tempfile.TemporaryDirectory(prefix="cad-http-contract-") as workdir:
        process = subprocess.Popen(
            [str(executable), str(PORT), "--no-browser"],
            cwd=workdir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            wait_ready(process)

            # Syntax, duplicate keys, non-finite numbers, type mismatches and
            # out-of-contract values all fail closed instead of becoming silent
            # defaults.
            expect_status("POST", "/api/evaluate", '{"usTariff":', 400)
            expect_status(
                "POST", "/api/evaluate", '{"usTariff":10,"usTariff":20}', 400
            )
            expect_status("POST", "/api/evaluate", '{"usTariff":1e9999}', 400)
            expect_status("POST", "/api/evaluate", '{"riskAversion":"high"}', 400)
            expect_status("POST", "/api/evaluate", '{"usSector0":101}', 400)
            expect_status("POST", "/api/v2/robustness", '{"parameterDraws":25}', 400)
            expect_status("POST", "/api/room", '{"action":', 400)

            registry = json.loads(expect_status("GET", "/api/v2/structural-registry", None, 200))
            assert registry["complete"] is True
            assert registry["invalidParameterCount"] == 0

            initial = json.loads(expect_status("GET", "/api/negotiation", None, 200))
            valid = json.loads(
                expect_status(
                    "POST",
                    "/api/negotiation",
                    json.dumps(
                        {
                            "actor": "canada",
                            "retaliatoryTariff": 10,
                            "canadaPriority": 55,
                            "riskAversion": 50,
                            "cooperationCeiling": 50,
                        }
                    ),
                    200,
                )
            )
            assert valid["revision"] == initial["revision"] + 1

            expect_status(
                "POST",
                "/api/negotiation",
                json.dumps({"actor": "canada", "retaliatoryTariff": 1000}),
                400,
            )
            after_invalid = json.loads(expect_status("GET", "/api/negotiation", None, 200))
            assert after_invalid["revision"] == valid["revision"], (
                "rejected negotiation writes must not mutate revision/state"
            )

            print("HTTP contract tests passed")
            return 0
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
            if process.returncode not in (None, 0, -15, 143):
                stdout, stderr = process.communicate()
                print(stdout.decode("utf-8", errors="replace"), file=sys.stderr)
                print(stderr.decode("utf-8", errors="replace"), file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
