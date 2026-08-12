# Calibration data interfaces

This directory contains public, distributable calibration metadata and derived snapshots. Do not commit licensed, protected, confidential, or negotiation-sensitive source data here.

## Files

- `source_registry.csv` — authoritative/public source catalogue and model-design provenance references.
- `current.snapshot.csv` — exact derived observed/sector calibration snapshot loaded by the application.
- `structural_parameter_registry.csv` — V2 structural coefficient baselines, classifications, vintages, uncertainty bounds and sampling rules.

## V2 structural registry

The structural registry separates model assumptions from observed data. Each coefficient declares a baseline, unit, provenance kind, source ID, vintage, lower/upper sensitivity bounds, distribution and whether it is sampled by the robustness engine.

The initial V2 ranges are deliberately labelled **sensitivity envelopes**, not empirical confidence intervals. `assumed` and provisional `calibrated` entries should be replaced or narrowed when reproducible empirical estimates become available. `mandate` entries such as the inflation target are fixed, and `derived` entries such as the paired inflation-expectations weight are not sampled independently.

`StructuralParameters::uncertainty_scale` is a global multiplier around the declared registry widths. `0` disables structural uncertainty exactly. The reference value `0.10` uses the registry's stated `relative_sigma` values without rescaling.

## Reviewed HS-line input

`tools/refresh_calibration.py --hs-lines <file>` expects:

```csv
direction,hs,sector_index,trade_value,applied_tariff,origin_eligible,origin_preference_used
ca_exports,870323,4,1000000,25.0,1,1
us_exports,040610,0,250000,25.0,1,0
```

`applied_tariff` is the total legally applicable ad-valorem rate for the line at the snapshot date after reviewed treatment of base tariffs, preferential origin qualification, overlays, exclusions/remissions and effective dates. The refresh tool does not infer legal treatment from prose.

## Historical response input

`tools/estimate_trade_response.py` expects:

```csv
sector_index,pre_trade,post_trade,tariff_change_pp,control_trade_change_pct,pre_price,post_price,control_price_change_pct,weight
4,100,80,25,0,100,110,0,1
```

The output contains sector trade elasticity and price pass-through estimates plus standard errors and can be supplied to `refresh_calibration.py --behavioral-estimates`.

## Trust rule

The committed bootstrap is intentionally incomplete. `certifiedForEmpiricalUse` must remain false until the required official/estimated layers pass the runtime and CI calibration gates. Structural-registry completeness means the assumptions are auditable; it does **not** mean those assumptions are empirically identified.
