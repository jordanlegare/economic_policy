# Canada Policy Studio

A dependency-light C++17 monetary–fiscal scenario engine and professional browser dashboard for testing how U.S. tariffs affect Canada and the United States under a broad menu of monetary, fiscal, trade and supply-side strategies. The dashboard reruns and re-scores modeled impacts when a user changes the tariff, bilateral priorities, risk tolerance, negotiated-relief ceiling or sector coverage. The server can refresh its economic baseline from public internet sources and labels fallback operation clearly.

The current engine evaluates 14 displayed scenarios, including generated policy candidates, through 700 seeded stochastic paths over 12 quarters. A three-party decision score rewards outcomes that serve the Bank's mandate, Canadian fiscal/household sustainability and U.S. trade-price interests while using a fairness floor so a weak party cannot simply be averaged away. Final sector packages can be re-simulated at 2,800 draws for verification.

> **Research disclaimer:** This is an illustrative policy-analysis tool, not an official Bank of Canada model, forecast or recommendation. It does not reproduce Bank systems or federal budget projections. Monte Carlo precision is conditional on the model and does not establish empirical validity of structural coefficients.

## Build and run

```bash
cmake -S . -B build
cmake --build build --parallel
./build/cad-policy-studio 8080
```

Open <http://localhost:8080>. The server has no runtime framework dependencies and serves the dashboard plus `POST /api/evaluate`.

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Model design

- **Monetary block:** endogenous rate reaction, neutral-rate estimate, inflation expectations and exchange-rate pass-through.
- **Real block:** output gap, labour market, productivity, population and global demand.
- **Financial block:** credit spreads, household leverage and rate-sensitive housing valuation.
- **Fiscal block:** balance and debt dynamics, the searched fiscal impulse and the supply benefit of productive investment. Legacy `program_growth`, `tax_impulse`, `infrastructure_impulse`, and `household_debt_income` members remain ABI-compatible internal placeholders but are no longer accepted or advertised as production request-state controls until explicit equations are implemented.
- **Tariff fiscal ledger:** U.S. and Canadian customs revenue in USD and CAD from effective sector-weighted tariff rates and an elasticity-adjusted bilateral goods base. Bilateral trade balances are reported outcomes, not welfare targets.
- **Trade block:** effective bilateral tariffs, retaliation, border friction, import-price pass-through, export exposure, trade elasticity, U.S. demand and market diversification.
- **Empirical Canadian production network:** the Canadian 20×20 domestic direct-requirements matrix is reproducibly aggregated from Statistics Canada Table 36-10-0001-01 (2024). Matrix entries are inter-industry purchases divided by downstream gross output; imports, taxes, value added and final demand remain outside the domestic matrix. The committed CSV/generated header/provenance are frozen calibration artifacts rather than hand-tuned coefficients.
- **Country-specific network boundary:** Canada and the United States use separate network objects. The Canadian object is empirical. Until the BEA refresh pipeline produces and certifies a U.S. artifact, the U.S. object is explicitly a structural proxy and must not be described as empirical U.S. input-output calibration. `tools/build_bea_io_matrix.py` is the fail-closed BEA refresh path.
- **Directional tariff incidence:** U.S.-import and Canadian-import directions can carry independent sector trade elasticities and price pass-through values. Production-compatible sector elasticities in the certified snapshot are reattached server-side on every evaluation; Canadian retaliatory-tariff pass-through evidence is activated only in the supported Canadian-import direction. Missing/reference-only or directionally incompatible estimates fall back to the declared aggregate anchor.
- **Household block:** cost-of-living pressure and real-income growth alongside housing and unemployment.
- **Whole-economy sector view:** all 20 two-digit NAICS sectors expose Canadian and U.S. output, employment, consumer-price, applied-tariff, buyer pass-through, margin-absorption and upstream-cost diagnostics.
- **Directional bilateral channels:** U.S. tariff coverage is weighted by Canadian sectoral export exposure, while Canadian retaliation is weighted by U.S. sales into Canada. U.S. export welfare does not reuse Canadian export accounting.
- **Sector negotiation search:** an exact Pareto dynamic program searches permitted sector-coverage choices at 25% increments of each side's residual relief envelope. Packages that make either party worse than its baseline are rejected; top frontier packages are stochastic re-simulated before recommendation.
- **Complete linked-issue bargaining frontier:** the five-issue, five-level negotiation layer retains the entire practical 0.5-point epsilon-Pareto set. The robust second stage evaluates every retained package with a bounded-memory two-pass common-random-number algorithm rather than truncating the frontier at 512 packages.
- **Cooperation envelope:** negotiated headline-rate relief and sector-coverage relief share one bounded cooperation envelope, preventing independent concession controls from exceeding the configured ceiling.
- **Bilateral growth constraint:** every strategy projects separate Canadian and U.S. GDP-growth paths for 12 quarters and can be rejected when either side falls below the configured minimum bilateral growth floor.
- **Dynamic implementation paths:** optimizer controls remain finite amplitudes, but fiscal stimulus, productive investment, negotiated relief, targeted relief and diversification follow explicit 12-quarter ramp/persistence/fade rules. The realized paths are returned in every scenario for auditability.
- **Tail and stress-regime risk:** large standardized innovations receive an explicit heavy-tail multiplier and financial/recession/high-tariff starting states amplify macro shocks. These are registered sensitivity assumptions, not estimated tail probabilities or regime-transition parameters.
- **Risk block:** recession frequency plus 90th-percentile inflation and federal-debt stress outcomes.
- **Decision layer:** separate BoC, Canadian federal/household and U.S. loss functions are combined through preference-weighted Nash welfare, a fairness floor and tail-risk penalties. User-supplied Canada/U.S. mandate priorities remain fixed during optimization.
- **Generated policy search:** each request evaluates a 288-combination policy search in addition to displayed expert strategies, covering monetary, fiscal, productive-investment, negotiated-relief and diversification choices.
- **Trade-balance reporting:** the engine never manufactures exports or imports to force a zero bilateral deficit. Balance, gap and progress are accounting diagnostics only.
- **Current-state baseline:** `GET /api/baseline` refreshes the three supported Bank of Canada Valet market observations at page load and reports `live-partial` when any are available. Field-level provenance explicitly identifies the remaining macro state as calibrated/default input rather than labelling the whole state live. The production server also loads and exposes the state-measurement contract at `GET /api/v2/state-measurements`.
- **Executable model-design configuration:** `data/calibration/decision_loss_weights.csv` is loaded at startup, validated as a complete 12-component ±20% sensitivity contract, and applied to every production/V2 economy rather than merely documenting duplicated C++ constants.
- **Deterministic V2 search plumbing:** robustness and welfare endpoints explicitly request the production exhaustive search mode; their optimization path no longer depends on whether a prior `/api/evaluate` call mutated cached state or whether the V2 request body was empty.
- **Server-authoritative sector UI:** browser sector sliders no longer run a parallel economic response equation. Until a changed posture is re-evaluated by the C++ engine, the UI marks the sector result pending instead of fabricating an immediate client-side counterfactual.

