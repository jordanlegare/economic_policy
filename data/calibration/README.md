# Calibration data interfaces

This directory contains public, distributable calibration metadata and derived snapshots. Do not commit licensed, protected, confidential, or negotiation-sensitive source data here.

## Files

- `source_registry.csv` — authoritative/public source catalogue and model-design provenance references.
- `current.snapshot.csv` — exact derived observed/sector calibration snapshot loaded by the application.
- `structural_parameter_registry.csv` — V2 structural coefficient baselines, classifications, vintages, uncertainty bounds and sampling rules.
- `empirical_structural_evidence.csv` — statistical/official anchors with explicit direct versus reference-only model mappings.
- `quarterly_estimation_panel.csv` / `quarterly_structural_estimates.csv` — frozen real-time SEP estimation layer.
- `realized_calibration_frontier.csv` — realized-data residual/multiplier frontier, including blocked identification requirements rather than silently substituting incompatible estimates.

## V2 structural registry

The structural registry separates model assumptions from observed data. Each coefficient declares a baseline, unit, provenance kind, source ID, vintage, lower/upper sensitivity bounds, distribution and whether it is sampled by the robustness engine.

The initial V2 ranges are deliberately labelled **sensitivity envelopes**, not empirical confidence intervals. `assumed` and provisional `calibrated` entries should be replaced or narrowed when reproducible empirical estimates become available. `mandate` entries such as the inflation target are fixed, and `derived` entries such as the paired inflation-expectations weight are not sampled independently.

`StructuralParameters::uncertainty_scale` is a global multiplier around the declared registry widths. `0` disables structural uncertainty exactly. The reference value `0.10` uses the registry's stated `relative_sigma` values without rescaling.

### Calibration completeness

Structural calibration completeness is intentionally stricter than provenance completeness. The denominator excludes mandate-fixed and algebraically derived parameters, while the numerator counts only production parameters with a direct empirical/official mapping. Reference-only evidence increases the empirical evidence base but does not increase production calibration completeness or overwrite the production coefficient.

The browser reports shock-variance and multiplier coverage separately. A structural registry can therefore be 100% complete as an audit/provenance contract while still being mostly provisional empirically.

## Realized-data residual frontier

Run the deterministic committed-data check with:

```bash
python3 tools/estimate_realized_residuals.py --verify
```

Regenerate the frontier after reviewing a changed realized panel with:

```bash
python3 tools/estimate_realized_residuals.py --refresh
```

The current bootstrap extracts a sanity-screened Statistics Canada quarterly real-GDP volatility estimate for `growth_shock_sd`, but keeps it `reference-only`: the production growth innovation is conditional on output gap, credit spread and coordinated-policy terms that are not yet frozen over the same realized sample. The tool explicitly records the data/identification requirements for the other provisional shock variances and remaining multipliers.

A chained-series break that produces an implausible annualized GDP jump is quarantined rather than counted as a macro innovation. The deterministic verifier requires the committed frontier to reproduce exactly and requires the realized GDP estimate to remain non-promoting until the conditioning-set requirement is satisfied.

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

## U.S. input-output refresh and structural promotion gate

`tools/build_bea_io_matrix.py` is the fail-closed refresh path for the U.S. production-network artifact. It requires `BEA_API_KEY`, discovers an adequate BEA InputOutput direct-requirements/use-table pair, aggregates it to the exact 20 model sectors, and writes a CSV, generated C++ header and provenance record. Until those artifacts are reviewed and committed, the U.S. network remains explicitly non-empirical.

`python3 tools/verify_structural_promotions.py` enforces the evidence boundary for macro/transmission parameters. Any production registry entry labelled `empirical_estimate`, `official_assessment` or `realized_residual_estimate` must have a matching `direct` evidence record from the same source. Reference-only estimates may inform sensitivity/calibrated anchors but cannot be silently promoted.

## Trust rule

The committed bootstrap is intentionally incomplete. `certifiedForEmpiricalUse` must remain false until the required official/estimated layers pass the runtime and CI calibration gates. Structural-registry completeness means the assumptions are auditable; it does **not** mean those assumptions are empirically identified.
