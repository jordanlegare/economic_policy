# Model Robustness V2

Canada Policy Studio should distinguish simulation precision from empirical confidence. More Monte Carlo draws reduce simulation error conditional on a model; they do not validate structural coefficients. This document defines the next research architecture.

## 1. Separate the model inputs

The current `Economy` object is retained for API compatibility, but new research work should classify each input into five conceptual layers:

- **ObservedState** — dated macroeconomic and trade observations such as inflation, unemployment, policy rates, exchange rates and bilateral trade values.
- **StructuralParameters** — estimated or calibrated elasticities, pass-through coefficients, transmission coefficients and sector sensitivities.
- **PolicyControls** — monetary, fiscal, relief, diversification and tariff-policy choices available to the scenario engine.
- **NegotiationPreferences** — Canada/U.S. priorities, risk tolerance, cooperation constraints and bilateral growth floors. These are mandates, not estimated economic parameters.
- **CalibrationMetadata** — source, vintage, estimation method, uncertainty interval and fallback status for every non-policy quantity.

Every calibrated coefficient should eventually be traceable to a provenance record answering: observed, estimated, calibrated, assumed, or chosen?

## 2. Parameter uncertainty

The existing stochastic paths primarily represent macro shocks conditional on fixed structural assumptions. V2 adds an outer parameter-uncertainty experiment:

1. Draw a plausible structural parameter set from documented distributions or bounded sensitivity ranges.
2. Run the existing common-random-number Monte Carlo policy comparison under that parameter set.
3. Record the winning strategy and key outcome distribution.
4. Repeat across parameter draws.
5. Report strategy win frequency, feasibility frequency, score dispersion and recommendation instability.

The principal output should therefore be statements such as `strategy X wins in 71% of admissible calibrations`, rather than relying on a single high-precision score.

### Initial uncertain parameters

Prioritize parameters with strong influence and weak identification:

- bilateral trade elasticity;
- tariff/import-price pass-through;
- fiscal demand multiplier;
- productive-investment supply effect;
- interest-rate demand sensitivity;
- inflation/output-gap sensitivity;
- diversification effectiveness;
- sector trade/import/cyclical loadings.

Ranges must be documented in the calibration registry rather than embedded silently in source code.

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

Policy rankings depend partly on normative loss-function weights. V2 treats these explicitly as sensitivity dimensions.

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

A production recommendation should eventually include:

- selected strategy under the reference calibration;
- probability/frequency that it wins across admissible parameter draws;
- probability that it satisfies all hard constraints;
- nearest competing strategies and score gaps;
- sensitivity to welfare weights;
- historical backtest diagnostics;
- data vintage and fallback state;
- structural parameters contributing most to recommendation variance;
- explicit `robust`, `fragile`, or `indeterminate` classification.

A recommendation is **robust** only when it remains feasible and highly ranked across a documented uncertainty set. Monte Carlo precision alone is insufficient.

## 7. Implementation sequence

1. Add typed provenance records and uncertainty bounds to the calibration pipeline.
2. Move hard-coded structural coefficients into a documented parameter object without changing baseline outputs.
3. Add deterministic parameter-draw generation and tests for reproducibility.
4. Add an outer robustness evaluator around `PolicyEngine::evaluate`.
5. Add historical vintage fixtures and backtest metrics.
6. Add welfare-weight sensitivity as a separate analysis endpoint.
7. Surface robustness and provenance in JSON and the browser dashboard.
8. Add CI tests ensuring baseline compatibility, no look-ahead in historical fixtures, deterministic seeded robustness runs, and mandate invariance.

## Research interpretation

Canada Policy Studio remains a scenario comparator rather than an official forecast or causal model. V2 is intended to make uncertainty more honest and auditable: the system should reveal where a recommendation depends on structural assumptions instead of hiding that dependence behind additional simulation precision.
