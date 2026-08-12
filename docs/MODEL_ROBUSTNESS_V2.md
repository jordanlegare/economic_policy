# Model Robustness V2

Canada Policy Studio distinguishes simulation precision from empirical confidence. More Monte Carlo draws reduce simulation error conditional on a model; they do not validate structural coefficients. V2 therefore adds a separate structural-uncertainty layer around the verified policy comparison.

## Current V2 status

The first active V2 robustness implementation is now defined as a **conditional structural sensitivity experiment**:

1. Run the existing verified policy/sector optimization under the reference calibration.
2. Freeze each resulting verified policy package, including its negotiated sector coverage.
3. Draw structural macro/transmission parameter sets from the documented `StructuralParameters` uncertainty model.
4. Re-simulate every verified package under every structural draw using 2,800 paths.
5. Use the same macro innovation sequence for every policy and every structural calibration (common random numbers).
6. Re-rank the verified packages under each draw and report how often the reference recommendation remains the winner.

This makes the V2 win frequency genuine structural sensitivity rather than repeated Monte Carlo noise. It is intentionally **conditional on the already-optimized sector packages**. Sector packages are not yet re-optimized inside every parameter draw; the result contract exposes `sectorPackagesReoptimized=false` so downstream users cannot mistake this stage for a full nested optimization.

With `uncertainty_scale=0`, the V2 simulator uses the reference coefficients, 2,800 verification draws, the verified sector packages and the original engine seed. The reference ranking is therefore an explicit regression target.

## 1. Separate the model inputs

The current `Economy` object is retained for API compatibility, but research work classifies inputs into five conceptual layers:

- **ObservedState** — dated macroeconomic and trade observations such as inflation, unemployment, policy rates, exchange rates and bilateral trade values.
- **StructuralParameters** — estimated or calibrated elasticities, pass-through coefficients, transmission coefficients and stochastic-process parameters.
- **PolicyControls** — monetary, fiscal, relief, diversification and tariff-policy choices available to the scenario engine.
- **NegotiationPreferences** — Canada/U.S. priorities, risk tolerance, cooperation constraints and bilateral growth floors. These are mandates, not estimated economic parameters.
- **CalibrationMetadata** — source, vintage, estimation method, uncertainty interval and fallback status for every non-policy quantity.

Every calibrated coefficient should eventually be traceable to a provenance record answering: observed, estimated, calibrated, assumed, or chosen?

## 2. Parameter uncertainty

V2 samples the structural parameters separately from the stochastic macro innovations. The same structural-draw seed reproduces the same parameter ensemble, while common random numbers hold path innovations fixed across calibrations and policy alternatives.

The active parameter set includes:

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

Positive coefficients use positive multiplicative draws; bounded coefficients use bounded draws. Inflation persistence and the expectations weight are sampled in composition while preserving their reference total anchor, preventing the uncertainty sampler from accidentally creating an explosive inflation process solely because two weights were varied independently.

The principal output is strategy survival frequency across structural calibrations, accompanied by P10/P90 score dispersion and an explicit classification:

- `robust`: win rate >= 80%;
- `moderately-robust`: win rate >= 60%;
- `fragile`: win rate >= 40%;
- `unstable`: win rate < 40%.

These are project decision labels, not statistical confidence intervals.

## 3. Historical backtesting

Add vintage-based exercises in which the engine receives only information that would have been available at the historical decision date. Suggested episodes:

- 2015 oil-price shock;
- 2020 pandemic shock;
- 2022 inflation acceleration;
- 2022–2023 tightening cycle;
- current Canada–U.S. tariff scenarios as data become available.

Backtests are diagnostic, not claims that the simulator should reproduce realized history. They should measure directional accuracy, interval coverage, regime classification, policy ranking stability and systematic residual patterns.

Each backtest must identify its data vintage and must not use future observations in calibration.

## 4. Welfare-weight sensitivity

Policy rankings depend partly on normative loss-function weights. V2 treats these explicitly as a separate sensitivity dimension.

For a bounded grid or sampled set of admissible welfare weights, report:

- percentage of configurations retaining the baseline recommendation;
- strategies receiving material support;
- thresholds at which the recommendation changes;
- changes in the minimum-party/fairness score;
- whether mandate constraints remain binding.

Mandate weights supplied by users remain fixed during any single optimization. Sensitivity analysis is a separate experiment and must never allow the optimizer to choose a more convenient mandate.

## 5. Three-layer architecture

Keep the system conceptually separated:

```
Economic model -> Decision engine -> Presentation layer
```

The economic model produces conditional outcome distributions. The decision engine applies mandates, institutional constraints, Pareto/Nash logic and robustness criteria. The presentation layer explains trade-offs, provenance, uncertainty and model disagreement without changing economic results.

This separation permits future model ensembles (for example, the current structural simulator plus reduced-form or externally calibrated alternatives) without rewriting the decision layer.

## 6. Robust recommendation contract

The active V2 contract includes:

- selected strategy under the reference calibration;
- number of structural parameter draws;
- number and share of draws retaining the selected strategy;
- mean, P10 and P90 score of the reference strategy across draws;
- calibration ID and vintage;
- robustness classification;
- whether structural parameters were active;
- whether common random numbers were used;
- whether sector packages were re-optimized.

A dedicated `robustness_to_json()` serializer exposes this contract independently of the legacy full-result JSON surface.

A recommendation is **robust** only when it remains highly ranked across a documented uncertainty set. Monte Carlo precision alone is insufficient.

## 7. Implementation sequence

Completed or active:

1. `StructuralParameters` type boundary and calibration identity.
2. Deterministic parameter-draw generation.
3. Reproducibility and zero-uncertainty tests.
4. Active parameterized V2 re-simulation of verified policy packages.
5. Common-random-number structural ranking and robustness classification.
6. Dedicated robustness JSON contract.

Next:

7. Add typed provenance records and uncertainty bounds to the calibration registry.
8. Re-optimize sector packages inside structural draws for a full nested robustness run.
9. Add historical vintage fixtures and backtest metrics.
10. Add welfare-weight sensitivity as a separate analysis endpoint.
11. Surface robustness/provenance in the browser dashboard.
12. Add CI gates for no-look-ahead backtests, calibration provenance completeness and mandate invariance.

## Research interpretation

Canada Policy Studio remains a scenario comparator rather than an official forecast or causal model. V2 is intended to make uncertainty more honest and auditable: the system should reveal where a recommendation depends on structural assumptions instead of hiding that dependence behind additional simulation precision.
