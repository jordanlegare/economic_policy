# Model trust contract for diplomatic use

Canada Policy Studio is a research decision-support system. This document defines what the software is allowed to call a **verified win-win**, what data are actually observed, and which claims remain outside the model.

It is not an official Government of Canada, Bank of Canada, United States government, CUSMA/USMCA secretariat, or treaty-body model. “Verified” means verified against the declared algorithm, search space, constraints, and model inputs; it does not mean empirically proven, politically accepted, or legally authorized.

## Evidence classes

Every briefing input or output should be treated as one of five classes:

1. **Observed** — fetched from an identified source with an as-of timestamp. In the current research server, only the Bank of Canada Valet policy rate, USD/CAD, and WTI series can be fetched live.
2. **User / mandate input** — supplied by the analyst or delegation, including Canada/U.S. priority weights, risk aversion, cooperation ceiling, bilateral growth floor, tariff assumptions, and starting sector coverage.
3. **Model assumption** — a reduced-form coefficient or default that is neither a live observation nor a mandate, including trade elasticities, sector transmission coefficients, and unfetched macro/trade defaults.
4. **Optimized** — a decision variable selected from an explicitly declared finite search space.
5. **Verified model output** — an optimized package that passed the engine's no-harm, Pareto, feasibility, and stochastic re-simulation checks.

The interface and briefing language should never collapse these categories into a generic claim that “the data say” something.

## Certification rule

A bargaining package may be labelled `verifiedWinWin=true` only when the relevant integrity checks pass:

1. **Independent national trade channels.** Canadian and U.S. export outcomes are simulated separately. U.S. welfare is not computed from Canadian export change.
2. **Trade balance is report-only.** The bilateral accounting balance is displayed for negotiating context but is not mechanically closed and receives no welfare bonus.
3. **Mandate weights are fixed inputs.** Canada/U.S. priority weights come from supplied inputs and are not searched to manufacture a preferred result.
4. **Cooperation is feasible.** Aggregate tariff-rate relief plus sector-coverage relief must remain inside the declared cooperation envelope.
5. **Sector schedule is propagated through the macro model.** The selected 20-sector U.S. coverage vector and 20-sector Canadian retaliation-coverage vector are re-simulated through the stochastic macro model.
6. **Bilateral macro no-harm.** A sector schedule may replace the starting coverage only if modeled Canadian welfare and modeled U.S. welfare are each no worse under the same macro strategy.
7. **Second-stage verification.** The selected sector schedule is checked again against the starting coverage with a larger common-random-number Monte Carlo sample; failure causes automatic fallback to the starting coverage.
8. **Individual rationality.** Both countries' bargaining utilities clear their modeled reservation values/BATNAs.
9. **Pareto efficiency.** The package is non-dominated within the explicitly searched bargaining set.

The API exposes the applicable trust diagnostics in the policy recommendation and under `negotiation.trust`. A failed check is a reason to show a warning, not a reason to hide the diagnostic.

## Cooperation ceiling

The cooperation ceiling is a **total effective tariff-relief envelope**, not a separate allowance for every concession lever.

If aggregate rate relief has already consumed part of that envelope, sector exemptions can use only the residual. For example, a 50% cooperation ceiling cannot become 50% rate relief plus an additional independent 50% sector-coverage reduction. A zero cooperation ceiling permits neither aggregate rate relief nor sector tariff-coverage movement.

This constraint applies to hand-authored scenarios, generated policy candidates, and the sector search.

## Sector optimization

The policy engine does not optimize each sector independently and then assume the twenty answers form the best national deal. For each macro-policy strategy it constructs a **global bilateral Pareto frontier** across all twenty sectors.

Each sector contributes a set of joint Canada/U.S. coverage choices. A dynamic program combines those choices sector by sector and removes dominated partial packages. Because sector utility is additive within this reduced-form layer, Pareto pruning preserves the non-dominated solutions on the declared finite grid without attempting an impossible brute-force Cartesian product across forty coverage variables.

The current research grid uses five points—0%, 25%, 50%, 75%, and 100%—of the **remaining relief permitted by the cooperation envelope**. `sectorGridStep=25` therefore refers to a fraction of the permitted relief range, not necessarily 25 tariff-coverage percentage points.

Directional national weights are also separate: U.S. tariff coverage is weighted by Canadian sector export exposure, while Canadian retaliation is weighted by U.S. sales/import exposure into Canada. A small sector therefore cannot automatically receive the same national importance as a large traded sector simply because each sector spans its own local utility range.

## Macro no-harm verification

A sector package is not accepted merely because its reduced-form sector utility improves.

For every macro strategy:

