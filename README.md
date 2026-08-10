# Canada Policy Studio

A dependency-light C++17 monetary–fiscal scenario engine and professional browser dashboard for testing how U.S. tariffs affect Canada and the United States under a broad menu of monetary, fiscal, trade and supply-side strategies. The opening briefing uses a 50% U.S. tariff and an automatic search of every whole-percentage Canada–U.S. opening allocation, then highlights the highest-scoring outcome for both nations. The dashboard automatically reruns and re-scores every modeled impact when a user changes the tariff, bilateral priorities, risk tolerance, or feasible negotiated-relief ceiling. The server refreshes its economic baseline from public internet sources and labels fallback operation clearly.

The model evaluates macroeconomic, financial, external, housing, fiscal and Canada–US trade indicators. Twelve policy mixes each run through 700 seeded stochastic paths over 12 quarters. A three-party Nash score rewards outcomes that serve the Bank's mandate, Canadian fiscal/household sustainability and US trade-price interests while preventing a weak party from being averaged away.

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
- **Whole-economy sector view:** all 20 two-digit NAICS sectors show comparable Canadian and U.S. output, employment and consumer-price effects, with trade exposure, search and accessible impact bars.
- **Live bilateral negotiation rooms:** persistent header tabs open dedicated Canada/LeBlanc and USA/Greer workspaces where each party can set its headline position and tariff coverage across all 20 sectors. Changes from either room or the joint controls are published through the shared AJAX negotiation endpoint; every open dashboard polls that session, applies newer revisions, reruns the full economy, and refreshes the strategy, headline, risk and all 20 sector metrics while identifying the delegation behind the latest update. Every U.S. headline-tariff change launches the sector search again, and its optimum is atomically applied to both delegations' sliders rather than treating the initial 50% rate as a selected deal. Shared risk and cooperation settings travel with the same revision, so separate browser sessions cannot silently model different agreements.
- **Bounded win-win allocation:** the Canada and U.S. outcome sliders are complementary shares of one agreement. Moving either slider immediately gives the remainder to the other party, so their displayed and modeled weights always total exactly 100%.
- **Continuous bilateral growth guardrail:** every strategy now projects separate Canadian and U.S. GDP-growth paths for all 12 quarters. Any agreement with a non-positive quarter on either side receives a binding ranking penalty, so a qualifying positive-growth agreement is always favored over a one-sided or temporarily contractionary result.
- **Risk block:** recession frequency plus 90th-percentile inflation and federal debt stress outcomes.
- **Decision layer:** separate BoC, Canadian federal/household and US loss functions combined into a preference-weighted Nash score with a fairness floor. Alongside twelve expert strategies, every request autonomously searches 144 monetary, fiscal, productive-investment, relief and diversification combinations for a customized win-win frontier. At startup, the engine compares all 101 whole-percentage bilateral allocations across every displayed strategy, selects the highest fairness-protected score, calibrates the four deal controls, and searches each U.S. sector from 100% coverage down in one-percentage-point increments. The browser applies that output-maximizing sector equilibrium before presenting the first result and reruns the bilateral sector search after every change, atomically moving both rooms to the newly modeled win-win coverage.
- **Internet baseline:** `GET /api/baseline` refreshes available Bank of Canada Valet observations at page load, exposes source metadata, and reports whether a documented fallback baseline is being used.

## Responsible decision use

The defaults are illustrative—not a live tariff schedule. Before each decision round, analysts should replace them with a dated, trade-weighted tariff inventory and documented data vintage, then run sensitivity ranges for pass-through and trade elasticity. The engine is a scenario comparator, not a causal forecasting system; it requires expert judgment, model comparison and governance review.

Useful primary reference points include the [Bank of Canada Monetary Policy Report](https://www.bankofcanada.ca/mpr/), [Statistics Canada Canadian international merchandise trade](https://www.statcan.gc.ca/en/subjects-start/international_trade), [Department of Finance tariff measures](https://www.canada.ca/en/department-finance.html), and [US International Trade Commission tariff data](https://hts.usitc.gov/). Live observations are downloaded automatically where machine-readable feeds are available; every response includes its timestamp, source metadata, and live/fallback state for auditability.
