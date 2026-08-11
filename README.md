# Canada Policy Studio

A dependency-light C++17 monetary–fiscal scenario engine and professional browser dashboard for testing how U.S. tariffs affect Canada and the United States under a broad menu of monetary, fiscal, trade and supply-side strategies. The dashboard reruns and re-scores modeled impacts when a user changes the tariff, bilateral priorities, risk tolerance, negotiated-relief ceiling or sector coverage. The server can refresh its economic baseline from public internet sources and labels fallback operation clearly.

The current engine evaluates 14 displayed scenarios, including generated policy candidates, through 700 seeded stochastic paths over 12 quarters. A three-party decision score rewards outcomes that serve the Bank's mandate, Canadian fiscal/household sustainability and U.S. trade-price interests while using a fairness floor so a weak party cannot simply be averaged away. Final sector packages can be re-simulated at 2,800 draws for verification.

> **Research disclaimer:** This is an illustrative policy-analysis tool, not an official Bank of Canada model, forecast or recommendation. It does not reproduce Bank systems or federal budget projections. Monte Carlo precision is conditional on the model and does not establish empirical validity of structural coefficients.

## Build and run

```bash
cmake -S . -B build
cmake --build build --parallel
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
- **Tariff fiscal ledger:** U.S. and Canadian customs revenue in USD and CAD from effective sector-weighted tariff rates and an elasticity-adjusted bilateral goods base. Bilateral trade balances are reported outcomes, not welfare targets.
- **Trade block:** effective bilateral tariffs, retaliation, border friction, import-price pass-through, export exposure, trade elasticity, U.S. demand and market diversification.
- **Household block:** cost-of-living pressure and real-income growth alongside housing and unemployment.
- **Whole-economy sector view:** all 20 two-digit NAICS sectors expose Canadian and U.S. output, employment and consumer-price effects.
- **Directional bilateral channels:** U.S. tariff coverage is weighted by Canadian sectoral export exposure, while Canadian retaliation is weighted by U.S. sales into Canada. U.S. export welfare does not reuse Canadian export accounting.
- **Sector negotiation search:** an exact Pareto dynamic program searches permitted sector-coverage choices at 25% increments of each side's residual relief envelope. Packages that make either party worse than its baseline are rejected; top frontier packages are stochastic re-simulated before recommendation.
- **Cooperation envelope:** negotiated headline-rate relief and sector-coverage relief share one bounded cooperation envelope, preventing independent concession controls from exceeding the configured ceiling.
- **Bilateral growth constraint:** every strategy projects separate Canadian and U.S. GDP-growth paths for 12 quarters and can be rejected when either side falls below the configured minimum bilateral growth floor.
- **Risk block:** recession frequency plus 90th-percentile inflation and federal-debt stress outcomes.
- **Decision layer:** separate BoC, Canadian federal/household and U.S. loss functions are combined through preference-weighted Nash welfare, a fairness floor and tail-risk penalties. User-supplied Canada/U.S. mandate priorities remain fixed during optimization.
- **Generated policy search:** each request evaluates a 288-combination policy search in addition to displayed expert strategies, covering monetary, fiscal, productive-investment, negotiated-relief and diversification choices.
- **Trade-balance reporting:** the engine never manufactures exports or imports to force a zero bilateral deficit. Balance, gap and progress are accounting diagnostics only.
- **Internet baseline:** `GET /api/baseline` refreshes available Bank of Canada Valet observations at page load, exposes source metadata, and reports whether a documented fallback baseline is being used.

## Model robustness roadmap

The next research phase explicitly separates observed state, structural parameters, policy controls, negotiation preferences and calibration provenance. It adds parameter uncertainty, vintage-based historical backtesting and welfare-weight sensitivity so recommendation stability can be reported rather than inferred from Monte Carlo precision alone.

See [`docs/MODEL_ROBUSTNESS_V2.md`](docs/MODEL_ROBUSTNESS_V2.md) for the implementation contract and sequencing.

## Responsible decision use

The defaults are illustrative—not a live tariff schedule. Before each decision round, analysts should replace them with a dated, trade-weighted tariff inventory and documented data vintage, then run sensitivity ranges for pass-through, trade elasticity and other weakly identified structural parameters. The engine is a scenario comparator, not a causal forecasting system; it requires expert judgment, model comparison and governance review.

Useful primary reference points include the [Bank of Canada Monetary Policy Report](https://www.bankofcanada.ca/mpr/), [Statistics Canada Canadian international merchandise trade](https://www.statcan.gc.ca/en/subjects-start/international_trade), [Department of Finance tariff measures](https://www.canada.ca/en/department-finance.html), and [US International Trade Commission tariff data](https://hts.usitc.gov/). Live observations are downloaded automatically where machine-readable feeds are available; every response includes its timestamp, source metadata, and live/fallback state for auditability.
