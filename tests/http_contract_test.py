#!/usr/bin/env python3
"""Black-box operational and fail-closed tests for the standalone HTTP server."""

from __future__ import annotations

import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
from typing import Mapping

PORT = 18082
AUTH_PORT = 18083
HOST = "127.0.0.1"
AUTH_TOKEN = "test-access-token-0123456789"


def request(
    method: str,
    path: str,
    body: str | None = None,
    *,
    port: int = PORT,
    headers: Mapping[str, str] | None = None,
    default_json_content_type: bool = True,
    timeout: float = 8,
) -> tuple[int, str]:
    connection = http.client.HTTPConnection(HOST, port, timeout=timeout)
    final_headers = dict(headers or {})
    if body is not None and default_json_content_type and "Content-Type" not in final_headers:
        final_headers["Content-Type"] = "application/json"
    connection.request(method, path, body=body, headers=final_headers)
    response = connection.getresponse()
    payload = response.read().decode("utf-8")
    status = response.status
    connection.close()
    return status, payload


def expect_status(
    method: str,
    path: str,
    body: str | None,
    expected: int,
    **kwargs,
) -> str:
    status, payload = request(method, path, body, **kwargs)
    if status != expected:
        raise AssertionError(
            f"{method} {path}: expected HTTP {expected}, got {status}: {payload[:400]}"
        )
    return payload


def wait_ready(
    process: subprocess.Popen[bytes],
    *,
    port: int = PORT,
    headers: Mapping[str, str] | None = None,
) -> None:
    deadline = time.time() + 15
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited early with status {process.returncode}")
        try:
            status, payload = request("GET", "/api/ready", port=port, headers=headers)
            if status == 200 and json.loads(payload).get("ready") is True:
                return
        except (OSError, json.JSONDecodeError):
            pass
        time.sleep(0.1)
    raise RuntimeError("server did not become ready")


def terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)
    if process.returncode not in (None, 0, -15, 143):
        stdout, stderr = process.communicate()
        print(stdout.decode("utf-8", errors="replace"), file=sys.stderr)
        print(stderr.decode("utf-8", errors="replace"), file=sys.stderr)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    executable = Path(os.environ.get("CAD_POLICY_STUDIO", root / "build-default" / "cad-policy-studio"))
    if not executable.exists():
        raise FileNotFoundError(f"missing test executable: {executable}")

    process = subprocess.Popen(
        [str(executable), str(PORT), "--no-browser", "--workers", "2"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_ready(process)
        health = json.loads(expect_status("GET", "/api/health", None, 200))
        assert health["workers"] == 2
        assert health["authRequired"] is False
        assert health["queueCapacity"] == 64
        assert health["queueDepth"] >= 0
        assert health["activeJobs"] >= 1
        assert health["failedJobs"] >= 0
        assert "baselineRefreshFailures" in health
        assert "baselineLastAttempt" in health
        assert "baselineLastSuccess" in health

        readiness = json.loads(expect_status("GET", "/api/ready", None, 200))
        assert readiness["ready"] is True
        assert readiness["reason"] == "ready"
        assert readiness["queueCapacity"] == 64
        assert readiness["queueDepth"] < readiness["queueCapacity"]
        assert "baselineLastError" in readiness

        # Occupy one real worker with an intentionally incomplete HTTP header.
        # A second request must still be serviced promptly by the other worker;
        # the old single-threaded accept/handle loop could not satisfy this.
        blocker = socket.create_connection((HOST, PORT), timeout=2)
        try:
            blocker.sendall(b"GET /api/health HTTP/1.1\r\nHost: localhost\r\n")
            time.sleep(0.05)
            started = time.monotonic()
            concurrent_health = json.loads(
                expect_status("GET", "/api/health", None, 200, timeout=2)
            )
            elapsed = time.monotonic() - started
            assert concurrent_health["workers"] == 2
            assert elapsed < 1.5, f"second client serialized behind blocked worker for {elapsed:.2f}s"
        finally:
            blocker.close()

        # The first baseline request must return the calibrated cache immediately;
        # it may not wait on three external curl calls.
        started = time.monotonic()
        baseline = json.loads(expect_status("GET", "/api/baseline", None, 200))
        elapsed = time.monotonic() - started
        assert elapsed < 2.0, f"baseline request blocked for {elapsed:.2f}s"
        assert "cache" in baseline
        assert "refreshInProgress" in baseline["cache"]

        # Syntax, duplicate keys, non-finite numbers, type mismatches and
        # out-of-contract values all fail closed instead of becoming silent defaults.
        expect_status("POST", "/api/evaluate", '{"usTariff":', 400)
        expect_status(
            "POST", "/api/evaluate", '{"usTariff":10,"usTariff":20}', 400
        )
        expect_status("POST", "/api/evaluate", '{"usTariff":1e9999}', 400)
        expect_status("POST", "/api/evaluate", '{"riskAversion":"high"}', 400)
        expect_status("POST", "/api/evaluate", '{"usSector0":101}', 400)
        expect_status("POST", "/api/v2/robustness", '{"parameterDraws":25}', 400)
        expect_status("POST", "/api/v2/robustness-batch", '{"parameterDraws":24}', 400)
        expect_status("POST", "/api/v2/robustness-batch", '{"parameterDraws":129}', 400)
        expect_status("POST", "/api/room", '{"action":', 400)
        expect_status(
            "POST",
            "/api/evaluate",
            "{}",
            415,
            default_json_content_type=False,
        )
        expect_status(
            "GET",
            "/api/room",
            None,
            400,
            headers={"X-CAD-Session-Id": "../escape"},
        )

        registry = json.loads(expect_status("GET", "/api/v2/structural-registry", None, 200))
        assert registry["complete"] is True
        assert registry["invalidParameterCount"] == 0

        # Browser/API state is isolated by explicit session id. Mutating session A
        # cannot change session B's negotiation revision or room identity.
        session_a = {"X-CAD-Session-Id": "contract-session-a"}
        session_b = {"X-CAD-Session-Id": "contract-session-b"}
        initial_a = json.loads(expect_status("GET", "/api/negotiation", None, 200, headers=session_a))
        initial_b = json.loads(expect_status("GET", "/api/negotiation", None, 200, headers=session_b))
        assert initial_a["revision"] == 0
        assert initial_b["revision"] == 0

        valid_a = json.loads(
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
                headers=session_a,
            )
        )
        assert valid_a["revision"] == 1
        after_b = json.loads(expect_status("GET", "/api/negotiation", None, 200, headers=session_b))
        assert after_b["revision"] == 0

        room_a = json.loads(expect_status("GET", "/api/room", None, 200, headers=session_a))
        room_b = json.loads(expect_status("GET", "/api/room", None, 200, headers=session_b))
        assert room_a["sessionId"] == "contract-session-a"
        assert room_b["sessionId"] == "contract-session-b"

        expect_status(
            "POST",
            "/api/negotiation",
            json.dumps({"actor": "canada", "retaliatoryTariff": 1000}),
            400,
            headers=session_a,
        )
        after_invalid = json.loads(expect_status("GET", "/api/negotiation", None, 200, headers=session_a))
        assert after_invalid["revision"] == valid_a["revision"], (
            "rejected negotiation writes must not mutate revision/state"
        )

        session_js = expect_status("GET", "/session.js", None, 200)
        assert "X-CAD-Session-Id" in session_js
        assert "Authorization" in session_js

        # Network binding is fail-closed: no token means startup rejection.
        rejected = subprocess.run(
            [str(executable), str(AUTH_PORT), "--no-browser", "--bind-all"],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
        assert rejected.returncode == 2
        assert b"requires an access token" in rejected.stderr

        auth_process = subprocess.Popen(
            [
                str(executable),
                str(AUTH_PORT),
                "--no-browser",
                "--bind-all",
                "--auth-token",
                AUTH_TOKEN,
                "--workers=2",
            ],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        auth_headers = {"Authorization": f"Bearer {AUTH_TOKEN}"}
        try:
            wait_ready(auth_process, port=AUTH_PORT, headers=auth_headers)
            expect_status("GET", "/api/health", None, 401, port=AUTH_PORT)
            expect_status("GET", "/api/ready", None, 401, port=AUTH_PORT)
            authenticated = json.loads(
                expect_status(
                    "GET", "/api/health", None, 200, port=AUTH_PORT, headers=auth_headers
                )
            )
            assert authenticated["authRequired"] is True
            authenticated_ready = json.loads(
                expect_status(
                    "GET", "/api/ready", None, 200, port=AUTH_PORT, headers=auth_headers
                )
            )
            assert authenticated_ready["ready"] is True
            # Static UI remains loadable so it can prompt for the bearer token;
            # API data and mutations remain protected.
            expect_status("GET", "/", None, 200, port=AUTH_PORT)
        finally:
            terminate(auth_process)

        # SIGTERM is a graceful lifecycle event: stop admission, drain the pool,
        # stop the baseline refresher, and exit successfully rather than dying -15.
        process.terminate()
        process.wait(timeout=10)
        assert process.returncode == 0, f"graceful shutdown returned {process.returncode}"
        stdout, stderr = process.communicate()
        assert b"Shutdown requested; draining" in stdout, stdout.decode(errors="replace")
        assert b"Shutdown complete" in stdout, stdout.decode(errors="replace")
        assert not stderr, stderr.decode(errors="replace")

        print("HTTP operational contract tests passed")
        return 0
    finally:
        terminate(process)


if __name__ == "__main__":
    raise SystemExit(main())
