# Historical vintage backtesting

Historical backtests are diagnostic experiments. They do not claim that Canada Policy Studio is an official forecast model, nor do they permit realized data to leak into the historical decision state.

## Information-set contract

Every fixture has an ISO `decision_date`. Rows are separated into three classes:

- `INPUT`: information the model is allowed to consume. Its `release_date` must be on or before the decision date.
- `BENCHMARK`: an observed policy decision used only for ex-post comparison. Its release date must be after the decision date.
- `OUTCOME`: realized macroeconomic data used only for ex-post forecast diagnostics. Its release date must be after the decision date.

If any input has a future release date, or any benchmark/outcome is already available at the decision date, the entire fixture receives `lookahead-failed` and `run_backtest()` refuses to evaluate it. The framework does not silently drop offending observations.

Every empirical datum must reference a declared `SOURCE`. `backtest_scope` is reserved for explicit model-boundary controls such as disabling the tariff channel in a historical monetary-policy episode; these controls are not represented as observed data.

## State coverage contract

Historical state coverage has two explicit tiers.

The backward-compatible **core tier** contains policy rate, headline inflation, core inflation, Canadian GDP growth, unemployment, wage growth and the two bilateral tariff scope controls.

The **expanded tier** contains:

- USD/CAD;
- WTI oil price;
- contemporaneous global-growth outlook;
- latest available U.S. real GDP growth;
- U.S. CPI inflation;
- federal budgetary balance as a share of GDP;
- federal debt as a share of GDP;
- household credit-market debt as a share of disposable income.

`inputCoverage` continues to mean core-tier coverage. `extendedInputCoverage` reports the expanded tier and `stateCoverage` reports both tiers together. `stateGrade=expanded-complete` means every declared field in both tiers is present, source-backed and temporally admissible.

This does **not** mean every member of `Economy` has a historical observable. In particular, `credit_spread` and `housing_gap` remain model abstractions whose exact empirical mappings have not been defined. Backtests intentionally retain the engine defaults for those fields rather than inserting convenient but conceptually different proxies. A future change should define the economic series and transformation first, then add the historical mapping.

Market inputs such as WTI use dated market observations. FX uses Federal Reserve H.10 releases that were publicly available by the decision cutoff. Fiscal inputs use the latest contemporaneous federal fiscal plan; forecast quantities are labelled as forecasts rather than observations.

## Shipped historical episodes

### January 20, 2015 — oil-price shock

`data/backtests/2015-01-20-oil-shock.csv` reconstructs the information set immediately before the Bank of Canada's January 21 surprise 25-basis-point cut.

The core state uses the 1% policy rate, November headline/core inflation, third-quarter Canadian GDP, and December labour-market data. The expanded state adds the January 20 H.10 USD/CAD release, January 16 WTI price, the January 20 IMF global outlook, third-quarter U.S. GDP, December U.S. CPI, the 2014 federal fiscal update and third-quarter household leverage. The ex-post benchmark is the Bank's -25 bp January 21 move. Twelve-quarter diagnostics use January 2018 CPI/unemployment and fourth-quarter 2017 GDP.

### March 3, 2020 — pandemic onset

`data/backtests/2020-03-03-pandemic-onset.csv` reconstructs the information set immediately before the Bank's March 4 scheduled 50-basis-point cut.

The core state uses the 1.75% policy rate, January inflation, fourth-quarter Canadian GDP and January labour data. The expanded state adds the March 2 H.10 USD/CAD release, March 2 WTI price, the Bank's January global-growth outlook, second-estimate fourth-quarter U.S. GDP, January U.S. CPI, the December 2019 Economic and Fiscal Update and third-quarter 2019 household leverage. The Bank described 2020 global growth as “just over 3 percent”; the numeric fixture representation is 3.1% and is labelled `official-forecast-rounded`. The 2019-20 fiscal-balance ratio is an explicit derived approximation using the update's $26.6 billion risk-adjusted deficit and $2,304 billion nominal-GDP level.

### July 12, 2022 — inflation tightening

`data/backtests/2022-07-12-inflation-tightening.csv` reconstructs the information set immediately before the Bank's July 13, 2022 100-basis-point increase.

The core state uses the 1.5% policy rate, May headline CPI, a transparently derived core-inflation midpoint, first-quarter Canadian GDP and June labour data. The expanded state adds the July 11 H.10 release, July 11 WTI price, the Bank's April global outlook, first-quarter U.S. GDP, May U.S. CPI, Budget 2022 fiscal ratios and first-quarter household leverage.

All three monetary-policy fixtures explicitly set bilateral tariff rates to zero as a scope control rather than retroactively imposing the current tariff scenario on historical episodes.

## Per-episode diagnostics

For the selected historical strategy the framework reports:

- recommended first policy move versus the observed policy move;
- policy-direction agreement and basis-point error;
- terminal inflation, GDP growth and unemployment versus realized outcomes;
- signed forecast errors;
- directional accuracy relative to the initial state;
- core, expanded and combined state coverage;
- provenance completeness and no-look-ahead status.

## Cross-episode diagnostics

`backtest_suite.hpp` aggregates only valid backtests. It reports:

- policy-direction accuracy and mean absolute first-move error;
- inflation direction accuracy and mean absolute terminal error;
- GDP-growth direction accuracy and mean absolute terminal error;
- unemployment direction accuracy and mean absolute terminal error;
- valid/no-look-ahead/provenance-complete fixture counts;
- expanded-complete fixture count;
- mean core, expanded and combined state coverage plus minimum combined coverage.

Aggregate diagnostics are exposed only when at least three fixtures are all valid, provenance-complete and free of look-ahead. This three-episode threshold is a reporting guard, **not** a claim of statistical validation or adequate sample size.

## CI invariants

The native `historical_backtest` test enforces:

1. all three shipped fixtures parse with 100% declared core and expanded state coverage;
2. every input is released by its decision cutoff;
3. every benchmark/outcome is released after its cutoff;
4. provenance references resolve;
5. expanded inputs actually replace the corresponding modern `Economy` defaults;
6. `credit_spread` and `housing_gap` remain untouched until explicit mappings are defined;
7. every historical evaluation produces finite diagnostic values;
8. aggregate coverage/count/error outputs are finite and reproducible;
9. a deliberately future-dated input invalidates the fixture and prevents evaluation.

Future fixtures should expand regime diversity without weakening these invariants. High-value additions include a 2022–2023 later-tightening decision, a soft-landing/hold decision, and a tariff-specific episode once a sufficiently mature realized outcome window exists.
