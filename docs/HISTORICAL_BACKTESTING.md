# Historical vintage backtesting

Historical backtests are diagnostic experiments. They do not claim that Canada Policy Studio is an official forecast model, nor do they permit realized data to leak into the historical decision state.

## Information-set contract

Every fixture has an ISO `decision_date`. Rows are separated into three classes:

- `INPUT`: information the model is allowed to consume. Its `release_date` must be on or before the decision date.
- `BENCHMARK`: an observed policy decision used only for ex-post comparison. Its release date must be after the decision date.
- `OUTCOME`: realized macroeconomic data used only for ex-post forecast diagnostics. Its release date must be after the decision date.

If any input has a future release date, or any benchmark/outcome is already available at the decision date, the entire fixture receives `lookahead-failed` and `run_backtest()` refuses to evaluate it. The framework does not silently drop offending observations.

Every empirical datum must reference a declared `SOURCE`. `backtest_scope` is reserved for explicit model-boundary controls such as disabling the tariff channel in a historical monetary-policy episode; these controls are not represented as observed data.

## Shipped historical episodes

### January 20, 2015 — oil-price shock

`data/backtests/2015-01-20-oil-shock.csv` reconstructs the information set immediately before the Bank of Canada's January 21 surprise 25-basis-point cut.

The fixture uses the 1% policy rate from the December 3 decision, November headline CPI of 2.0%, the contemporaneous Bank core CPI measure of 2.1%, third-quarter GDP growth of roughly 2.8% annualized, December unemployment of 6.6%, and 1.88% year-over-year wage growth derived from the contemporaneous Labour Force Survey wage table. The ex-post benchmark is the Bank's -25 bp January 21 move. Twelve-quarter diagnostics use January 2018 CPI/unemployment and fourth-quarter 2017 GDP.

### March 3, 2020 — pandemic onset

`data/backtests/2020-03-03-pandemic-onset.csv` reconstructs the information set immediately before the Bank's March 4 scheduled 50-basis-point cut.

The fixture uses the 1.75% policy rate, January CPI of 2.4%, a 2.0% contemporaneous core-inflation anchor from the Bank's January decision, fourth-quarter 2019 GDP represented at an annualized rate, January unemployment of 5.5%, and 4.24% year-over-year wage growth derived from the January Labour Force Survey wage table. The ex-post policy benchmark is -50 bp. Twelve-quarter diagnostics use February 2023 CPI/unemployment and fourth-quarter 2022 GDP.

### July 12, 2022 — inflation tightening

`data/backtests/2022-07-12-inflation-tightening.csv` reconstructs the information set immediately before the Bank's July 13, 2022 100-basis-point increase.

Inputs include the 1.5% policy rate, May headline CPI of 7.7%, a 4.15% core-inflation input transparently derived as the midpoint of the Bank's June 1 published 3.2%–5.1% core-measure range, first-quarter GDP represented at approximately 3.2% annualized, June unemployment of 4.9%, and June wage growth of 5.2%.

All three monetary-policy fixtures explicitly set bilateral tariff rates to zero as a scope control rather than retroactively imposing the current tariff scenario on historical episodes.

## Per-episode diagnostics

For the selected historical strategy the framework reports:

- recommended first policy move versus the observed policy move;
- policy-direction agreement and basis-point error;
- terminal inflation, GDP growth and unemployment versus realized outcomes;
- signed forecast errors;
- directional accuracy relative to the initial state;
- fixture coverage, provenance completeness and no-look-ahead status.

## Cross-episode diagnostics

`backtest_suite.hpp` aggregates only valid backtests. It reports:

- policy-direction accuracy and mean absolute first-move error;
- inflation direction accuracy and mean absolute terminal error;
- GDP-growth direction accuracy and mean absolute terminal error;
- unemployment direction accuracy and mean absolute terminal error;
- valid/no-look-ahead/provenance-complete fixture counts.

Aggregate diagnostics are exposed only when at least three fixtures are all valid, provenance-complete and free of look-ahead. This three-episode threshold is a reporting guard, **not** a claim of statistical validation or adequate sample size.

## CI invariants

The native `historical_backtest` test enforces:

1. all three shipped fixtures parse with 100% declared core-input coverage;
2. every input is released by its decision cutoff;
3. every benchmark/outcome is released after its cutoff;
4. provenance references resolve;
5. every historical evaluation produces finite diagnostic values;
6. aggregate counts and errors are finite and reproducible;
7. a deliberately future-dated input invalidates the fixture and prevents evaluation.

Future fixtures should expand regime diversity without weakening these invariants. High-value additions include a 2022–2023 later-tightening decision, a soft-landing/hold decision, and a tariff-specific episode once a sufficiently mature realized outcome window exists.
