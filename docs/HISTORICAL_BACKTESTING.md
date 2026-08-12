# Historical vintage backtesting

Historical backtests are diagnostic experiments. They do not claim that Canada Policy Studio is an official forecast model, nor do they permit realized data to leak into the historical decision state.

## Information-set contract

Every fixture has an ISO `decision_date`. Rows are separated into three classes:

- `INPUT`: information the model is allowed to consume. Its `release_date` must be on or before the decision date.
- `BENCHMARK`: an observed policy decision used only for ex-post comparison. Its release date must be after the decision date.
- `OUTCOME`: realized macroeconomic data used only for ex-post forecast diagnostics. Its release date must be after the decision date.

If any input has a future release date, or any benchmark/outcome is already available at the decision date, the entire fixture receives `lookahead-failed` and `run_backtest()` refuses to evaluate it. The framework does not silently drop offending observations.

Every empirical datum must reference a declared `SOURCE`. `backtest_scope` is reserved for explicit model-boundary controls such as disabling the tariff channel in a historical monetary-policy episode; these controls are not represented as observed data.

## Initial fixture: July 12, 2022

`data/backtests/2022-07-12-inflation-tightening.csv` reconstructs a compact information set immediately before the Bank of Canada's July 13, 2022 rate decision.

Inputs available by July 12 include:

- policy rate: 1.5%, from the June 1 Bank of Canada decision;
- headline CPI inflation: 7.7% year over year for May 2022, released June 22;
- core inflation: 4.15%, transparently derived as the midpoint of the Bank's June 1 published 3.2%–5.1% range of core measures rather than equating headline CPI with core inflation;
- first-quarter real GDP growth: 0.8% quarter over quarter, represented as approximately 3.2% annualized for the engine's annual-rate convention, released May 31;
- unemployment rate: 4.9% for June 2022, released July 8;
- average hourly wage growth: 5.2% year over year for June 2022, released July 8.

The bilateral tariff channel is set to zero as an explicit scope control because this fixture is designed to test the macro/monetary-policy path rather than retroactively impose the current tariff scenario on 2022.

The first ex-post policy benchmark is the Bank's 100-basis-point July 13 increase. The 12-quarter realized outcome diagnostics use July 2025 CPI and unemployment plus second-quarter 2025 GDP, all of which were released after the historical forecast horizon.

## Diagnostics

For the selected historical strategy the framework reports:

- recommended first policy move versus the observed policy move;
- policy-direction agreement and basis-point error;
- terminal inflation, GDP growth and unemployment versus realized outcomes;
- signed forecast errors;
- directional accuracy relative to the initial state;
- fixture coverage, provenance completeness and no-look-ahead status.

These diagnostics are descriptive. A single episode is not model validation. Aggregate performance statistics should only be reported after multiple independently sourced historical fixtures exist.

## CI invariants

The native `historical_backtest` test enforces:

1. the shipped fixture parses with complete declared core-input coverage;
2. all inputs are released by the decision cutoff;
3. all benchmarks/outcomes are released after the cutoff;
4. provenance references resolve;
5. the historical evaluation produces finite diagnostic values;
6. a deliberately future-dated input invalidates the fixture and prevents evaluation.

Future fixtures should add episodes without weakening these invariants. Candidate next episodes are the 2015 oil shock, the 2020 pandemic shock and later stages of the 2022–2023 tightening cycle.
