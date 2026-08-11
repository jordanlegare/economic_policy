# Computational Negotiation Engine

The diplomatic interface now contains a bargaining layer above the macroeconomic scenario engine. Its purpose is to turn scenario payoffs into structured negotiation packages without presenting an optimizer as diplomatic authority.

## Architecture

1. `PolicyEngine` remains the macro payoff generator. It produces Canadian monetary/fiscal outcomes, U.S. outcomes, sector impacts, risk measures, and strategy scores under common stochastic worlds.
2. `analyze_negotiation()` consumes the current `Economy` and the complete `Result` from that macro run.
3. The bargaining layer searches linked concessions cheaply, without re-running the 700-path macro simulation for every diplomatic package.
4. Only packages acceptable to both delegations relative to their outside options are admitted to the bargaining set.
5. Dominated agreements are removed and the surviving Pareto frontier is ranked by gains over reservation values plus an incentive-compatibility screen.

## Endogenous concession search

For every displayed macro strategy the engine searches five linked diplomatic issues at three levels each:

- U.S. tariff relief
- Canadian retaliatory-tariff relief
- border and standards facilitation
- reciprocal procurement access
- North American supply-chain commitments

This creates `14 × 3^5 = 3,402` candidate packages in the normal application configuration. Tariff-relief moves are bounded by the user's negotiated-relief/cooperation ceiling. The other implementation issues remain searchable even at a zero tariff-relief ceiling because governments can negotiate customs, standards, procurement, and supply-chain measures without changing the headline tariff.

The recommended concession pattern is therefore an optimizer result, not a fixed 50/50 split.

## BATNAs and reservation values

A delegation's **BATNA** is its strongest modeled non-cooperative outside option among strategies with no more than 20% negotiated relief and excluding the generated custom/zero-deficit bargaining packages.

Canadian BATNA utility is the geometric mean of the Bank of Canada and Canadian federal/household scores. U.S. BATNA utility is the U.S. score from its strongest non-cooperative strategy.

The **reservation value** is the BATNA plus a small risk-sensitive acceptance margin. A package is individually rational only when both delegations meet or exceed their respective reservation values.

## Separate trade channels

The bargaining layer does not reuse Canada's export result as a proxy for the United States.

- Canadian exports respond to the U.S. tariff, Canadian exposure to the U.S. market, U.S. tariff relief, border facilitation, procurement access, and supply-chain commitments.
- U.S. exports respond separately to Canadian retaliation, U.S. exposure to the Canadian market, Canadian tariff relief, border facilitation, procurement access, and supply-chain commitments.

These channels are deliberately separate so an agreement can improve one country's exports by a different amount—or in a different direction—than the other's.

## Issue linkage

Packages receive explicit linkage value when tariff concessions are combined with implementation measures. Three interaction terms are modeled:

- reciprocal tariff relief
- border facilitation linked to procurement access
- supply-chain commitments linked to tariff relief

This permits packages that trade movement across issues instead of requiring one-for-one concessions on the same instrument.

## Pareto frontier

After the individual-rationality screen, packages are sorted by Canadian utility and filtered so that only packages achieving a new maximum U.S. utility remain. The resulting set is the discrete Pareto frontier of the searched package space: no retained package is dominated by another searched package on both delegations' utilities.

The displayed frontier is ranked using:

- Nash gains over reservation values
- stability score
- the weaker delegation's utility as a small tie-breaker

The UI shows up to twelve frontier packages while reporting the complete frontier size.

## Agreement stability

Stability is a one-shot deviation diagnostic, not a treaty-enforcement model.

For each package, the engine estimates the incentive for either delegation to withdraw its costly commitments after receiving the counterparty's concessions. It subtracts reciprocity value plus a compact implementation/reputation penalty. Positive deviation gains indicate an agreement that would benefit from stronger sequencing, safeguards, verification, snap-back provisions, dispute settlement, or other enforcement architecture.

A package is marked stable when neither modeled unilateral deviation gain exceeds 0.5 utility points.

## Interpretation limits

The negotiation layer is reduced-form and inherits the assumptions and calibration limits of the macro scenario engine. Its package utilities are comparative decision-support scores, not revealed political preferences, legal positions, treaty values, or forecasts of what either government will accept.

Use the engine to identify trade-offs, outside options, potentially efficient bundles, and enforcement questions. Mandates, red lines, legal commitments, classified information, and political authority must remain outside the model unless explicitly governed by an appropriate institutional process.
