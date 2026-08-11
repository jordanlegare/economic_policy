# Canada–U.S. empirical calibration and provenance pipeline

## Purpose

The negotiation engine distinguishes a mathematically verified optimization from an empirically calibrated representation of the Canadian and U.S. economies. A package can be Pareto-efficient within the model while the underlying economic coefficients remain incomplete or uncertain. This calibration layer makes that distinction explicit and machine-readable.

The platform is a research and negotiation-support system. It does not replace Statistics Canada, U.S. Census, USITC, CBSA, Global Affairs Canada, Department of Finance Canada, BEA, legal counsel, or an official government forecast.

## Evidence classes

Every model input should belong to one of four classes:

1. `observed` — directly reported by an authoritative statistical source.
2. `official-derived` — deterministically transformed from official observations or legal schedules, with the transformation reproducible.
3. `empirically-estimated` — a behavioural parameter estimated from historical observations, with vintage, method and uncertainty retained.
4. `assumption` — a scenario/model parameter that has not passed an empirical calibration gate.

An assumption is never silently relabelled as observed data. Missing sources lower the calibration grade rather than being filled with an undocumented fallback.

## Current authoritative source registry

The machine-readable registry is `data/calibration/source_registry.csv`. Principal sources include:

- Statistics Canada Table 12-10-0172-01: bilateral merchandise trade by principal partner and product group — https://www150.statcan.gc.ca/n1/tbl/csv/12100172-eng.zip
- Statistics Canada Table 12-10-0099-01: monthly merchandise trade by HS section and U.S. geography — https://www150.statcan.gc.ca/n1/tbl/csv/12100099-eng.zip
- Statistics Canada Table 36-10-0001-01: input-output tables, detail level — https://www150.statcan.gc.ca/n1/tbl/csv/36100001-eng.zip
- Statistics Canada Table 36-10-0478-01: supply and use tables, detail level — https://www150.statcan.gc.ca/n1/tbl/csv/36100478-eng.zip
- U.S. Census international trade HS APIs — https://api.census.gov/data/timeseries/intltrade/exports/hs and https://api.census.gov/data/timeseries/intltrade/imports/hs
- USITC HTS 2026 Revision 12 — https://www.usitc.gov/sites/default/files/tata/hts/hts_2026_revision_12_json.json
- CBSA Customs Tariff 2026 — https://www.cbsa-asfc.gc.ca/trade-commerce/tariff-tarif/2026/menu-eng.html
- Department of Finance Canada current counter-tariff product list — https://www.canada.ca/en/department-finance/programs/international-trade-finance-policy/canadas-response-us-tariffs/complete-list-us-products-subject-to-counter-tariffs.html
- CUSMA tariff schedule of Canada — https://www.international.gc.ca/trade-commerce/trade-agreements-accords-commerciaux/agr-acc/cusma-aceum/text-texte/tariff-schedule-liste-canada.aspx?lang=eng
- CUSMA Chapter 4 / Annex 4-B rules of origin — https://www.international.gc.ca/trade-commerce/trade-agreements-accords-commerciaux/agr-acc/cusma-aceum/text-texte/04.aspx?lang=eng
- BEA Input-Output and Industry Economic Accounts — https://apps.bea.gov/api/data

The registry also records legally dated tariff overlays. A measure announced but not yet effective is visible to diplomats but must not alter the applied-rate calibration before its effective date.

## Snapshot contract

`data/calibration/current.snapshot.csv` is the exact input-vintage record used by the application. It contains:

- metadata: snapshot id, as-of date, generation timestamp and schema version;
- parameters with value, unit, evidence class, source id, vintage, uncertainty and whether they are permitted to override the engine;
- source records with agency, dataset, vintage, URL, content hash and status;
- 20 sector records with bilateral trade shares, effective tariff rates, elasticities, pass-through and origin utilization;
- legal tariff measures with announcement/effective dates and status.

The application exposes the same information at `GET /api/calibration` and attaches it to every `/api/evaluate` response and briefing note.

## Bilateral trade calibration

`tools/refresh_calibration.py` downloads the Statistics Canada annual bilateral and all-country trade cubes, parses the selected vintage and hashes the downloaded bytes. It derives:

- Canadian merchandise exports to the United States;
- Canadian merchandise imports from the United States;
- U.S. share of Canadian merchandise exports;
- U.S. share of Canadian merchandise imports.

These official/official-derived values may override the corresponding model defaults.

U.S. Census HS mirror statistics should be added to release snapshots as an independent cross-check. As of 2026 the Census API requires `CENSUS_API_KEY`; a missing credential is reported in provenance rather than bypassed.

## Tariff-line calibration

The optimizer should operate on the economically effective tariff burden, not an undifferentiated headline tariff. The release pipeline accepts a reviewed HS-line file with:

