# Model Robustness V2

Canada Policy Studio distinguishes simulation precision from empirical confidence. More Monte Carlo draws reduce simulation error conditional on a model; they do not validate structural coefficients. V2 therefore places a documented structural-uncertainty layer around the complete policy decision process and a separate vintage-based empirical diagnostic layer around historical episodes.

## Current V2 status

The active V2 robustness implementation is a **full nested decision sensitivity experiment**:

1. Run the production policy/sector optimization under the reference calibration.
2. Draw structural macro/transmission parameter sets from the documented structural registry.
3. Inside every structural draw, rerun the complete 288-candidate generated policy-control search used by the production engine.
4. Combine the newly selected custom controls with the 13 fixed expert strategies.
5. Re-optimize each strategy's 20-sector negotiation package across the production Pareto-screened finalist schedules.
6. Use 700 common-random-number paths for policy/finalist selection and 2,800 paths for verification.
7. Re-rank the fully re-optimized decisions and report how often the reference control decision remains the winner.

This closes the previous fixed-policy-control qualification. Both generated policy controls and sector coverage are now endogenous to every structural calibration.

The deterministic sector Pareto screen depends on the observed economy, policy controls, priorities and cooperation envelope, but not on sampled macro structural coefficients. V2 therefore caches the 13 fixed-strategy frontiers plus the reference custom frontier. A new custom frontier is built only when a structural draw selects different generated controls. This preserves the exact search while avoiding repeated deterministic work.

`policyControlsReoptimized=true` and `sectorPackagesReoptimized=true` identify an active full decision run. The robustness contract reports policy-control search counts, control changes, strategy-family wins, nested sector optimization counts and reference-package retention.

With `uncertainty_scale=0`, full V2 must recover the production 288-control winner, production sector packages and production recommendation exactly. This is an explicit regression target.

## 1. Separate the model inputs

Research inputs are classified conceptually as:

- **ObservedState** — dated macroeconomic and trade observations.
- **StructuralParameters** — elasticities, pass-through coefficients, transmission coefficients and stochastic-process parameters.
- **PolicyControls** — monetary, fiscal, relief, diversification and tariff-policy choices.
- **NegotiationPreferences** — Canada/U.S. priorities, risk tolerance, cooperation constraints and bilateral growth floors. These are mandates, not estimated parameters.
- **CalibrationMetadata** — source, vintage, classification, sensitivity envelope and fallback status.

Every V2 structural coefficient has a registry record specifying its baseline, unit, provenance kind, source ID, vintage, lower/upper bounds, distribution and whether it is sampled.

The current envelopes are sensitivity ranges, not empirical confidence intervals. `assumed` and provisional `calibrated` entries remain visibly distinct from observed or mandated quantities.

## 2. Parameter uncertainty

V2 samples structural parameters separately from stochastic macro innovations. The same structural seed reproduces the parameter ensemble, while common random numbers hold path innovations fixed across calibrations, policy alternatives and competing sector packages.

The active set includes:

- neutral rate;
- monetary-policy inflation/output response coefficients;
- maximum quarterly policy adjustment;
- output persistence;
- fiscal demand multiplier;
- real-rate demand sensitivity;
- productive-investment supply effect;
- global-growth sensitivity;
- inflation persistence and expectations composition;
- Phillips-curve slope;
- FX, import-price and oil-price inflation pass-through;
- Canadian trade-drag and U.S. retaliation-drag scales;
- tariff-ledger elasticity scale;
- output, inflation, growth and export shock standard deviations.

The 2% inflation target is a fixed mandate and is never sampled. The expectations weight is derived jointly with inflation persistence so the reference inflation anchor is preserved.

The headline recommendation survival rate refers to the **same reference control decision**, not merely the same strategy label. If `custom` still wins but with different controls, that counts toward `strategyFamilyWinRate` but not toward `recommendationWinRate`.

Project decision labels remain:

- `robust`: recommendation win rate >= 80%;
- `moderately-robust`: recommendation win rate >= 60%;
- `fragile`: recommendation win rate >= 40%;
- `unstable`: recommendation win rate < 40%.

These are decision labels, not statistical confidence intervals.

## 3. Full policy-control search

The production generated-policy grid is rerun inside every structural draw:

- first monetary move: -25, 0 or +25 bp;
- fiscal impulse: -0.15, 0.10, 0.35 or 0.60;
- productive share: 0.35, 0.65 or 0.90;
- cooperation factor: 0, 0.33, 0.67 or 1.0 of the permitted de-escalation envelope;
- diversification boost: 0 or 0.15.

