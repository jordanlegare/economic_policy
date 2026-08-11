# Model trust contract for diplomatic use

Canada Policy Studio is a research decision-support system. This document defines what the software is allowed to call a **verified win-win** and which claims remain outside the model.

## Certification rule

A bargaining package may be labelled `verifiedWinWin=true` only when all of the following are true:

1. **Independent national trade channels.** Canadian export outcomes and U.S. export outcomes are simulated separately. U.S. welfare is not computed from Canadian export change.
2. **Trade balance is report-only.** The bilateral accounting balance is displayed for negotiating context but is not mechanically closed and receives no welfare bonus.
3. **Mandate weights are fixed inputs.** Canada/U.S. priority weights come from the supplied mandate and are not searched to manufacture a preferred outcome.
4. **Sector schedule is propagated through the macro model.** The selected 20-sector U.S. tariff-coverage vector and 20-sector Canadian retaliation-coverage vector are re-simulated through the stochastic macro model.
5. **Second-stage verification.** Sector-optimized strategies are rechecked with a larger common-random-number Monte Carlo sample than the initial search.
6. **Individual rationality.** Both countries' package utilities clear their modeled reservation values/BATNAs.
7. **Pareto efficiency.** The package is non-dominated within the explicitly searched package set.

The API exposes these checks under `negotiation.trust`. The diplomatic UI must show a warning instead of a certification if any required condition fails.

## Sector optimization

The policy engine no longer optimizes each sector independently and then assumes the 20 separate answers form the best national deal. For each policy strategy it constructs a **global bilateral Pareto frontier** across all twenty sectors.

Each sector contributes a set of joint Canada/U.S. coverage choices. A dynamic program combines those choices sector by sector and removes dominated partial packages. Because sector utilities are additive within this reduced-form layer, Pareto pruning preserves the non-dominated finite-grid solutions while avoiding the impossible brute-force Cartesian product across forty coverage variables.

The current research grid includes 0%, 25%, 50%, 75%, 100%, and the user's current coverage value when it falls between those points. The grid resolution is reported in the API. This means `verified win-win` means **best/non-dominated within the declared finite search design**, not proof of a continuous global optimum.

The strongest frontier schedules are then run back through the stochastic macro engine. This closes the previous gap where sector coverage was merely a display recommendation that had not been propagated into GDP, inflation, employment, trade and country welfare.

## Deal objective

The model uses a hierarchy rather than a single opaque score:

- a bilateral growth floor can be enforced as a hard feasibility constraint;
- the sector search rejects schedules that make either side worse than its starting sector posture;
- the bargaining search rejects packages below either side's reservation value;
- dominated packages are removed;
- the remaining packages are ranked by a generalized Nash gain using the **fixed** Canada/U.S. mandate weights, with smaller secondary terms for fairness and implementation stability.

This makes the mathematical meaning of “win-win” explicit: both sides must gain relative to the modeled outside option, not merely produce a high aggregate score.

## Trade-balance treatment

No trade flow is created or destroyed simply to hit a bilateral zero-deficit target. The model computes bilateral imports, exports, tariff receipts and the resulting accounting balance from the modeled tariff/coverage configuration. The balance may be politically relevant and remains visible, but it is not an economic-welfare target unless a future user explicitly adds it as a separately disclosed mandate constraint.

## Stochastic comparability

Policy alternatives use common random numbers so candidate rankings are not driven by different random shock streams. The sector-selected strategies are then checked with a larger verification sample. The API reports both search and verification draw counts.

This reduces Monte Carlo ranking noise; it does not eliminate model uncertainty.

## Exhaustive search benchmark

`win_win_optimality_tests` constructs a tractable bargaining problem, enumerates every package on the declared five-level linked-issue grid, derives the Pareto frontier using an independent O(n²) dominance test, and confirms that the production optimizer returns the same highest-ranked non-dominated package.

This test is a regression guard for search correctness. It does not validate the economic coefficients themselves.

## What diplomats may say

Appropriate language:

- “Within the model's searched set, this package is non-dominated.”
- “Both modeled parties clear their reservation values.”
- “The selected sector schedule was propagated through the stochastic macro model and rechecked with the reported verification sample.”
- “The model reports separate Canadian and U.S. export channels.”
- “The bilateral trade balance is contextual, not a welfare objective.”

Inappropriate language:

- “This is the objectively best possible agreement.”
- “The U.S. government will accept this package.”
- “These utility values reveal either government's true preferences.”
- “The model proves causal effects with certainty.”
- “A verified win-win is a negotiating mandate, legal position, forecast or commitment.”

## Remaining validation work before institutional reliance

The optimization plumbing can be verified in software, but **economic calibration remains a separate empirical task**. Before operational government use, the reduced-form coefficients should be calibrated and versioned against authoritative bilateral trade data, tariff-line/product data, input-output relationships, rules-of-origin utilization, price pass-through, sector employment/value added, and historical policy episodes. Calibration sources, vintages and transformations should be recorded in the evidence ledger.

A future production-grade model should also propagate parameter uncertainty into package confidence intervals and perform out-of-sample/backtest exercises. Until then, the platform should be treated as structured scenario and bargaining support rather than a forecasting system.
