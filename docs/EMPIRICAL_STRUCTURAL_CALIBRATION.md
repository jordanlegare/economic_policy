# Empirical structural calibration

V2 distinguishes **statistical evidence coverage** from **direct parameter substitution**. A published estimate counts as statistically anchored when it measures the same economic channel, but it is applied to the production equation only when units, state definition and timing match one-for-one.

`data/calibration/empirical_structural_evidence.csv` currently anchors 7 of the 25 structural parameters (28%) to public Bank of Canada statistical/model evidence:

- nominal neutral rate;
- monetary-policy response to inflation;
- monetary-policy response to the output gap;
- inflation persistence;
- inflation volatility benchmark for the innovation scale;
- exchange-rate pass-through;
- tariff/import-price pass-through.

Only the neutral-rate estimate is currently classified `direct`. The Bank's 2026 assessment maps exactly to the model concept, so the active structural baseline is 2.75%, with the published 2.25%-3.25% range used as its sensitivity bounds.

The other six estimates are `reference-only`. They are deliberately **not** substituted yet because their published statistical objects differ from the current production equations. Examples include a targeted Taylor rule that separates demand- and supply-driven inflation, annual PCE persistence versus the model's quarterly inflation state, and retail-price pass-through versus the model's aggregate import-price incidence term.

This prevents a higher empirical-coverage percentage from being achieved by silently mixing incompatible units or estimands.

## Next conversion step

The next tranche should build a frozen quarterly Canadian estimation panel from Bank of Canada Valet and Statistics Canada WDS data, estimate production-equation-compatible coefficients and residual covariance, then promote reference-only entries to `direct` only when equation-level mapping tests pass. The structural sampler should ultimately draw from the estimated joint covariance matrix rather than independent hand-set relative sigmas.
