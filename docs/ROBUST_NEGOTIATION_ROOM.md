# Robust recommendation engine and Diplomat Room

## Purpose

This layer answers two different operational questions:

1. **How sensitive is a recommended package to uncertainty in the model?**
2. **Given the current round, mandate, red lines and concession history, what should the delegation do next?**

It does not estimate political acceptance. All reported probabilities are conditional model probabilities under declared parameter distributions.

## Second-stage uncertainty analysis

After the policy engine and bargaining engine produce the Pareto frontier, `analyze_robust_recommendations` re-evaluates every retained package under a common set of second-stage uncertainty draws. The default is 5,000 draws with common random numbers across packages.

The uncertainty set currently includes:

- trade elasticity;
- border friction;
- price pass-through;
- Canadian growth shock;
- U.S. growth shock;
- Canadian inflation shock;
- U.S. inflation shock;
- outside-option/reservation-margin uncertainty.

Where the calibration snapshot contains an empirical standard error, the robust layer uses it. Where it does not, the distribution is explicitly marked `assumption` or `model-uncertainty` and the analysis grade remains `model-risk-provisional`.

For each package the engine reports:

- mean and median Canada/U.S. surplus above reservation;
- 95% model interval for each country's surplus;
- lower-tail CVaR at 10%;
- probability Canada clears its reservation value;
- probability the United States clears its reservation value;
- probability both clear simultaneously;
- probability the package is the best realized package in a draw;
- mean, 95th-percentile and maximum regret relative to the best package in each draw.

## Robust selection rule

The robust package is chosen lexicographically:

1. prefer packages that meet the joint reservation-clearance probability gate;
2. minimize maximum regret;
3. maximize joint-clearance probability;
4. maximize the weaker country's CVaR surplus;
5. use rank-win probability as a final tie-break.

The probability gate rises with the user's risk-aversion setting from 65% to 90%.

This is deliberately more conservative than ranking by expected score alone.

## Meaning of CVaR

`canadaCvar10Surplus` and `usCvar10Surplus` are the average modeled surplus in the worst 10% of second-stage draws. Positive CVaR therefore means the country remains above its modeled reservation value even on average in the lower tail. Negative CVaR is a warning that the package has meaningful downside exposure.

## Meaning of regret

For each uncertainty draw, the engine computes the decision value of every retained package and identifies the best realized package. A package's regret is the difference between that draw's best value and the value of the package being assessed.

`maxRegret` is the largest observed regret across the declared second-stage draw set. Minimax regret therefore asks which package leaves the delegation least exposed to having chosen badly if the uncertain parameters turn out unfavourably.

## Diplomat Room

`NegotiationRoom` is a persistent negotiation workflow built around an append-only local event log. The default log is:

`runtime/negotiation-room.events`

The `runtime/` directory is ignored by Git.

The room tracks:

- round and negotiation phase;
- issue-level authority classes;
- maximum authorized Canadian moves;
- minimum required U.S. reciprocity;
- hard red lines;
- Canada and U.S. offer history;
- concession magnitude and estimated value/cost;
- whether concessions are reciprocal and conditional;
- custom `If they ask for X -> respond with Y` playbooks;
- post-round debriefs, signals, unresolved questions and next actions.

New offer records must reference a package in the current bargaining frontier. Historical offers are replayed on startup without requiring the old frontier to still exist; when displayed, they are re-scored against the latest robust analysis where a matching package ID remains available.

## Mandate authority

Each issue has an authority class:

- `delegation_discretion`;
- `senior_approval_required`;
- `ministerial`;
- `legal_constraint`;
- `non_negotiable`.

A package that exceeds a non-hard mandate limit is not silently recommended as executable; it is marked as requiring escalation. A package that violates a hard red line is excluded from room counteroffer suggestions.

## Counteroffer categories

The room generates distinct next-move candidates from the robust frontier:

- **Robust default** — the minimax-regret package after the reservation probability gate;
- **Hold firm** — strongest Canadian lower-tail surplus inside the recorded mandate;
- **Bridge** — highest joint reservation-clearance probability inside the mandate;
- **Low regret** — lowest worst-case regret inside the mandate;
- **Escalate for authority** — economically attractive package that requires higher authority.

Duplicate package suggestions are removed.

## Concession ledger

The concession ledger is intentionally explicit about estimates. If an `estimatedOwnCost` is supplied, it is used in the aggregate concession balance; otherwise magnitude is used as a fallback accounting unit. This is not an economic valuation unless the user has supplied a defensible valuation method.

An automatic playbook warning is raised when recorded Canadian concessions are more than 20% ahead of recorded U.S. concessions.

## API

`POST /api/evaluate` now includes a top-level `robustness` block.

`GET /api/room` returns the current room state and counteroffer suggestions based on the most recent evaluation.

`POST /api/room` accepts append-only room events:

- `set-round`;
- `set-mandate`;
- `red-line`;
- `offer`;
- `concession`;
- `playbook`;
- `debrief`.

## Security boundary

The append-only local event log is useful for research continuity, but it is **not** a secure diplomatic records system. The application explicitly reports `secureForProtectedInformation=false`.

Do not store classified, protected, privileged or negotiation-sensitive operational records in this prototype without an accredited security environment with authentication, access controls, encryption, audit, retention and records-management controls.