This yields 288 generated control candidates per structural calibration. Each candidate is evaluated with the parameterized macro model under common random numbers. The best generated candidate then competes with the fixed expert strategies after nested sector re-optimization.

The contract reports `policyControlCandidatesPerDraw`, total candidates examined, control changes, reference-control retention, strategy-family wins and strategy-family win rate.

## 4. Historical backtesting

Historical vintage backtesting is now an active V2 diagnostic layer. It is deliberately separated from structural robustness because it answers a different question: how did the model behave when constrained to information that was actually available at a historical decision date?

Every fixture declares a `decision_date` and separates rows into:

- `INPUT`: may be consumed by the model and must have `release_date <= decision_date`;
- `BENCHMARK`: an ex-post observed policy decision and must have `release_date > decision_date`;
- `OUTCOME`: realized macro data used only for diagnostics and must have `release_date > decision_date`.

A single date violation invalidates the entire fixture with `lookahead-failed`; the engine does not silently omit the offending datum. Empirical observations also require source provenance.

The first shipped fixture, `ca-2022-07-12-inflation-tightening`, reconstructs the information set immediately before the Bank of Canada's July 13, 2022 policy decision. It uses then-available policy-rate, inflation, GDP, unemployment and wage data, keeps the tariff channel explicitly out of scope, and compares the model's recommendation with the next day's 100-basis-point move. Twelve-quarter macro diagnostics use realized 2025 inflation, unemployment and GDP data released only after the forecast horizon.

The backtest contract reports policy-direction agreement, basis-point policy error, terminal forecast errors, directional accuracy, core-input coverage, provenance completeness and no-look-ahead status.

Backtests are diagnostics, not claims that the simulator should reproduce realized history. One episode is not validation; aggregate claims require multiple independently sourced fixtures.

## 5. Welfare-weight sensitivity

Policy rankings also depend on normative loss-function weights. V2 treats this as a separate experiment, not something the optimizer can alter opportunistically.

For a bounded grid or sampled set of admissible weights, report recommendation retention, materially supported alternatives, switch thresholds, fairness changes and whether hard mandate constraints remain binding.

## 6. Three-layer architecture

Keep the system conceptually separated:

```
Economic model -> Decision engine -> Presentation layer
```

The economic model produces conditional outcome distributions. The decision engine applies mandates, institutional constraints, policy-control search, Pareto/Nash logic and robustness criteria. The presentation layer explains trade-offs, provenance, uncertainty and model disagreement without changing economic results.

## 7. Robust recommendation contract

The active structural-robustness contract includes:

- selected strategy under the reference calibration;
- structural parameter draw count;
- exact recommendation wins and win rate;
- broader strategy-family wins and win rate;
- mean, P10 and P90 score of the reference control decision;
- calibration and structural-registry identity;
- sampled parameter count and provenance/bounds flags;
- common-random-number status;
- policy-control re-optimization status and search counts;
- reference generated-control retention rate;
- nested sector-reoptimization status;
- cached/built sector-frontier count;
- nested sector optimization and finalist counts;
- number of sector-package changes;
- reference-package retention rate;
- robustness classification.

A dedicated `robustness_to_json()` serializer exposes this contract independently of the legacy full-result JSON surface. Historical backtests have a separate `backtest_to_json()` contract so ex-post diagnostics cannot be confused with forward-looking robustness statistics.

A recommendation is **robust** only when the same control decision remains highly ranked after structural uncertainty, endogenous policy-control search and endogenous sector-package adaptation are all accounted for. Monte Carlo precision alone is insufficient.

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
11. Historical vintage fixture schema, no-look-ahead validation and first source-backed 2022 episode.
12. Native CI regression that deliberately injects future information and requires rejection.

Next:

13. Add additional independently sourced historical episodes, beginning with 2015 and 2020.
14. Surface structural provenance, decision robustness and historical diagnostics in the application/API.
15. Replace provisional structural envelopes with empirical estimates where defensible.
16. Add welfare-weight sensitivity as a separate analysis endpoint.
17. Aggregate historical diagnostics only after enough fixtures exist for meaningful summaries.

## Research interpretation

Canada Policy Studio remains a scenario comparator rather than an official forecast or causal model. V2 is intended to reveal where a recommendation depends on structural assumptions, policy-control adaptation, sector adaptation, historical information constraints and normative choices instead of hiding those dependencies behind additional simulation precision.
