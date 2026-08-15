#!/usr/bin/env python3
"""Hold many HTTP/1.1 keep-alive connections for ingress capacity qualification.

This is intentionally a connection-capacity tool, not a replacement for a full
application workload generator. Run it from multiple hosts for six-figure tests
so generator memory, CPU, source-port space and file descriptors do not become
the bottleneck.
"""

from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass
import os
import random
import resource
import statistics
import sys
import time
from typing import Optional


@dataclass
class Stats:
    attempted: int = 0
    established: int = 0
    live: int = 0
    requests_ok: int = 0
    requests_failed: int = 0
    connect_failed: int = 0
    bytes_received: int = 0


class LatencyWindow:
    def __init__(self, limit: int = 20_000) -> None:
        self._limit = limit
        self._values: list[float] = []

    def add(self, seconds: float) -> None:
        if len(self._values) >= self._limit:
            del self._values[: max(1, self._limit // 10)]
        self._values.append(seconds)

    def snapshot_ms(self) -> tuple[float, float, float]:
        if not self._values:
            return 0.0, 0.0, 0.0
        values = sorted(self._values)
        mean = statistics.fmean(values) * 1000.0
        p95 = values[min(len(values) - 1, int(len(values) * 0.95))] * 1000.0
        p99 = values[min(len(values) - 1, int(len(values) * 0.99))] * 1000.0
        return mean, p95, p99


async def read_response(reader: asyncio.StreamReader, timeout: float) -> tuple[int, int]:
    header = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=timeout)
    lines = header.decode("iso-8859-1", errors="replace").split("\r\n")
    if not lines or len(lines[0].split()) < 2:
        raise RuntimeError("invalid HTTP response status line")
    try:
        status = int(lines[0].split()[1])
    except ValueError as exc:
        raise RuntimeError("invalid HTTP response status") from exc

    content_length: Optional[int] = None
    chunked = False
    for line in lines[1:]:
        if not line or ":" not in line:
            continue
        name, value = line.split(":", 1)
        name = name.strip().lower()
        value = value.strip()
        if name == "content-length":
            content_length = int(value)
        elif name == "transfer-encoding" and "chunked" in value.lower():
            chunked = True

    body_bytes = 0
    if content_length is not None:
        if content_length:
            body = await asyncio.wait_for(reader.readexactly(content_length), timeout=timeout)
            body_bytes = len(body)
    elif chunked:
        while True:
            size_line = await asyncio.wait_for(reader.readline(), timeout=timeout)
            if not size_line:
                raise RuntimeError("unexpected EOF in chunked response")
            size_token = size_line.split(b";", 1)[0].strip()
            size = int(size_token, 16)
            if size == 0:
                while True:
                    trailer = await asyncio.wait_for(reader.readline(), timeout=timeout)
                    if trailer in (b"\r\n", b"\n", b""):
                        break
                break
            body = await asyncio.wait_for(reader.readexactly(size), timeout=timeout)
            body_bytes += len(body)
            await asyncio.wait_for(reader.readexactly(2), timeout=timeout)
    else:
        # Keep-alive qualification requires a framed response. Reading until EOF
        # would make a successful persistent connection look hung.
        raise RuntimeError("response has neither Content-Length nor chunked framing")

    return status, len(header) + body_bytes


def request_bytes(args: argparse.Namespace, connection_id: int) -> bytes:
    host_header = args.host if args.port in (80, 443) else f"{args.host}:{args.port}"
    headers = [
        f"GET {args.path} HTTP/1.1",
        f"Host: {host_header}",
        "Connection: keep-alive",
        "Accept: application/json,text/plain,*/*",
        "User-Agent: cad-policy-capacity/1",
    ]
    token = args.token or os.environ.get("CAD_POLICY_STUDIO_TOKEN", "")
    if token:
        headers.append(f"Authorization: Bearer {token}")
    if args.session_prefix:
        headers.append(f"X-CAD-Session-Id: {args.session_prefix}-{connection_id}")
    return ("\r\n".join(headers) + "\r\n\r\n").encode("ascii")


async def one_connection(
    connection_id: int,
    args: argparse.Namespace,
    stats: Stats,
    latencies: LatencyWindow,
    stop_at: float,
) -> None:
    stats.attempted += 1
    writer: Optional[asyncio.StreamWriter] = None
    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(args.host, args.port, ssl=args.tls),
            timeout=args.connect_timeout,
        )
        stats.established += 1
        stats.live += 1
        payload = request_bytes(args, connection_id)

        while time.monotonic() < stop_at:
            started = time.perf_counter()
            writer.write(payload)
            await asyncio.wait_for(writer.drain(), timeout=args.io_timeout)
            status, received = await read_response(reader, args.io_timeout)
            elapsed = time.perf_counter() - started
            stats.bytes_received += received
            latencies.add(elapsed)
            if args.accept_status_min <= status <= args.accept_status_max:
                stats.requests_ok += 1
            else:
                stats.requests_failed += 1
                if args.fail_on_http_error:
                    break

            remaining = stop_at - time.monotonic()
            if remaining <= 0:
                break
            jitter = random.uniform(-0.10, 0.10) * args.heartbeat
            await asyncio.sleep(min(remaining, max(0.05, args.heartbeat + jitter)))
    except (asyncio.TimeoutError, ConnectionError, OSError, RuntimeError, ValueError):
        if writer is None:
            stats.connect_failed += 1
        else:
            stats.requests_failed += 1
    finally:
        if writer is not None:
            stats.live -= 1
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, OSError):
                pass


