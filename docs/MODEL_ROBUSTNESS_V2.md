# Model Robustness V2

Canada Policy Studio distinguishes simulation precision from empirical confidence. More Monte Carlo draws reduce simulation error conditional on a model; they do not validate structural coefficients. V2 therefore places a documented structural-uncertainty layer around the complete policy decision process, a vintage-based empirical diagnostic layer around historical episodes, and a separate normative-preference sensitivity layer around the decision objective.

## Current V2 status

The active V2 robustness implementation is a **full nested decision sensitivity experiment**:

1. Run the production policy/sector optimization under the reference calibration.
2. Draw structural macro/transmission parameter sets from the documented structural registry.
3. Inside every structural draw, rerun the complete 288-candidate generated policy-control search used by the production engine.
4. Combine the newly selected custom controls with the 13 fixed expert strategies.
5. Re-optimize each strategy's 20-sector negotiation package across the production Pareto-screened finalist schedules.
6. Use 700 common-random-number paths for policy/finalist selection and 2,800 paths for verification.
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

Historical vintage backtesting is a separate active V2 diagnostic layer. It asks how the model behaves when constrained to information available at a historical decision date.

Every fixture declares a `decision_date` and separates rows into:

- `INPUT`: may be consumed by the model and must have `release_date <= decision_date`;
- `BENCHMARK`: an ex-post observed policy decision and must have `release_date > decision_date`;
- `OUTCOME`: realized macro data used only for diagnostics and must have `release_date > decision_date`.

A single date violation invalidates the entire fixture with `lookahead-failed`; the engine does not silently omit the offending datum. Empirical observations also require source provenance.

The shipped suite contains three independently dated monetary-policy episodes:

- `ca-2015-01-20-oil-shock` — immediately before the Bank's January 21, 2015 oil-insurance cut;
- `ca-2020-03-03-pandemic-onset` — immediately before the March 4, 2020 scheduled COVID cut;
- `ca-2022-07-12-inflation-tightening` — immediately before the July 13, 2022 100-basis-point tightening move.

Historical state now has two declared coverage tiers. The original core tier preserves the existing policy/inflation/Canadian-growth/labour/tariff-scope contract. The expanded tier adds USD/CAD, WTI oil, global growth, U.S. GDP growth, U.S. inflation, federal fiscal balance/GDP, federal debt/GDP and household debt/disposable income. All three shipped fixtures are required by CI to reach 100% in both declared tiers while remaining provenance-complete and no-look-ahead clean.

The state contract intentionally does not count `credit_spread` or `housing_gap` as historically reconstructed. Those fields are model abstractions without a defined one-to-one observable series in the current engine. They retain the engine default in historical runs, and CI explicitly guards against silently substituting an arbitrary market spread or housing index.

Per-episode diagnostics report policy-direction agreement, basis-point policy error, terminal forecast errors, directional accuracy, core/expanded/combined coverage, provenance completeness and no-look-ahead status.

`backtest_suite.hpp` adds cross-episode diagnostics: policy-direction accuracy and mean absolute first-move error, direction accuracy and mean absolute terminal error for inflation/GDP/unemployment, expanded-complete fixture count, and mean/minimum historical state coverage. Aggregate diagnostics are exposed only when at least three fixtures are valid, provenance-complete and no-look-ahead clean. That threshold permits reporting; it does **not** imply statistical validation or adequate sample size.

## 5. Welfare-weight sensitivity

Normative preference sensitivity is now an active V2 layer. It varies the explicit Canada/U.S. delegation priority weights and risk-aversion input while keeping the economic calibration, BoC mandate-loss coefficients, cooperation ceiling and bilateral growth floor fixed.

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

Historical results remain separate. `backtest_to_json()` exposes a single vintage run, including core/expanded/combined state coverage, while `backtest_suite_to_json()` exposes cross-episode diagnostics and coverage summaries. Normative preference sensitivity is exposed separately through `welfare_sensitivity_to_json()` so preference dependence cannot be confused with structural or historical uncertainty.

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
16. Full-production normative preference grid over bilateral priority and risk aversion, with retention/switch/fairness diagnostics.

Next:

17. Define defensible observable mappings for model-specific `credit_spread` and `housing_gap`, or keep them explicitly structural/defaulted.
18. Surface structural provenance, decision robustness, historical diagnostics and preference sensitivity in the application/API.
19. Replace provisional structural envelopes with empirical estimates where defensible.
20. Add typed sensitivity for internal component-loss weights, explicitly separating mandate-fixed from assumed welfare coefficients.
21. Add later-tightening, hold/soft-landing and eventually tariff-specific historical episodes.

## Research interpretation

Canada Policy Studio remains a scenario comparator rather than an official forecast or causal model. V2 is intended to reveal where a recommendation depends on structural assumptions, policy-control adaptation, sector adaptation, historical information constraints and normative choices instead of hiding those dependencies behind additional simulation precision.
