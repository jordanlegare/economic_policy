# Canada Policy Studio

Canada Policy Studio is a dependency-light C++17 monetary, fiscal, trade and negotiation scenario engine with a browser dashboard for examining Canada–U.S. tariff shocks and policy responses. It searches monetary, fiscal, productive-investment, negotiated-relief, diversification and sector-tariff choices, then reruns candidate packages through stochastic macro, trade-network and bilateral-welfare checks.

The production search contains **288 generated policy-control combinations plus 13 expert strategies**. Ordinary strategy evaluation uses 700 common-random-number stochastic paths over 12 quarters; sector-selected finalists can be re-simulated at 2,800 draws before a package is allowed to carry the stronger verified-win-win label.

> **Research disclaimer:** This is a research decision-support and scenario-comparison system, not an official Bank of Canada model, government forecast, negotiating mandate or causal estimate. Monte Carlo precision is conditional on the model. A software-verifiable optimum is not the same thing as empirical validation, political acceptance or legal authority.

## Current evidence status

The plumbing is substantially stronger than the empirical identification layer, and the software is designed to show that distinction rather than hide it.

- **Canada production network:** empirical 20×20 domestic direct-requirements matrix aggregated from Statistics Canada Table 36-10-0001-01 for 2024.
- **U.S. production network:** the active fallback is the U.S.-specific **EPA USEEIO v2.5 `USEEIOv2.5-catbird-22` proxy**, not the Canadian matrix. EPA's detailed v2.5 model is a 2022 model with underlying 2017 U.S. input-output data. It remains explicitly non-current-vintage and `us_trade_input_output_empirical() == false` until a reviewed BEA artifact is certified.
- **BEA promotion boundary:** BEA activation is content-bound, not presence-only. The generated year, table IDs, CSV hash, header hash and deterministic artifact fingerprint must match a separately reviewed certification marker before the runtime can switch from EPA USEEIO to BEA.
- **Canada↔U.S. bilateral sourcing:** the OECD ICIO builder exists and is fail-closed, but bilateral intermediate-input sourcing is **not active in production** until an official OECD archive and reviewed fractional ICIO→20-sector crosswalk are supplied and reviewed.
- **Structural calibration:** direct empirical mappings are counted separately from reference-only evidence and model assumptions. V3 makes eight production-network transmission assumptions explicit in the structural registry and denominator rather than leaving them as hidden constants. Their sensitivity ranges and declared dependence assumptions are model-risk inputs, not empirical estimates or confidence intervals.
- **Historical validation:** three full macro-policy episodes (2015, 2020 and 2022) are shipped with no-look-ahead and state-measurement checks. They support descriptive diagnostics, not statistical validation. A separate 2018 Section 232 fixture stress-tests directional trade channels.
- **Current legal-state snapshot:** the frozen calibration snapshot is dated **2026-08-12**. Measures announced for later effective dates remain visible in the legal timeline but are excluded from the effective tariff state until their stated effective date; the currently recorded Section 338 measure is therefore marked future until **2026-08-19**.

The browser's visible **50% U.S. tariff opening is a stress/scenario input**, deliberately distinct from the date-gated official calibration snapshot. Do not read the 50% opening control as a claim about the current legally effective average tariff.

## Build and run

```bash
cmake -S . -B build
cmake --build build --parallel
./build/cad-policy-studio 8080
```

Open <http://localhost:8080>. The standalone server serves the dashboard and its JSON APIs without a runtime web framework. The default listener is loopback-only and uses a bounded worker pool, so expensive model calls from separate browser sessions can execute concurrently instead of serializing the entire server.

Each browser tab creates a separate `X-CAD-Session-Id`; latest evaluation state, delegation controls and diplomat-room state are isolated by that identifier. Stateful evaluations within one session are serialized while separate sessions remain concurrent.

Network binding is explicitly fail-closed. `--bind-all` refuses to start without an access token of at least 16 characters supplied through `CAD_POLICY_STUDIO_TOKEN` or `--auth-token`; network-bound `/api/*` calls then require `Authorization: Bearer <token>`. Prefer the environment-variable form on shared systems, and use TLS termination/tunnelling if traffic leaves a trusted encrypted boundary.

See [`docs/SERVER_OPERATIONS.md`](docs/SERVER_OPERATIONS.md) for worker, session, HTTP, authentication and live-cache contracts.

## Test

```bash
ctest --test-dir build --output-on-failure
```

The repository also carries JavaScript, Python, calibration-integrity, sanitizer and Windows standalone checks in CI. Operational tests launch the real server and verify session isolation, strict HTTP handling, authenticated network binding and non-blocking baseline responses.

## Model architecture

