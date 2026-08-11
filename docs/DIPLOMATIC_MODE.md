# Diplomatic mode

Diplomatic mode turns the Canada Policy Studio from an analyst-facing scenario dashboard into a negotiation-support workspace. It does **not** turn the model into a mandate authority, forecast engine, legal position, or substitute for diplomatic judgment.

## Decision workflow

1. **Set the economic situation.** Confirm the data vintage, headline tariff assumptions, retaliation, sector coverage, risk tolerance and cooperation ceiling.
2. **Read the three-package ladder.** The workspace distinguishes a preferred package, a bridge package chosen for bilateral balance, and a fallback package that clears locally defined red lines where possible.
3. **Set red lines.** Minimum Canada score, minimum U.S. score, maximum recession risk, minimum bilateral growth and maximum inflation are stored locally in the browser. They do not alter the model itself.
4. **Inspect reciprocity.** The concession map identifies sectors where modeled coverage relief creates the largest negotiating movement. Coverage relief is a bargaining cue, not a tariff schedule or legal commitment.
5. **Prepare language.** Talking points deliberately distinguish statements such as “the model estimates…” from political instructions such as “Canada must…”.
6. **Generate a briefing note.** The printable brief records the preferred, bridge and fallback packages, red-line status, leading concession areas, talking points and private working notes.

## Preferred, bridge and fallback packages

- **Preferred package:** the highest-ranked package under the current model and negotiation settings.
- **Bridge package:** an alternative selected for bilateral balance, with an explicit penalty for a large Canada–U.S. score gap and recession exposure.
- **Fallback package:** the highest-ranked remaining package that clears the diplomat’s local red lines where one exists; otherwise the status-quo reference is retained.

This ladder is intended to support sequencing in the room. A model winner is not automatically a politically feasible agreement.

## Decision robustness

Diplomatic mode reports the score separation between the first- and second-ranked packages as a **decision-robustness cue**. It is intentionally not labelled statistical confidence. A close score spread should encourage sensitivity analysis, additional scenarios and stronger reliance on expert judgment.

## Institutional durability

The diplomatic layer uses delegation labels rather than tying the workflow to individual officeholders. Negotiation state is attributed to the Canada delegation, U.S. delegation, or automatic search. This keeps the tool usable through personnel changes and avoids confusing model state with a named official’s actual position.

## Information handling

Red lines and diplomat notes are stored in the user’s browser through local storage. They are not included in the economic evaluation request. The existing live negotiation state remains shared through the application’s negotiation endpoint.

Do not enter classified, protected, privileged or otherwise restricted information unless the deployment environment has been explicitly approved for that information category. The tool itself does not assign or enforce a security classification.

## Analytical protocol

Use language that preserves the distinction between evidence and authority:

- Good: “Under these assumptions, the model ranks Package A first.”
- Good: “The model shows the largest reciprocal sector movement in manufacturing.”
- Good: “The preferred and bridge packages are close; the result is sensitive to the red-line choice.”
- Avoid: “The model proves Package A will work.”
- Avoid: “Canada must accept this package.”
- Avoid: “The searched GDP floor guarantees growth.”

The model remains an illustrative scenario comparator. Material negotiating decisions require current data, legal review, policy authority, expert model comparison and political judgment.
