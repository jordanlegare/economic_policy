# Model Robustness V2

Canada Policy Studio distinguishes simulation precision from empirical confidence. More Monte Carlo draws reduce simulation error conditional on a model; they do not validate structural coefficients. V2 therefore places a documented structural-uncertainty layer around the verified policy comparison.

## Current V2 status

The active V2 robustness implementation is a **nested structural sensitivity experiment**:

1. Run the production policy/sector optimization under the reference calibration.
2. Keep the policy controls fixed for each verified strategy.
3. Draw structural macro/transmission parameter sets from the documented structural registry.
4. For every strategy and structural draw, re-optimize the 20-sector negotiation package across the same Pareto-screened finalist schedules used by the production engine.
5. Use 700 common-random-number paths for finalist selection and 2,800 paths for verification of the selected package.
6. Re-rank the sector-reoptimized policy strategies and report how often the reference recommendation remains the winner.

The deterministic sector Pareto screen depends on the observed economy, policy controls, priorities and cooperation envelope, but not on the sampled macro structural coefficients. V2 therefore caches that exact frontier once per policy and re-simulates/re-selects its finalists inside each structural draw. This is mathematically equivalent to rebuilding the same frontier repeatedly while avoiding unnecessary deterministic computation.

`sectorPackagesReoptimized=true` means the reference sector package is no longer held fixed. The robustness contract also reports the number of nested optimizations, finalist simulations, package changes and the reference strategy's sector-package retention rate.

With `uncertainty_scale=0`, nested V2 must recover the production ranking and sector packages exactly. This is an explicit regression target.

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

The principal output is strategy survival frequency across structural calibrations, accompanied by P10/P90 score dispersion and project decision labels:

- `robust`: win rate >= 80%;
- `moderately-robust`: win rate >= 60%;
- `fragile`: win rate >= 40%;
- `unstable`: win rate < 40%.

These are decision labels, not statistical confidence intervals.

## 3. Historical backtesting

The next empirical layer is vintage-based backtesting in which the engine receives only information available at the historical decision date. Candidate episodes include:

- 2015 oil-price shock;
- 2020 pandemic shock;
- 2022 inflation acceleration;
- 2022–2023 tightening cycle;
- Canada–U.S. tariff episodes as dated observations become available.

Backtests are diagnostics, not claims that the simulator should reproduce realized history. They should measure directional accuracy, interval coverage, regime classification, policy-ranking stability and systematic residual patterns. Each fixture must identify its data vintage and prohibit look-ahead calibration.

## 4. Welfare-weight sensitivity

Policy rankings also depend on normative loss-function weights. V2 treats this as a separate experiment, not something the optimizer can alter opportunistically.

For a bounded grid or sampled set of admissible weights, report recommendation retention, materially supported alternatives, switch thresholds, fairness changes and whether hard mandate constraints remain binding.

## 5. Three-layer architecture

Keep the system conceptually separated:

```
Economic model -> Decision engine -> Presentation layer
```

The economic model produces conditional outcome distributions. The decision engine applies mandates, institutional constraints, Pareto/Nash logic and robustness criteria. The presentation layer explains trade-offs, provenance, uncertainty and model disagreement without changing economic results.

## 6. Robust recommendation contract

The active V2 contract includes:

- selected strategy under the reference calibration;
- structural parameter draw count;
- recommendation wins and win rate;
- mean, P10 and P90 reference-strategy score;
- calibration and structural-registry identity;
- sampled parameter count and provenance/bounds flags;
- common-random-number status;
- nested sector-reoptimization status;
- cached sector-frontier count;
- nested sector optimization count;
- nested candidate/finalist counts;
- number of sector-package changes;
- reference-package retention rate;
- robustness classification.

A dedicated `robustness_to_json()` serializer exposes this contract independently of the legacy full-result JSON surface.

A recommendation is **robust** only when it remains highly ranked after both structural uncertainty and endogenous sector-package adaptation are accounted for. Monte Carlo precision alone is insufficient.

## 7. Implementation sequence

Completed or active:

1. `StructuralParameters` type boundary and calibration identity.
2. Deterministic parameter-draw generation.
3. Reproducibility and zero-uncertainty tests.
4. Active parameterized V2 macro re-simulation.
5. Common-random-number structural ranking.
6. Dedicated robustness JSON contract.
7. Typed structural provenance records and explicit sensitivity bounds.
8. Nested sector-package re-optimization inside every structural draw.

Next:

9. Surface the structural registry and nested robustness metrics in the application/API.
10. Add historical vintage fixtures and backtest metrics.
11. Replace provisional envelopes with empirical estimates where defensible.
12. Add welfare-weight sensitivity as a separate analysis endpoint.
13. Add CI gates for no-look-ahead backtests and mandate invariance.

## Research interpretation

Canada Policy Studio remains a scenario comparator rather than an official forecast or causal model. V2 is intended to reveal where a recommendation depends on structural assumptions, sector adaptation and normative choices instead of hiding that dependence behind additional simulation precision.
