# Canada Policy Studio

A dependency-light C++17 monetary–fiscal scenario engine and professional browser dashboard for exploring balanced (“win-win”) strategies between the Bank of Canada and the federal government.

The model evaluates 21 macroeconomic, financial, external, housing and fiscal indicators. Five policy mixes each run through 700 seeded stochastic paths over 12 quarters. A Nash-style joint score rewards outcomes that serve both price-stability and federal sustainability objectives while penalizing one-sided strategies.

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
- **Decision layer:** separate BoC and federal loss functions combined into a balanced joint score. All assumptions are visible, editable and designed for scenario comparison—not point forecasting.