1. The starting sector coverage is simulated.
2. The global sector Pareto search rejects schedules that make either side worse in aggregate sector utility.
3. Up to eight leading non-dominated sector schedules are re-simulated through the full macro model using 700 common-random-number draws.
4. A candidate may replace the starting coverage only if full modeled Canadian welfare and full modeled U.S. welfare are both at least as high as under the starting coverage for that same strategy, and the declared bilateral growth floor is respected.
5. Every resulting strategy is then re-simulated using 2,800 common-random-number draws.
6. At that second stage, the selected coverage is compared again against the same strategy under starting coverage. If either country is worse off, the engine automatically falls back to the starting coverage before final strategy ranking.

This reduces the risk of calling a sector package “win-win” because of a reduced-form proxy or a favorable first Monte Carlo sample. It does not eliminate structural parameter uncertainty.

## Deal objective

The model uses a hierarchy rather than an opaque single target:

- mandate and cooperation limits are constraints/inputs, not optimized preferences;
- the bilateral growth floor can operate as a hard feasibility condition;
- sector schedules must pass sector and full-macro no-harm checks;
- bargaining packages below either side's reservation value are rejected;
- dominated packages are removed;
- remaining bargaining packages are ranked by generalized Nash surplus using the **fixed** Canada/U.S. mandate weights, with smaller secondary terms for fairness and implementation stability.

Bilateral trade balance, tariff revenue, and the accounting gap remain visible but do not directly increase the welfare score.

## Bargaining verification

The bargaining layer operates only after macro/sector verification. It searches five levels on each of five linked bargaining dimensions:

- U.S. tariff relief;
- Canadian retaliatory-tariff relief;
- border and standards facilitation;
- reciprocal procurement access;
- North American supply-chain commitments.

That is `5^5 = 3,125` linked terms per macro scenario. Packages below either side's reservation value are rejected and the survivors are Pareto-pruned.

`win_win_optimality_tests` independently enumerates the full declared bargaining grid, constructs the Pareto frontier using a separate O(n²) dominance check, and verifies that the production optimizer returns the same highest-ranked package. This proves search correctness on that finite bargaining grid; it does not validate the economic coefficients themselves.

## Trade-balance treatment

No trade flow is created or destroyed simply to hit a bilateral zero-deficit target. The model computes bilateral imports, exports, tariff receipts, and the resulting accounting balance from modeled tariffs and coverage. The balance may be politically relevant and remains visible, but it is not an economic-welfare target unless a future mandate explicitly introduces it as a separately disclosed constraint.

## Baseline provenance

`GET /api/baseline` explicitly separates live observations from assumptions.

Current live-capable fields are:

- Bank of Canada policy rate — Valet `V39079`;
- USD/CAD — Valet `FXUSDCAD`;
- WTI oil — Valet `WTI`.

All other macro, fiscal, U.S., tariff, bilateral trade, and elasticity fields are model defaults/user-editable assumptions unless a future authoritative ingestion pipeline actually supplies them. Statistics Canada, Department of Finance Canada, Global Affairs Canada, U.S. Census Bureau, BEA, BLS, USITC, CBSA, and official tariff/treaty sources are calibration/reference targets until their data are ingested, timestamped, versioned, and provenance-linked in the running system.

## What `verifiedWinWin` does not mean

A certification does **not** mean:

- the agreement is globally optimal across every continuous tariff rate or every HTS/HS tariff line;
- the reduced-form sector coefficients are official or fully calibrated;
- either government will accept the package;
- the package is legally authorized or treaty compliant without legal review;
- every province, state, worker, consumer, firm, or sector is better off;
- the model is a forecast with statistically estimated confidence intervals; or
- all displayed baseline data are current government observations.

## Appropriate diplomatic language

Use language such as:

- “Within the model's declared search grid, this package is non-dominated.”
- “Both modeled parties clear their reservation values.”
- “The selected sector schedule passed the bilateral macro no-harm check and the larger second-stage recheck.”
- “Under these assumptions, the model estimates…”
- “The model reports separate Canadian and U.S. export channels.”
- “The bilateral trade balance is contextual, not a welfare objective.”

Avoid statements such as:

- “This is the objectively best possible agreement.”
- “The United States will accept this package.”
- “These utilities reveal either government's true preferences.”
- “The model proves the causal effect.”
- “A verified win-win is a negotiating mandate, legal position, forecast, or commitment.”

## Remaining work before institutional quantitative reliance

The optimization plumbing can be tested in software; **economic calibration is a separate empirical task**. Before official quantitative reliance, the reduced-form coefficients should be calibrated and versioned against authoritative bilateral trade and tariff-line/product data, input-output relationships, rules-of-origin utilization, price pass-through, sector employment/value added, investment/capacity data, and historical policy episodes.

A production data architecture should also record data vintages, transformations, model/version hashes, and uncertainty ranges with every decision snapshot, then propagate parameter uncertainty into country utility, BATNA, reservation-value, and package comparisons. Until then, the platform should be treated as structured scenario and bargaining support rather than a forecasting or mandate system.
