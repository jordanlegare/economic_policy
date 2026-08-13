# Server operations and trust boundary

Canada Policy Studio is designed to run as a local research application by default. The embedded HTTP server is not intended to be exposed to an untrusted network without the explicit protections described here.

## Default loopback operation

```bash
./build/cad-policy-studio 8080
```

The default listener binds only to `127.0.0.1`. API authentication is not required in this mode because the process is not reachable through non-loopback interfaces.

The server uses a bounded worker pool. The automatic worker count is derived from available hardware and constrained to 2–8 workers. It can be overridden explicitly:

```bash
./build/cad-policy-studio 8080 --workers 4
```

The accepted override range is 1–16 workers. The request queue is bounded; a saturated server returns HTTP 503 rather than allowing unbounded queued work.

`GET /api/health` reports the worker count, active in-memory session count, live-baseline refresh state, and whether API authentication is required.

## Network binding is authenticated

`--bind-all` changes the listener to all IPv4 interfaces and therefore changes the security boundary. The server refuses to start in this mode unless an access token of at least 16 characters is supplied.

Use either an environment variable:

```bash
export CAD_POLICY_STUDIO_TOKEN='replace-with-a-long-random-secret'
./build/cad-policy-studio 8080 --bind-all
```

or the explicit command-line option:

```bash
./build/cad-policy-studio 8080 --bind-all --auth-token 'replace-with-a-long-random-secret'
```

The environment-variable form is preferable on shared systems because command-line arguments may be visible to other local users or process-inspection tooling.

When network-bound, every `/api/*` request requires:

```text
Authorization: Bearer <token>
```

Static dashboard assets remain readable so the browser can load the application and prompt for the token. API data and mutations remain protected. The token is stored only in that browser tab's `sessionStorage` by the bundled UI and is not logged by the server.

This bearer-token boundary is transport-independent. If the application is exposed across a network that is not already trusted/encrypted, terminate TLS in a suitable reverse proxy or tunnel; the embedded server does not implement TLS.

## Browser/session isolation

Each browser tab creates an opaque session identifier in `sessionStorage` and sends it on same-origin API requests as:

```text
X-CAD-Session-Id: <session-id>
```

The server isolates these mutable objects per session:

- latest submitted/evaluated `Economy` state;
- latest negotiation analysis;
- latest bargaining Monte Carlo analysis;
- delegation negotiation controls;
- diplomat-room mandate, offer, concession, playbook and debrief state.

Room event logs are written beneath:

```text
runtime/sessions/<session-id>/negotiation-room.events
```

(on Windows, the runtime root uses the application's local application-data directory when available).

A client that omits `X-CAD-Session-Id` uses the compatibility `default` session. Session identifiers are restricted to a short filename-safe alphabet and cannot contain path separators.

Session locking is intentionally narrower than model computation: an expensive policy evaluation runs without holding the session mutex, then atomically publishes its resulting session state. Separate sessions can therefore progress concurrently; state mutations within one session remain ordered.

## HTTP request boundary

The embedded server accepts HTTP/1.0 and HTTP/1.1 origin-form requests. It deliberately implements a constrained subset rather than pretending to be a general-purpose web server.

The boundary is fail-closed:

- request headers are limited to 32 KiB;
- request bodies are limited to 128 KiB;
- duplicate headers are rejected;
- `Content-Length` must be a strict non-negative decimal value;
- bytes beyond the declared body are rejected;
- non-identity `Transfer-Encoding` (including chunked requests) is rejected;
- POST API requests require `Content-Type: application/json`;
- API JSON remains the scalar-object contract documented by the runtime parser;
- sockets use finite receive/send timeouts.

The application is intentionally `Connection: close`; persistent HTTP connection management is outside the embedded-server scope.

## Live Bank of Canada baseline cache

`GET /api/baseline` no longer performs network I/O synchronously. At startup the server immediately has a calibrated baseline available, while a background cache worker refreshes the supported Bank of Canada Valet series.

The policy-rate, USD/CAD and WTI fetches are launched concurrently and each retains the existing finite curl timeout. The cache refreshes periodically (five minutes by default), while `/api/baseline` returns the most recent cached state immediately.

The response includes a `cache` object with the refresh state and interval. A calibrated cache warming or failed live refresh remains explicitly distinct from a successfully refreshed `live-partial` baseline.

This removes external-data latency from the API request hot path; it does not change which state variables are considered live-observed versus calibrated/default inputs.

## Structural robustness: interactive and batch modes

Two endpoints use the same full production `PolicyEngine` re-optimization for every structural draw:

- `POST /api/v2/robustness` — interactive mode, 1–24 structural draws (default 6);
- `POST /api/v2/robustness-batch` — explicit research batch mode, 25–128 structural draws (default 48).

The batch endpoint is intentionally synchronous from the caller's perspective: it may consume substantial CPU, but the worker pool prevents that request from serializing unrelated sessions.

Robustness JSON now reports sampling precision for the principal binomial stability statistics:

- recommendation win-rate Wilson 95% interval;
- recommendation win-rate Monte Carlo standard error;
- strategy-family win-rate Wilson 95% interval;
- policy-control and sector-package retention Wilson intervals;
- whether the recommendation classification is stable at 95% relative to the declared 40%, 60% and 80% classification boundaries.

These intervals quantify Monte Carlo sampling error of the structural ensemble. They are not economic parameter confidence intervals and do not convert provisional structural assumptions into empirical estimates.

## Concurrency and research semantics

Concurrency changes server scheduling, not model mathematics. `CalibratedPolicyEngine` and `PolicyEngine` evaluations remain deterministic for a fixed submitted state, seed and calibration. Common-random-number semantics inside the model are unchanged.

The server remains a research application. Do not store classified, protected, privileged or negotiation-sensitive records without an accredited security environment, even when bearer authentication is enabled.
