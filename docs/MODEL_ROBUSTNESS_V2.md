# Model Robustness V2

Canada Policy Studio distinguishes simulation precision from empirical confidence. More Monte Carlo draws reduce simulation error conditional on a model; they do not validate structural coefficients. V2 therefore places a documented structural-uncertainty layer around the complete policy decision process, a vintage-based empirical diagnostic layer around historical episodes, and a separate normative-preference sensitivity layer around the decision objective.

## Current V2 status

The active V2 robustness implementation is a **full nested decision sensitivity experiment**:

1. Run the production policy/sector optimization under the reference calibration.
2. Draw structural macro/transmission parameter sets from the documented structural registry.
3. For every structural draw, construct a production `PolicyEngine` with those parameters and rerun `evaluate()`; robustness no longer maintains a duplicate macro/sector simulator.
4. The production rerun carries the complete 288-candidate generated policy-control search, 13 expert strategies, country-specific trade-network objects, sector Pareto search, bilateral growth constraints and stochastic verification.
5. Use the same seeded production Monte Carlo/common-random-number semantics under every calibration and compare exact controls, strategy family and sector package with the reference production result.
6. The linked-issue negotiation stage retains the complete 0.5-point epsilon-Pareto set and evaluates it with a bounded-memory two-pass common-random-number robust algorithm rather than truncating at 512 packages.
7. Re-rank the fully re-optimized decisions and report how often the reference control decision remains the winner.

Both generated policy controls and sector coverage are endogenous to every structural calibration. With `uncertainty_scale=0`, full V2 must recover the production 288-control winner, production sector packages and production recommendation exactly.

## 1. Separate the model inputs

Research inputs are classified conceptually as:

- **ObservedState** — dated macroeconomic and trade observations.
- **StructuralParameters** — elasticities, pass-through coefficients, transmission coefficients and stochastic-process parameters.
- **PolicyControls** — monetary, fiscal, relief, diversification and tariff-policy choices.
- **NegotiationPreferences** — Canada/U.S. priorities, risk tolerance, cooperation constraints and bilateral growth floors. These are mandates/delegation inputs, not estimated structural parameters.
- **CalibrationMetadata** — source, vintage, classification, sensitivity envelope and fallback status.

Every V2 structural coefficient has a registry record specifying its baseline, unit, provenance kind, source ID, vintage, lower/upper bounds, distribution and whether it is sampled. Current uncertainty envelopes are sensitivity ranges, not empirical confidence intervals.

## 2. Parameter uncertainty

V2 samples structural parameters separately from stochastic macro innovations. The same structural seed reproduces the parameter ensemble, while common random numbers hold path innovations fixed across calibrations, policy alternatives and competing sector packages.

The active set includes the neutral rate; monetary-policy response coefficients; output/inflation persistence; fiscal, real-rate and productive-supply transmission; global-growth sensitivity; Phillips-curve and pass-through terms; trade-drag scales; tariff-ledger elasticity; and macro/export shock standard deviations.

The 2% inflation target is a fixed mandate and is never sampled. The expectations weight is derived jointly with inflation persistence so the reference inflation anchor is preserved.

The headline recommendation survival rate refers to the **same reference control decision**, not merely the same strategy label. If `custom` still wins but with different controls, that counts toward `strategyFamilyWinRate` but not toward `recommendationWinRate`.

Project decision labels remain `robust` at >=80%, `moderately-robust` at >=60%, `fragile` at >=40%, and `unstable` below 40%. These are project decision labels, not statistical confidence intervals.

## 3. Full policy-control search

The production generated-policy grid is rerun inside every structural draw: three first monetary moves, four fiscal impulses, three productive shares, four cooperation factors and two diversification settings, for 288 generated candidates per structural calibration. The best generated candidate competes with the fixed expert strategies after nested sector re-optimization.

The contract reports policy-control candidates examined, control changes, reference-control retention, strategy-family wins and strategy-family win rate, in addition to the sector-package audit metrics.

## 4. Historical backtesting

Historical vintage backtesting is a separate active V2 diagnostic layer. It asks how the model behaves when constrained to a reconstructed historical decision state. Ordinary inputs must have `release_date <= decision_date`; benchmark and outcome data are strictly ex post. Public historical reconstructions are separately labelled and may use a later-open publication only to recover a market observation dated before the decision cutoff. They are not represented as frozen real-time source vintages.

The shipped suite contains three independently dated monetary-policy episodes:

- `ca-2015-01-20-oil-shock` — immediately before the Bank's January 21, 2015 oil-insurance cut;
- `ca-2020-03-03-pandemic-onset` — immediately before the March 4, 2020 scheduled COVID cut;
- `ca-2022-07-12-inflation-tightening` — immediately before the July 13, 2022 100-basis-point tightening move.

Historical state has two backward-compatible declared coverage tiers. The core/expanded 16-field contract remains unchanged and all three fixtures retain 100% coverage.

### Full modeled-state measurement audit

V2 separately audits all 18 modeled empirical states by adding `credit_spread` and `housing_gap` to the 16 declared fields. A model-specific state counts as resolved only when its registry status is `ready` and the fixture supplies the input.

`housing_gap` is materialized from the Bank of Canada Housing Affordability Index with a prior-20-quarter median that excludes the current quarter. `data/calibration/housing_affordability_benchmarks.csv` records the dated derivation.

`credit_spread` is now materialized as a broad Canadian corporate-minus-government yield spread in percentage points, consistent with the Bank of Canada research definition. V2 prefers an official broad-market historical spread and otherwise a published Canadian investment-grade index spread; bank lending rates, single-issuer spreads and generic financial-conditions indexes are disallowed substitutes. `data/calibration/credit_spread_benchmarks.csv` records source family, historical reference date and reconstruction method.