- **Monetary block:** endogenous rate reaction, neutral-rate estimate, inflation expectations and exchange-rate transmission.
- **Real block:** output gap, growth, labour market, productivity, population and external demand.
- **Financial block:** credit spreads, household leverage, housing pressure and rate-sensitive financial conditions.
- **Fiscal block:** fiscal impulse, debt/balance dynamics and productive-investment supply effects.
- **Trade block:** bilateral tariff incidence, export exposure, retaliation, border friction, trade elasticities, pass-through and diversification. Macro trade, tariff ledgers, direct sector welfare and the production network share one bounded directional constant-elasticity quantity response rather than maintaining parallel linear volume equations.
- **Tariff fiscal ledger:** U.S. and Canadian tariff receipts are calculated from effective sector-weighted rates and the elasticity-adjusted bilateral goods base. Bilateral trade balances are reported outcomes, not welfare targets.
- **Country-specific production networks:** Canada and the United States always use separate network objects. Canada's matrix is the frozen StatCan empirical artifact. The U.S. object is either the EPA USEEIO proxy or, only after exact certification, a BEA artifact. Supplier-demand, input-cost, price, output and jobs transmission coefficients are explicit structural-registry parameters.
- **Directional incidence:** U.S.-import and Canadian-import directions can carry separate sector elasticities and price-pass-through mappings. Missing or incompatible estimates fall back to declared aggregate anchors rather than being silently promoted.
- **Whole-economy sector view:** 20 sectors expose output, jobs, prices, applied tariffs, buyer pass-through, exporter/importer absorption and propagated input-cost pressure.
- **Risk layer:** stochastic recession, inflation-tail, debt and stress-regime diagnostics are reported separately from empirical-confidence claims. Structural robustness validates and samples declared parameter-dependence structures rather than silently assuming every structural coefficient is independent.
- **Decision layer:** Bank of Canada, Canadian fiscal/household and U.S. loss components are combined through disclosed weights, bilateral feasibility checks, fairness protection and tail-risk penalties.
- **Operational layer:** strict bounded HTTP parsing, per-session mutable state, bounded worker concurrency, authenticated network binding and cached external-market observations are kept outside the model equations.

Legacy request-state members that do not yet have production equations remain internal compatibility placeholders and are not exposed as live policy controls.

## Search and trust semantics

The engine does not treat a high aggregate score as sufficient for a win-win label.

- The generated policy grid spans monetary, fiscal, productive-investment, negotiated-relief and diversification choices.
- Sector tariff coverage is searched jointly through a finite Pareto dynamic program rather than optimized one sector at a time and concatenated afterward.
- The searched sector grid includes 0%, 25%, 50%, 75%, 100% and the submitted coverage value when it lies between those points.
- Selected sector schedules are propagated back through the stochastic macro engine.
- Common random numbers are used so alternatives are compared under the same shock draws.
- Bilateral growth floors and modeled national no-harm/reservation checks can reject otherwise attractive packages.
- The linked-issue bargaining layer retains the complete practical 0.5-point epsilon-Pareto frontier and evaluates robust candidates with bounded-memory two-pass common-random-number logic.
- A verified package is therefore **verified within the declared finite model/search design**, not proven globally optimal in continuous policy space.

See [`docs/MODEL_TRUST.md`](docs/MODEL_TRUST.md), [`docs/MODEL_CONSISTENCY_V3.md`](docs/MODEL_CONSISTENCY_V3.md), and [`docs/NEGOTIATION_ENGINE.md`](docs/NEGOTIATION_ENGINE.md) for the formal trust contracts.

## Data, calibration and production-network refresh

### Canada input-output

The Canadian production artifact is generated from Statistics Canada data and frozen under `data/calibration/` with source/provenance hashes and a generated C++ header.

### U.S. input-output

The current U.S. fallback is generated from EPA USEEIO v2.5. Its committed CSV/header/provenance identify the source workbook, `A_d` domestic direct-requirements matrix, `q` output weighting, mapped commodities, exclusions and source hash.

`tools/build_bea_io_matrix.py` is the preferred current-vintage U.S. replacement path. It requires authenticated BEA extraction and produces the matrix, generated header, contract metadata and provenance. `tools/verify_bea_io_certification.py` independently rejects partial artifacts, hash drift, vintage/table drift and mismatched certification markers. An unreviewed BEA header cannot silently activate production.

See [`docs/US_IO_NETWORK.md`](docs/US_IO_NETWORK.md).

### Bilateral intermediate sourcing

`tools/build_oecd_bilateral_io.py` accepts an OECD ICIO ZIP obtained directly from the official OECD distribution plus a reviewed fractional industry crosswalk. It produces Canada←U.S. and U.S.←Canada intermediate-input sourcing shares with SHA-pinned provenance. Third-party mirrors are not accepted as empirical source material, and generation alone does not activate the artifact in production.

### Structural parameters

`data/calibration/structural_parameter_registry.csv` is the executable structural registry. It records baselines, units, provenance class, vintage, bounds, distributions, sampling status and explicit `CORR` dependence records. The current V3 registry includes the production-network transmission coefficients that were formerly embedded in code. `data/calibration/empirical_structural_evidence.csv` records direct versus reference-only evidence, while `data/calibration/realized_calibration_frontier.csv` records which remaining shock variances and transmission multipliers are still blocked by missing aligned realized identification data.

