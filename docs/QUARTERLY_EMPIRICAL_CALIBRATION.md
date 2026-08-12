# Quarterly empirical structural calibration

The V2 empirical layer now contains a frozen **2001Q1–2019Q4** quarterly estimation panel built from Bank of Canada Staff Economic Projections (SEP) real-time vintages. Each SEP vintage is a snapshot of the information set available to staff at the time; the committed panel uses the current-quarter output gap, core CPI index, implied policy rate and U.S./CAD exchange rate from each vintage. Statistics Canada GDP and unemployment series are retained as revised-data diagnostics, not mixed into the real-time structural regressions.

## Production-form estimation

The refresh tool estimates equations in the same normalization used by the production simulator and commits both coefficients and residual covariance. Ten parameters receive direct quarterly evidence. Promotion is intentionally stricter than estimation: a coefficient is allowed to overwrite a production baseline only when the estimand matches, the sign/bounds are admissible, and the diagnostic is statistically defensible.

The current promotion set is deliberately small:

- `output_persistence = 0.8802835399` (SE 0.0540357; 95% bounded interval 0.77437–0.98);
- `inflation_persistence = 0.7371310581` (SE 0.0785239; interval 0.58322–0.89104);
- `inflation_expectations_weight = 1 - inflation_persistence = 0.2628689419` remains derived rather than independently estimated.

The neutral rate remains the previously promoted 2026 Bank assessment midpoint of 2.75%. Together, those give three direct structural mappings. The broader empirical-evidence registry now covers **12 of 25 structural parameters (48%)**, while only **3 of 25 (12%)** are direct production substitutions.

## Rejected promotions are evidence

The quarterly exercise does not force estimates into the model merely to raise a coverage percentage. The real-rate demand coefficient and FX coefficient have the wrong sign/near-zero estimates; the Phillips-curve interval crosses zero; residual shock scales are based on smooth staff current-quarter estimates rather than realized macro shocks; and the fitted policy-rule coefficients describe the staff implied-rate path, not the Governing Council reaction function. These remain `reference-only` and are visible in the evidence ledger.

The estimated output-gap/inflation residual correlation is approximately **-0.00625**, effectively zero in this sample. The covariance is retained and tested, but the Monte Carlo engine keeps its independent innovation ordering rather than adding complexity unsupported by this estimate.

## Reproducibility

`tools/refresh_sep_calibration.py --refresh` rebuilds the panel from public Bank/Statistics Canada sources. Ordinary CI runs `--verify` offline against committed data, so tests do not depend on network availability. Source-family hashes and transformations are recorded in `quarterly_estimation_manifest.csv`.

This is an empirical calibration exercise, not causal identification and not an official Bank of Canada model.