The three credit-spread inputs are 1.26 pp for January 2015, 1.50 pp for February 2020 and 1.37 pp for 2022Q1-end. Some raw benchmark histories are proprietary and some open reproductions were published later, so this is a **public historical-state reconstruction**, not a claim that all 18 fields have frozen real-time public-source vintages.

The shipped suite therefore resolves **18/18 (100%) of the modeled empirical-state audit** while preserving the separate 16-field declared-state coverage contract.

Per-episode diagnostics report policy-direction agreement, basis-point policy error, terminal forecast errors, directional accuracy, core/expanded/combined declared coverage, provenance completeness and no-look-ahead status. Aggregate diagnostics remain descriptive and require at least three valid fixtures; that threshold is not statistical validation.

## 5. Welfare-weight sensitivity

Normative preference sensitivity is an active V2 layer. It varies the explicit Canada/U.S. delegation priority weights and risk-aversion input while keeping the economic calibration, BoC mandate-loss coefficients, cooperation ceiling and bilateral growth floor fixed.

The default local grid is centred on the submitted preferences and evaluates Canada-priority shifts of -15/0/+15 percentage points crossed with risk-aversion shifts of -25/0/+25 points. Every profile reruns the full production 288-control search, sector Pareto optimization and 700/2,800-draw verification path; this is not a score-only reranking of frozen scenarios.

`evaluate_welfare_sensitivity()` reports reference-control retention, strategy-family retention, sector-package retention, supported alternative winners, fairness range and the nearest tested one-dimensional priority/risk shift that changes the control decision. `welfare_sensitivity_to_json()` exposes the standalone contract.

The internal BoC/federal/U.S. component-loss coefficients remain fixed in this milestone. `mandateWeightsFixed=true` is audited across every preference profile. Sensitivity of those internal component weights is a later extension and should use a typed weight registry rather than anonymous literals.

## 6. Three-layer architecture

Keep the system conceptually separated:

```
Economic model -> Decision engine -> Presentation layer
```

The economic model produces conditional outcome distributions. The decision engine applies mandates, institutional constraints, policy-control search, Pareto/Nash logic and robustness criteria. The presentation layer explains trade-offs, provenance, uncertainty and model disagreement without changing economic results.

## 7. Output contracts

The structural-robustness contract exposes recommendation survival, structural calibration identity, parameter provenance/bounds, common-random-number status, policy-control and sector re-optimization audit counts, retention metrics and robustness classification through `robustness_to_json()`.

Historical results remain separate. `backtest_to_json()` exposes a single historical run, while `backtest_suite_to_json()` exposes cross-episode diagnostics. Normative preference sensitivity is exposed separately through `welfare_sensitivity_to_json()`.

The state-measurement registry plus the housing and credit-spread benchmark ledgers expose the empirical-definition and reconstruction status of model-specific state fields without redefining the legacy declared-state coverage metric.

A recommendation is **robust** only when the same control decision remains highly ranked after structural uncertainty, endogenous policy-control search and endogenous sector-package adaptation are all accounted for. Historical backtests address empirical diagnostic performance, while welfare sensitivity addresses normative preference dependence; none substitutes for the others.

## 8. Implementation sequence

Completed or active:

1. `StructuralParameters` type boundary and calibration identity.
2. Deterministic parameter-draw generation.
3. Reproducibility and zero-uncertainty tests.
4. Active parameterized V2 macro re-simulation.
5. Common-random-number structural ranking.
6. Dedicated robustness JSON contract.
7. Typed structural provenance records and explicit sensitivity bounds.
8. Nested sector-package re-optimization inside every structural draw.
9. Full 288-candidate generated policy-control re-optimization inside every structural draw.
10. Exact recommendation-control survival separated from strategy-family survival.
11. Historical vintage fixture schema and hard no-look-ahead validation.
12. Source-backed 2015, 2020 and 2022 historical decision fixtures.
13. Cross-episode direction/MAE diagnostics with a three-fixture reporting guard.
14. Native CI contamination test that injects future information and requires rejection.
15. Expanded historical state tier covering FX, oil, global/U.S. macro, federal fiscal state and household leverage.
16. Full-production normative preference grid over bilateral priority and risk aversion.
17. V2 evidence APIs and browser panel.
18. Explicit 18-field empirical coverage audit.
19. Vintage-safe one-sided Housing Affordability Index mapping and benchmark ledger.
20. Public Canadian corporate-spread reconstruction with fixture-level audit ledger; modeled empirical-state coverage reaches 18/18.

Next:

21. Replace provisional structural envelopes with empirical estimates where defensible; the structural-promotion CI gate now prevents reference-only evidence from being promoted accidentally.
22. Replace the explicitly provisional U.S. production-network proxy with the versioned BEA 20-sector artifact generated by `tools/build_bea_io_matrix.py` once authenticated source extraction is available.
23. Add typed sensitivity for internal component-loss weights, explicitly separating mandate-fixed from assumed welfare coefficients.
24. Expand historical validation beyond the three macro-policy fixtures. The 2018 Section 232 steel/aluminum fixture now provides a separate treated-product trade-channel stress/falsification benchmark without pretending to be a complete 18-state macro vintage.
25. Improve archival real-time vintage quality for reconstructed market-state inputs where open historical feeds become available.

## Research interpretation

Canada Policy Studio remains a scenario comparator rather than an official forecast or causal model. V2 is intended to reveal where a recommendation depends on structural assumptions, policy-control adaptation, sector adaptation, historical information constraints and normative choices instead of hiding those dependencies behind additional simulation precision.