- direction (`ca_exports` or `us_exports`);
- HS code;
- model sector index;
- bilateral trade value;
- total legally applied ad-valorem tariff at the snapshot date;
- origin eligibility;
- preference utilization.

The applied rate must merge, where legally applicable:

1. base MFN/schedule duty;
2. CUSMA/USMCA preferential treatment when origin requirements are satisfied;
3. Chapter 99, Section 232, Section 338 or other U.S. overlays;
4. Canadian surtaxes/counter-tariffs;
5. remissions/exclusions when the data support them;
6. effective dates.

Automatic scraping of a tariff schedule is not treated as legal interpretation. A reviewed HS-line merge is an explicit release input because classification, origin, exclusions, content rules and overlapping legal instruments can materially change the duty actually applicable to a product.

The 20-sector macro engine currently represents tariffs as a maximum rate plus sector coverage. The calibration layer compresses each sector's trade-weighted effective tariff into this representation exactly: `maximum_rate × coverage = sector_effective_rate`. This retains the existing simulator interface while preserving the calibrated sector burden.

## Rules-of-origin utilization

Legal eligibility is not the same as observed preference use. The line calibration therefore distinguishes `origin_eligible` from `origin_preference_used` and computes a trade-weighted utilization rate. Economy-wide utilization should not be inferred from a narrow TPL/TRQ or automotive publication without documenting its scope.

## Input-output, value-added and employment propagation

A release-grade snapshot should map bilateral HS trade into industry/product structures and then into the input-output network. The intended data flow is:

`HS line → reviewed concordance → product/industry → direct trade exposure → domestic value added → intermediate-input exposure → employment/wage exposure → 20 diplomatic model sectors`

Canadian propagation should be calibrated from Statistics Canada input-output/supply-use tables. U.S. propagation should use BEA Input-Output/Industry Economic Accounts. The snapshot does not set `input_output_calibrated=1` until these mappings are complete and reconciliation checks pass.

## Behavioural estimates

Trade elasticities and price pass-through are behavioural parameters, not official observations. `tools/estimate_trade_response.py` provides a transparent first-stage estimator from documented historical tariff episodes and matched/control movements.

For each episode it calculates:

- net log trade response relative to the control;
- positive tariff sensitivity `-Δlog(trade)/Δtariff`;
- net log price response relative to the control;
- price pass-through `Δlog(price)/Δtariff`.

It aggregates estimates by sector and reports standard errors. `--require-all-sectors` requires all 20 sectors and more than one usable episode per sector.

This estimator is deliberately labelled reduced-form. For official-quality inference, episode/control construction should be reviewed by a trade economist and, where data permit, upgraded to a panel/event-study, difference-in-differences, instrumental-variable or structural trade design with documented identifying assumptions.

## Calibration grade

The runtime computes a completeness score:

- official bilateral trade: 25 points;
- reviewed applied tariff-line layer: 25;
- input-output propagation: 20;
- origin utilization: 10;
- empirically estimated trade elasticities: 10;
- empirically estimated price pass-through: 10.

Grades:

- `empirical-calibrated`: at least 95/100;
- `official-partial`: at least 50/100;
- `provenance-only`: a loaded snapshot below 50/100;
- `uncalibrated`: no snapshot loaded.

`certifiedForEmpiricalUse=true` requires at least 95/100. This certification means the declared calibration gates are populated; it does not mean the model is an official forecast, that legal interpretation is final, or that political acceptance has been predicted.

## Release workflow

A recommended calibration release is:

```bash
export CENSUS_API_KEY=...
export BEA_API_KEY=...
python3 tools/estimate_trade_response.py data/private-or-reviewed/historical_episodes.csv \
  --output build/calibration/behavioral_estimates.csv --require-all-sectors
python3 tools/refresh_calibration.py \
  --as-of 2026-08-11 \
  --trade-year 2025 \
  --hs-lines data/private-or-reviewed/hs_line_calibration.csv \
  --behavioral-estimates build/calibration/behavioral_estimates.csv \
  --strict
cmake -S . -B build
cmake --build build --clean-first
ctest --test-dir build --output-on-failure
```

The reviewed line/episode files may contain licensed, sensitive or non-public data and should not be committed unless distribution rights permit it. The resulting snapshot should contain provenance and derived calibration values, not protected source material.

## Current bootstrap status

The committed 2026-08-11 bootstrap is intentionally incomplete. It already replaces the old bilateral merchandise totals/shares with a 2025 Statistics Canada vintage, but sector-level legally applied tariffs, economy-wide origin utilization, input-output propagation and historical behavioural estimates remain uncertified. The application therefore displays the bootstrap as incomplete rather than presenting model defaults as empirical facts.

That is deliberate: the trust contract is designed to fail visibly until the remaining calibration work is actually performed.
