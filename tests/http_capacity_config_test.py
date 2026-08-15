#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import py_compile
import re

ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, label: str) -> None:
    assert needle in haystack, f"missing {label}: {needle}"


def main() -> None:
    nginx = text("deploy/nginx/nginx-100k.conf")
    require(nginx, "worker_rlimit_nofile 1048576;", "Nginx descriptor ceiling")
    require(nginx, "worker_connections 262144;", "Nginx event-loop capacity")
    require(nginx, "listen 8080 reuseport backlog=65535;", "large ingress backlog")
    require(nginx, "hash $cad_affinity consistent;", "session affinity")
    require(nginx, "proxy_request_buffering on;", "slow-request isolation")
    require(nginx, "proxy_buffering on;", "slow-response isolation")
    require(nginx, "proxy_cache cad_static;", "static response cache")
    require(nginx, "proxy_cache_lock on;", "cache stampede protection")
    require(nginx, "proxy_next_upstream off;", "stateful retry safety")
    require(nginx, "proxy_set_header Authorization $http_authorization;", "bearer forwarding")
    require(nginx, "proxy_set_header X-CAD-Session-Id $http_x_cad_session_id;", "session routing header")

    backend_ports = re.findall(r"server 127\.0\.0\.1:(18\d{3})", nginx)
    assert backend_ports == [str(port) for port in range(18080, 18096)], backend_ports

    service = text("deploy/systemd/cad-policy@.service")
    require(service, "--bind-all", "backend bearer-auth mode")
    require(service, "--workers=8", "bounded HTTP workers")
    require(service, "--compute-workers=1", "bounded compute lanes")
    require(service, "LimitNOFILE=131072", "backend descriptor ceiling")
    require(service, "EnvironmentFile=/etc/cad-policy-studio/backend.env", "backend environment")

    target = text("deploy/systemd/cad-policy-backends.target")
    for port in range(18080, 18096):
        require(target, f"cad-policy@{port}.service", f"backend instance {port}")

    env_example = text("deploy/systemd/backend.env.example")
    require(env_example, "CAD_POLICY_STUDIO_TOKEN=", "backend bearer token")
    require(env_example, "CAD_SESSION_CACHE_SIZE=8192", "production session cache")

    sysctl = text("deploy/sysctl/99-cad-policy-100k.conf")
    require(sysctl, "fs.file-max = 2097152", "host file table")
    require(sysctl, "net.core.somaxconn = 65535", "kernel accept backlog")
    require(sysctl, "net.ipv4.tcp_max_syn_backlog = 262144", "SYN backlog")

    nft = text("deploy/nftables/cad-policy-backends.nft")
    require(nft, 'iifname != "lo" tcp dport 18080-18095 drop', "backend bypass firewall")

    session_store = text("include/server_session.hpp")
    require(session_store, "CAD_SESSION_CACHE_SIZE", "configurable session cache")
    require(session_store, "static constexpr std::size_t shard_count = 64;", "session-store sharding")
    require(session_store, "std::array<Shard, shard_count>", "session shard storage")

    load_tool = ROOT / "tools/load/http_capacity.py"
    py_compile.compile(str(load_tool), doraise=True)

    docs = text("docs/http-capacity.md")
    require(docs, "100,000+ simultaneously connected", "explicit concurrency scope")
    require(docs, "does **not** prove", "capacity qualification caveat")

    print("HTTP 100k-capacity deployment contracts passed")


if __name__ == "__main__":
    main()