## Trade-data refresh and validation

The production Canadian IO artifact is versioned under `data/calibration/` and generated by the StatCan IO extraction workflow. The U.S. refresh tool requires `BEA_API_KEY`; if the key or an adequate BEA table is unavailable it exits without generating a matrix rather than substituting unrelated data.

```bash
BEA_API_KEY=... python3 tools/build_bea_io_matrix.py --year 2024
```

`tools/build_oecd_bilateral_io.py` accepts an OECD ICIO ZIP obtained directly from the official OECD distribution plus a reviewed fractional industry crosswalk. It generates Canada←U.S. and U.S.←Canada intermediate-input sourcing-share matrices with SHA-pinned provenance. Automated production activation remains disabled until official source bytes and the reviewed crosswalk are available; third-party mirrors are not accepted as empirical source material.

The repository also carries `data/backtests/2018-section232-trade-validation.csv`, a deliberately **directional/stress** validation episode. It records the 2018 steel/aluminum legal rates and later Statistics Canada evidence on treated-product export contraction and U.S. importer tariff-cost incidence. Because those estimates concern tariffed steel/aluminum products rather than the entire 20-sector manufacturing aggregate, they are used to test sign/channel behavior and sensitivity—not force-fit as whole-manufacturing structural parameters.

## V2 model-evidence APIs

V2 keeps structural uncertainty, historical diagnostics and normative preference sensitivity separate so they cannot be mistaken for one another or for ordinary Monte Carlo precision.

- `GET /api/v2/structural-registry` — structural coefficient provenance, sensitivity bounds, distributions and sampling status.
- `GET /api/v2/backtests` — the no-look-ahead 2015, 2020 and 2022 macro-policy episodes plus cross-episode diagnostics. The separate 2018 Section 232 fixture validates trade channels without pretending to be a complete 18-state macro vintage.
- `GET /api/v2/evidence-status` — compact readiness/status summary for the structural registry and historical suite.
- `POST /api/v2/robustness` — full nested structural decision robustness. The optional `parameterDraws` field is clamped to 1–24; the interactive default is 6. Every structural draw delegates back to the production `PolicyEngine`, so the robustness layer cannot drift into a parallel macro/trade model.
- `POST /api/v2/welfare` — the full 3×3 delegation-preference/risk grid plus six named ±20% internal-component weight profiles (BoC inflation, federal debt, U.S. inflation), rerunning production optimization for every profile while keeping the institutional mandate fixed.

If a V2 POST body is empty, the server analyzes the most recently evaluated calibrated economy (or the calibrated default state before the first evaluation). The Windows standalone executable embeds the structural registry and all shipped macro backtest fixtures, so these endpoints remain available when launched from an empty directory.

## Model robustness V2

V2 separates observed state, structural parameters, policy controls, negotiation preferences and calibration provenance. It includes provenance-bounded structural uncertainty, production-engine reruns under every structural calibration, strict historical-vintage backtesting, expanded historical state coverage and welfare-preference sensitivity.

See [`docs/MODEL_ROBUSTNESS_V2.md`](docs/MODEL_ROBUSTNESS_V2.md) for the implementation contract and scientific interpretation.

## Responsible decision use

The defaults are illustrative—not a live tariff schedule. Before each decision round, analysts should replace them with a dated, trade-weighted tariff inventory and documented data vintage, then run sensitivity ranges for pass-through, trade elasticity and other weakly identified structural parameters. The engine is a scenario comparator, not a causal forecasting system; it requires expert judgment, model comparison and governance review.

Useful primary reference points include the [Bank of Canada Monetary Policy Report](https://www.bankofcanada.ca/mpr/), [Statistics Canada Canadian international merchandise trade](https://www.statcan.gc.ca/en/subjects-start/international_trade), [Statistics Canada input-output tables](https://www150.statcan.gc.ca/t1/tbl1/en/tv.action?pid=3610000101), [U.S. BEA Input-Output Accounts](https://www.bea.gov/data/industries/input-output-accounts-data), [Department of Finance tariff measures](https://www.canada.ca/en/department-finance.html), and [US International Trade Commission tariff data](https://hts.usitc.gov/). Live observations are downloaded automatically where machine-readable feeds are available; every response includes its timestamp, source metadata, and live/fallback state for auditability.
