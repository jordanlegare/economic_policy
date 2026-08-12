# V2 welfare preference sensitivity

Welfare sensitivity is a normative robustness experiment. It is separate from structural-parameter uncertainty and historical backtesting.

The experiment asks whether the production recommendation survives plausible changes in the delegation's declared bilateral welfare preferences while the economic model, Bank mandate-loss coefficients, cooperation ceiling, growth constraint and stochastic design remain unchanged.

## Active preference grid

The default grid is centred on the submitted `Economy` preferences. It evaluates a 3 x 3 local surface:

- Canada priority share: reference minus 15 percentage points, reference, reference plus 15 percentage points;
- risk aversion: reference minus 25 points, reference, reference plus 25 points.

Canada/U.S. priorities are normalized to 100% and bounded to 5%-95% so both parties remain represented. Risk aversion is bounded to 0-100. Duplicate boundary profiles are removed.

For the default 50/50 priority and 50 risk-aversion state, this yields nine full production evaluations.

## What is re-optimized

Each preference profile calls the ordinary production `PolicyEngine::evaluate()` path. Therefore every profile reruns:

1. the 288 generated policy-control candidates;
2. the fixed expert strategies;
3. the 20-sector Pareto search;
4. 700-draw finalist selection;
5. 2,800-draw verification;
6. final strategy ranking under that profile's Canada/U.S. priority and risk-aversion inputs.

This is not a score-only re-ranking of frozen scenarios.

## What remains fixed

The experiment does **not** vary the internal BoC mandate-loss coefficients or rewrite the mandate. `mandateWeightsFixed=true` remains a required audit signal for every profile.

It also does not alter structural parameters, historical inputs, cooperation ceilings or bilateral growth floors. Those are tested in separate V2 layers.

## Reported diagnostics

`evaluate_welfare_sensitivity()` reports:

- reference strategy;
- profile count;
- reference-control retention rate;
- strategy-family retention rate;
- sector-package retention rate;
- number of recommendation switches;
- nearest tested Canada-priority shift that switches the control decision, when observed on the reference-risk slice;
- nearest tested risk-aversion shift that switches the control decision, when observed on the reference-priority slice;
- minimum and maximum bilateral fairness score across the grid;
- alternative strategy win counts/rates;
- whether all tested winners satisfy the bilateral growth constraint;
- whether all runs preserve fixed mandate weights.

The JSON serializer is `welfare_sensitivity_to_json()`.

## Interpretation

Project labels are descriptive decision-stability labels:

- `preference-robust`: reference controls retained in at least 80% of tested profiles;
- `moderately-preference-robust`: at least 60%;
- `preference-fragile`: at least 40%;
- `preference-unstable`: below 40%.

These are not confidence intervals and the nine-point grid is not a probability distribution over political preferences.

A switch threshold is only the nearest **tested** shift, not an exact continuous indifference point. A later extension may use adaptive bisection around observed switches if continuous threshold estimates become decision-relevant.

## Boundary of this milestone

This milestone tests delegation priorities and risk aversion because those are already explicit normative inputs to the production objective. Sensitivity of the internal component weights inside the BoC, federal and U.S. loss functions remains a separate extension and should only be added with a typed weight registry and explicit mandate/assumption classification.