async def reporter(stats: Stats, latencies: LatencyWindow, stop_at: float) -> None:
    while time.monotonic() < stop_at:
        await asyncio.sleep(5.0)
        mean, p95, p99 = latencies.snapshot_ms()
        print(
            "capacity: "
            f"attempted={stats.attempted} established={stats.established} live={stats.live} "
            f"ok={stats.requests_ok} request_fail={stats.requests_failed} "
            f"connect_fail={stats.connect_failed} mean_ms={mean:.1f} p95_ms={p95:.1f} "
            f"p99_ms={p99:.1f} recv_mib={stats.bytes_received / (1024 * 1024):.1f}",
            flush=True,
        )


async def run(args: argparse.Namespace) -> int:
    stats = Stats()
    latencies = LatencyWindow()
    started = time.monotonic()
    stop_at = started + args.duration
    report_task = asyncio.create_task(reporter(stats, latencies, stop_at))
    tasks: list[asyncio.Task[None]] = []

    if args.connections <= 0:
        raise ValueError("--connections must be positive")
    if args.ramp_seconds < 0 or args.duration <= 0:
        raise ValueError("--ramp-seconds must be non-negative and --duration positive")

    launch_interval = args.ramp_seconds / args.connections if args.ramp_seconds else 0.0
    next_launch = started
    for connection_id in range(args.connections):
        if time.monotonic() >= stop_at:
            break
        now = time.monotonic()
        if launch_interval and now < next_launch:
            await asyncio.sleep(next_launch - now)
        tasks.append(asyncio.create_task(
            one_connection(connection_id, args, stats, latencies, stop_at)
        ))
        next_launch += launch_interval

    await asyncio.gather(*tasks)
    report_task.cancel()
    try:
        await report_task
    except asyncio.CancelledError:
        pass

    mean, p95, p99 = latencies.snapshot_ms()
    success_rate = stats.established / stats.attempted if stats.attempted else 0.0
    print(
        "final: "
        f"attempted={stats.attempted} established={stats.established} "
        f"connect_success={success_rate:.5f} ok={stats.requests_ok} "
        f"request_fail={stats.requests_failed} connect_fail={stats.connect_failed} "
        f"mean_ms={mean:.1f} p95_ms={p95:.1f} p99_ms={p99:.1f}",
        flush=True,
    )
    return 0 if success_rate >= args.min_connect_success and stats.requests_failed == 0 else 1


def warn_file_limit(connections: int) -> None:
    try:
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    except (AttributeError, ValueError):
        return
    needed = connections + 256
    if soft < needed:
        print(
            f"warning: RLIMIT_NOFILE soft={soft} is below approximately {needed} "
            f"descriptors needed; hard={hard}",
            file=sys.stderr,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--tls", action="store_true", help="use TLS for the client connection")
    parser.add_argument("--connections", type=int, default=1000)
    parser.add_argument("--ramp-seconds", type=float, default=30.0)
    parser.add_argument("--duration", type=float, default=120.0)
    parser.add_argument("--heartbeat", type=float, default=20.0)
    parser.add_argument("--path", default="/healthz")
    parser.add_argument("--session-prefix", default="")
    parser.add_argument("--token", default="", help="bearer token; defaults to CAD_POLICY_STUDIO_TOKEN")
    parser.add_argument("--connect-timeout", type=float, default=5.0)
    parser.add_argument("--io-timeout", type=float, default=15.0)
    parser.add_argument("--accept-status-min", type=int, default=200)
    parser.add_argument("--accept-status-max", type=int, default=399)
    parser.add_argument("--fail-on-http-error", action="store_true")
    parser.add_argument("--min-connect-success", type=float, default=0.99)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    warn_file_limit(args.connections)
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
