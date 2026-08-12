# Historical vintage backtesting

Historical backtests are diagnostic experiments. They do not claim that Canada Policy Studio is an official forecast model, nor do they permit realized macro outcomes to leak into the historical decision state.

## Information-set contract

Every fixture has an ISO `decision_date`. `INPUT` rows must describe economic information dated on or before that date; `BENCHMARK` and `OUTCOME` rows are ex-post diagnostics and must be later. Every empirical datum references a declared `SOURCE`. A future-dated ordinary input invalidates the fixture with `lookahead-failed`.

A small number of explicitly labelled `public-historical-reconstruction` rows use later-open public publications to reconstruct a market state that existed before the decision date. Their fixture date is the historical observation/reference date, not a claim that the later publication itself was available in real time. This distinction is mandatory: such rows are observation-period clean but are **not** certified frozen archival real-time vintages.

## State coverage contract

The backward-compatible declared contract remains 16 fields: eight core policy/macro/scope fields plus eight external/financial/fiscal fields. `inputCoverage`, `extendedInputCoverage`, `stateCoverage` and `stateGrade` retain that meaning.

V2 separately audits all 18 modeled empirical states by adding `housing_gap` and `credit_spread`. Both now have explicit measurement contracts and fixture values, so the three shipped episodes reach **18/18 (100%) modeled empirical-state coverage**.

### Housing affordability pressure

`housing_gap` uses the Bank of Canada Housing Affordability Index:

`housing_gap = 100 * (HAI_t / median(HAI_{t-20} ... HAI_{t-1}) - 1)`

The current quarter is excluded from the prior-20-quarter median. `data/calibration/housing_affordability_benchmarks.csv` records the selected quarter, cutoff and benchmark for each episode. Public historical HAI levels may be revised, so this is information-window clean rather than a frozen archival vintage.

### Corporate credit spread

`credit_spread` is a broad Canadian corporate-minus-government bond yield spread in percentage points, consistent with the Bank of Canada research definition. V2 prefers an official broad-market historical spread when available and otherwise uses a published Canadian investment-grade index spread. Bank lending rates, single-issuer spreads and generic financial-conditions indexes are explicitly disallowed substitutes.

`data/calibration/credit_spread_benchmarks.csv` records the measurement family and reconstruction method. The shipped values are:

- 2015-01-20: **1.26 pp**, the current Canadian investment-grade BAML/ICE BofA index spread reported by Canso's January 2015 newsletter;
- 2020-03-03: **1.50 pp**, Statistics Canada's February 2020 corporate-government yield spread from Chart 7 of its business-debt study;
- 2022-07-12: **1.37 pp**, the 2022Q1-end ICE BofA Canadian investment-grade spread reconstructed from Canso's published quarterly path: Q2 +21 bp, Q3 +22 bp, Q4 opening level 180 bp.

The raw ICE BofA history is not represented as public. The 2020 Statistics Canada table and parts of the 2022 reconstruction were published after the corresponding decision dates. Therefore 18/18 means every model state has a source-backed historical measurement; it does **not** mean all 18 fields have frozen real-time source vintages.

## Shipped historical episodes

### January 20, 2015 — oil-price shock

The fixture reconstructs the information set immediately before the Bank of Canada's January 21 surprise 25-basis-point cut. It now includes both the 2014Q3 HAI pressure gap and a 1.26 pp Canadian investment-grade credit spread.

### March 3, 2020 — pandemic onset

The fixture reconstructs the information set immediately before the Bank's March 4 scheduled 50-basis-point cut. It includes the 2019Q3 HAI pressure gap and the February 2020 official historical corporate-government spread of 1.50 pp.

### July 12, 2022 — inflation tightening

The fixture reconstructs the information set immediately before the Bank's July 13, 2022 100-basis-point increase. It includes the 2022Q1 HAI pressure gap and a 1.37 pp 2022Q1-end Canadian investment-grade credit spread reconstruction.

All three monetary-policy fixtures explicitly set bilateral tariff rates to zero as a scope control rather than retroactively imposing the current tariff scenario on historical episodes.

## Diagnostics

Per-episode diagnostics report recommended versus realized first policy move, terminal inflation/GDP/unemployment forecast errors, directional accuracy, declared-state coverage, provenance completeness and no-look-ahead status. `backtest_suite.hpp` aggregates only valid fixtures and requires at least three valid, provenance-complete episodes before aggregate diagnostics are exposed. This is a reporting guard, not statistical validation.

## CI invariants

The native `historical_backtest` and `state_measurement_contract` tests enforce:

1. all three fixtures remain 100% complete on the declared 16-field contract;
2. all 18 modeled empirical fields are resolved in every shipped fixture;
3. housing values reproduce the one-sided HAI ledger;
4. credit-spread values reproduce the public reconstruction ledger and use an approved broad-market measurement family;
5. both model-specific states actually replace modern engine defaults in historical runs;
6. every historical evaluation remains finite and reproducible;
7. a deliberately future-dated ordinary input still invalidates a fixture.

Future work should improve **vintage quality**—especially archived real-time market feeds—rather than silently changing the economic definition merely to obtain easier data.
