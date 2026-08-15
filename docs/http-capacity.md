# HTTP capacity and 100k-concurrency deployment

## Scope

The built-in C++ HTTP server is appropriate for local/desktop use and bounded backend work. It is **not** a six-figure direct-connection server. The production target in this document is 100,000+ simultaneously connected browser/client sockets at the ingress, with bounded and horizontally distributed API work behind that ingress.

This is intentionally different from claiming that 100,000 CPU-heavy `/api/evaluate` requests can execute at the same instant. Model evaluations are compute-bound and must be capacity-planned from measured service time, available CPU/GPU capacity, and the desired latency SLO.

## Audit findings

At the server boundary on `main`:

- the listen backlog is 64;
- automatic request workers are clamped to 2–8 and the explicit maximum is 16;
- the request queue is 64 entries;
- each accepted client is handed to a worker that performs blocking `recv` and `send` calls;
- socket receive/send timeouts are 15 seconds;
- responses force `Connection: close`, so every request creates a new backend TCP connection;
- static/browser responses are reconstructed from filesystem reads on the request path;
- the session store used one global map mutex and defaulted to 128 cached sessions.

That combination is good overload protection for a local server, but it means slow clients and connection bursts consume worker/queue capacity before model CPU becomes the bottleneck. Raising the queue or worker count alone would increase memory/thread pressure without solving the socket fan-in problem.

## Implemented production topology

```text
clients / external TLS load balancer
                 |
                 v
        Nginx event-driven ingress
        :8080, reuseport, large backlog
          |                 |
          | cached GET/HEAD | buffered API requests/responses
          v                 v
     static cache      consistent hash on X-CAD-Session-Id
                         |
             +-----------+-----------+
             |                       |
      cad-policy@18080 ...     cad-policy@18095
      8 HTTP workers each      8 HTTP workers each
      1 compute lane each      1 compute lane each
```

The supplied Nginx configuration is the connection terminator. It is sized with `worker_connections 262144`, `worker_rlimit_nofile 1048576`, `reuseport`, and a 65,535 listen backlog. Client request bodies are buffered before a backend is selected, and responses are buffered before being drained to slow clients. As a result, a slow browser socket normally occupies Nginx state rather than one of the C++ worker threads.

Non-API GET/HEAD responses are cached at Nginx. This is important because the application currently rebuilds the HTML shell and rereads browser assets on each request and deliberately sends `Cache-Control: no-store` from the built-in server. The ingress overrides that policy only for the non-API route and never caches `/api` responses.

API requests are consistently hashed by `X-CAD-Session-Id`. The state model already persists negotiation/room events, so affinity keeps live in-memory state on one process while allowing another process to reconstruct a session after a backend restart/failure. `proxy_next_upstream off` prevents a partially handled stateful request from silently hopping to another process.

The in-process `SessionStore` is now split into 64 independent shards instead of one global mutex. Its default remains 128 entries for local runs. Production can set `CAD_SESSION_CACHE_SIZE`; the supplied backend environment uses 8,192 entries per process. Across 16 consistently hashed processes, that is approximately 131,072 cache slots before idle eviction. Active sessions are not evicted while a worker holds them.

## Security boundary

The backend template starts each process with `--bind-all` so the existing application bearer-token contract remains active and the generated UI knows that API authentication is required. Set a long random `CAD_POLICY_STUDIO_TOKEN` in `/etc/cad-policy-studio/backend.env` (mode `0600`). Nginx forwards the `Authorization` header unchanged.

Because `--bind-all` listens on all interfaces, load the supplied nftables rule before exposing the host. It drops non-loopback traffic to ports 18080–18095, preventing clients from bypassing Nginx. If your platform already provides an equivalent host firewall/security-group rule, use that instead of creating a second policy layer.

Terminate TLS at an external load balancer/CDN or add your site-specific TLS configuration to Nginx. The checked-in Nginx file intentionally listens on port 8080 and does not embed certificate paths or secrets.

## Installation sketch

Build and install the Linux binary under `/opt/cad-policy/bin/cad-policy-studio`, with the repository/runtime working tree under `/opt/cad-policy`. Create the service account and runtime directory with permissions appropriate for your host.

Then install the supplied files:

```bash
sudo install -d -m 0755 /etc/cad-policy-studio
sudo install -m 0600 deploy/systemd/backend.env.example /etc/cad-policy-studio/backend.env
# Edit CAD_POLICY_STUDIO_TOKEN before starting anything.

sudo install -m 0644 deploy/systemd/cad-policy@.service /etc/systemd/system/cad-policy@.service
sudo install -m 0644 deploy/systemd/cad-policy-backends.target /etc/systemd/system/cad-policy-backends.target
sudo install -m 0644 deploy/sysctl/99-cad-policy-100k.conf /etc/sysctl.d/99-cad-policy-100k.conf
sudo sysctl --system

sudo nft -f deploy/nftables/cad-policy-backends.nft
sudo systemctl daemon-reload
sudo systemctl enable --now cad-policy-backends.target

sudo install -m 0644 deploy/nginx/nginx-100k.conf /etc/nginx/nginx.conf
sudo nginx -t
sudo systemctl reload nginx
```

The exact Nginx installation path may differ by distribution. Do not replace a shared system-wide `nginx.conf` blindly if the host serves other applications; include/adapt the `http`/`server` sections instead.

## Capacity math and tuning

The Nginx `worker_connections` number includes client sockets and upstream sockets. With 100k mostly idle/keep-alive clients, the upstream count is far smaller than the client count. During a burst of active API work, plan file descriptors for both sides. The checked-in `worker_rlimit_nofile` and `fs.file-max` values intentionally leave substantial headroom.

The supplied backend pool has 16 processes × 8 HTTP workers = 128 running HTTP jobs, plus up to 64 queued jobs per process. Each process is constrained to one internal compute lane (`--compute-workers=1`) so 16 processes do not each create a machine-sized compute pool and oversubscribe the CPU. On a host with a different physical core/GPU topology, tune **process count × compute workers** to the measured hardware capacity, not to the client count.

For multiple hosts, put the same ingress/backend unit behind an external L4/L7 load balancer and use a durable/shared session-event store or a routing key that keeps a session on one host. The checked-in single-host event logs are not a distributed database.

## Load qualification

A 100k claim should be earned with a staged test rather than inferred from configuration values. Use `tools/load/http_capacity.py` from multiple load-generator machines so the generators do not become the bottleneck.

Recommended gates:

1. **Connection-hold gate:** `/healthz`, ramp to 100k aggregate open keep-alive connections. Verify no connection-reset spike and stable Nginx RSS/file-descriptor usage.
2. **Cached browser gate:** `/`, ramp request rate while confirming a high `X-CAD-Cache: HIT` ratio and near-zero backend traffic after warm-up.
3. **Cheap API gate:** `/api/health` with a valid bearer token and distinct `X-CAD-Session-Id` values. Determine sustainable request throughput and p95/p99 latency without worker queues saturating.
4. **Stateful/API gate:** representative negotiation/evaluation traffic with realistic think time. Measure queue depth, 503 rate, CPU/GPU utilization, event-log I/O, p95/p99 latency, and per-session correctness.
5. **Failure gate:** restart one backend while traffic is active. Confirm Nginx removes it after passive failures, subsequent requests remap consistently, and durable session state reconstructs correctly.

Example aggregate connection test using four generators:

```bash
# Run on each of four load-generator hosts.
python3 tools/load/http_capacity.py \
  --host ingress.example.net --port 8080 \
  --connections 25000 --ramp-seconds 60 --duration 300 \
  --path /healthz --heartbeat 20
```

For API qualification:

```bash
export CAD_POLICY_STUDIO_TOKEN='...'
python3 tools/load/http_capacity.py \
  --host ingress.example.net --port 8080 \
  --connections 5000 --ramp-seconds 60 --duration 300 \
  --path /api/health --heartbeat 5 --session-prefix loadgen-a
```

The load script reports attempted/established/live connections, request successes/failures, and response latency. On POSIX it also warns when the generator's `RLIMIT_NOFILE` is too small for the requested connection count.

## What this does not guarantee

This patch removes the immediate connection-admission architecture mismatch and the global session-map lock, and it supplies a horizontally sharded production boundary. It does **not** prove a fixed 100k throughput number on arbitrary hardware. The model endpoints have heterogeneous CPU/GPU costs, and the durable session/event-log layer is still local filesystem storage. Use the staged qualification gates above to establish the actual SLO envelope for the intended production host(s).
