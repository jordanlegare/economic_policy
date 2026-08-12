# Empirical structural calibration

V2 separates empirical evidence from model normalization. A parameter is counted as **direct** only when the published quantity maps to the simulator in the same units and interpretation. Evidence that constrains a coefficient but requires a horizon, unit or aggregation conversion is **indirect** and remains explicitly labelled.

The current structural registry contains 25 parameters. The 2% inflation target is a mandate and the expectations weight is derived, leaving 23 empirically estimable parameters. `data/calibration/empirical_anchor_registry.csv` currently anchors 7 of those 23 to Canadian evidence, an evidence-anchor rate of 30.4%. Only the neutral rate is presently a direct same-units anchor; the other six are empirical constraints rather than claimed one-for-one coefficient estimates.

The neutral-rate baseline is now 2.75%, the midpoint of the Bank of Canada's 2026 nominal neutral-rate assessment range of 2.25%–3.25%. The published range is treated as an assessment range, not a statistical confidence interval.

Other current empirical anchors cover monetary-policy reaction-function evidence, inflation persistence, the output-inflation trade-off, exchange-rate pass-through and tariff/import-price pass-through. Their model coefficients retain explicit normalization caveats.

CI audits the denominator, evidence classes, 7/23 coverage, exact neutral-rate mapping and JSON evidence-status contract. The audit must not classify mandate or derived parameters as estimable evidence wins.

## Next statistical layer

The next calibration milestone is a reproducible quarterly panel built from public Bank of Canada and Statistics Canada series. It should estimate directly mappable coefficients with time-series regressions, calibrate stochastic innovation scales from residuals, retain parameter standard errors and covariance, and draw correlated structural parameter vectors rather than independent perturbations. Historical/vintage splits must be used for evaluation so the estimation stage does not leak future observations into backtests.

The 30.4% figure therefore means **evidence anchored**, not “30.4% of the model statistically identified.” Direct statistical identification remains intentionally reported separately.