Declared structural dependence is applied through a Gaussian correlation matrix and Cholesky factorization before normal/lognormal parameter transforms. Invalid non-positive-semidefinite dependence structures fail closed during robustness sampling. The frozen output/inflation residual correlation remains a macro innovation-process calibration and is not repurposed as covariance between uncertain parameter estimates.

`tools/verify_structural_promotions.py` prevents reference-only evidence from being silently promoted into a production coefficient.

## Current-state baseline

`GET /api/baseline` returns from an in-memory cache and therefore does not wait on Bank of Canada network requests. A background worker refreshes the supported live-capable policy-rate, USD/CAD and WTI observations concurrently at a finite interval. The cache is immediately initialized with the calibrated baseline, so startup remains usable even when live fetches are slow or unavailable.

A partially refreshed baseline is explicitly labelled `live-partial`; the rest of the macro state remains calibrated/default input unless an authoritative ingestion path supplies it. The response also identifies whether a refresh is in progress.

The visible 50% startup trade shock is applied only to the first scenario evaluation. The underlying calibration endpoint remains the date-gated economic/legal snapshot and is restored immediately afterward.

## Model-evidence APIs

The V2 API surface keeps structural uncertainty, historical diagnostics and normative preference sensitivity separate; the V3 structural registry strengthens the model-consistency and dependence contract behind those endpoints.

- `GET /api/v2/structural-registry` — structural coefficient provenance, bounds, distributions, declared correlations and direct-calibration completeness.
- `GET /api/v2/backtests` — no-look-ahead macro-policy historical diagnostics.
- `GET /api/v2/evidence-status` — compact runtime/evidence readiness, including state-measurement, decision-loss and country-I/O status.
- `GET /api/v2/state-measurements` — the production state-measurement contract.
- `POST /api/v2/robustness` — interactive full nested structural-decision robustness; `parameterDraws` must be 1–24 (default 6), and every draw reruns the production `PolicyEngine`.
- `POST /api/v2/robustness-batch` — explicit research batch mode; `parameterDraws` must be 25–128 (default 48), with the same full production re-optimization per draw.
- `POST /api/v2/welfare` — delegation-preference and internal-component sensitivity with the production optimizer rerun for every profile.
- `GET /api/health` — compact operational state for worker/session/cache/auth diagnostics.

Structural robustness reports the dependence mode, declared correlation-pair count and correlation-matrix validity together with Wilson 95% intervals and Monte Carlo standard error for recommendation-retention rates. It also reports whether the discrete robustness classification is stable at 95% relative to its declared thresholds. These intervals measure **simulation sampling precision**, not economic parameter uncertainty or empirical identification.

The interactive structural screen uses 6 draws for responsiveness; larger research runs should use the batch endpoint rather than silently raising the interactive cap.

See [`docs/MODEL_ROBUSTNESS_V2.md`](docs/MODEL_ROBUSTNESS_V2.md), [`docs/MODEL_CONSISTENCY_V3.md`](docs/MODEL_CONSISTENCY_V3.md), [`docs/HISTORICAL_BACKTESTING.md`](docs/HISTORICAL_BACKTESTING.md), [`docs/WELFARE_SENSITIVITY.md`](docs/WELFARE_SENSITIVITY.md), and [`docs/SERVER_OPERATIONS.md`](docs/SERVER_OPERATIONS.md).

## Repository governance

`main` is protected by an active GitHub ruleset: changes require a pull request, required review conversations must be resolved, the branch must be up to date, force-push/deletion paths are blocked, and the required CI contexts must pass before merge. The protected checks cover U.S. I/O integrity, quarterly calibration, frontend checks, GCC, Clang, sanitizers and Windows x64.

This protects software/evidence contracts from unreviewed drift; it does not turn provisional economic assumptions into empirical estimates.

## Responsible decision use

Before operational use, analysts should replace scenario inputs with a dated tariff inventory and reviewed assumptions appropriate to the decision date, inspect the legal timeline, and run structural and preference sensitivity. Particular caution is warranted around the still-provisional macro multipliers, shock variances, production-network transmission coefficients, U.S. input-output vintage and inactive bilateral-sourcing layer.

Useful primary reference points include the [Bank of Canada Monetary Policy Report](https://www.bankofcanada.ca/mpr/), [Statistics Canada Canadian international merchandise trade](https://www.statcan.gc.ca/en/subjects-start/international_trade), [Statistics Canada input-output tables](https://www150.statcan.gc.ca/t1/tbl1/en/tv.action?pid=3610000101), [U.S. BEA Input-Output Accounts](https://www.bea.gov/data/industries/input-output-accounts-data), [U.S. EPA USEEIO](https://www.epa.gov/land-research/us-environmentally-extended-input-output-useeio-models), [Department of Finance Canada](https://www.canada.ca/en/department-finance.html), and the [U.S. International Trade Commission HTS](https://hts.usitc.gov/).