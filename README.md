# Canada Policy Studio

A dependency-light C++17 monetary–fiscal scenario engine and professional browser dashboard for exploring balanced (“win-win”) strategies between the Bank of Canada and the federal government.

The model evaluates macroeconomic, financial, external, housing, fiscal and Canada–US trade indicators. Seven policy mixes each run through 700 seeded stochastic paths over 12 quarters. A three-party Nash score rewards outcomes that serve the Bank's mandate, Canadian fiscal/household sustainability and US trade-price interests while preventing a weak party from being averaged away.

> **Research disclaimer:** This is an illustrative policy-analysis tool, not an official Bank of Canada model, forecast or recommendation. It does not reproduce Bank systems or federal budget projections.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/cad-policy-studio 8080
```

Open <http://localhost:8080>. The server has no runtime framework dependencies and serves the dashboard plus `POST /api/evaluate`.

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Model design

- **Monetary block:** endogenous rate reaction, neutral-rate estimate, inflation expectations and exchange-rate pass-through.
- **Real block:** output gap, labour market, productivity, population and global demand.
- **Financial block:** credit spreads, household leverage and rate-sensitive housing valuation.
- **Fiscal block:** balance and debt dynamics, program growth, fiscal impulse and the supply benefit of productive investment.
- **Trade block:** effective bilateral tariffs, retaliation, border friction, import-price pass-through, export exposure, trade elasticity, US demand and market diversification.
- **Household block:** an explicit cost-of-living pressure index and real-income growth alongside housing and unemployment.
- **Risk block:** recession frequency plus 90th-percentile inflation and federal debt stress outcomes.
- **Decision layer:** separate BoC, Canadian federal/household and US loss functions combined into a three-party Nash score. All assumptions are visible, editable and designed for scenario comparison—not point forecasting.

## Responsible decision use

The defaults are illustrative—not a live tariff schedule. Before each decision round, analysts should replace them with a dated, trade-weighted tariff inventory and documented data vintage, then run sensitivity ranges for pass-through and trade elasticity. The engine is a scenario comparator, not a causal forecasting system; it requires expert judgment, model comparison and governance review.

Useful primary reference points include the [Bank of Canada Monetary Policy Report](https://www.bankofcanada.ca/mpr/), [Statistics Canada Canadian international merchandise trade](https://www.statcan.gc.ca/en/subjects-start/international_trade), [Department of Finance tariff measures](https://www.canada.ca/en/department-finance.html), and [US International Trade Commission tariff data](https://hts.usitc.gov/). No data is downloaded automatically, so runs remain reproducible and auditable.
