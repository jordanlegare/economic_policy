# Calibration data interfaces

This directory contains public, distributable calibration metadata and derived snapshots. Do not commit licensed, protected, confidential, or negotiation-sensitive source data here.

## Files

- `source_registry.csv` — authoritative/public source catalogue and refresh notes.
- `current.snapshot.csv` — exact derived calibration snapshot loaded by the application.

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

The committed bootstrap is intentionally incomplete. `certifiedForEmpiricalUse` must remain false until the required official/estimated layers pass the runtime and CI calibration gates.
