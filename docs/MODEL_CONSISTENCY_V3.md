# Model Consistency V3

This document records the model-consistency and structural-uncertainty contract introduced after the V2 plumbing and server-hardening tranches. It is a software/model-risk contract, not an empirical-validation claim.

## 1. One directional tariff quantity response

For each of the 20 sectors and each bilateral direction, the production model now computes one bounded constant-elasticity quantity response:

`quantity_ratio = (1 + effective_tariff_rate)^(-elasticity)`

and therefore:

`quantity_loss = 1 - quantity_ratio`.

The same directional quantity loss is consumed by:

- the bilateral trade ledger and tariff-revenue base;
- the macro trade-drag channel;
- the first-round production-network supplier-demand shock;
- direct sector output effects;
- sector protection/leverage terms used in bilateral sector welfare.

The sector layer no longer reconstructs a separate linear `tariff × elasticity` volume response. This matters particularly in large-tariff stress cases: the constant-elasticity formulation remains bounded and positive without an arbitrary quantity floor.

Tariff incidence remains decomposed into buyer pass-through, exporter absorption and importer absorption. Price effects consume the incidence objects rather than rebuilding a second tariff-price equation.

## 2. Production-network coefficients are structural parameters

The following coefficients were previously embedded as constants in `trade_network.cpp` and are now explicit `StructuralParameters`, `TradeNetworkTuning` inputs and registry entries:

- `network_supplier_demand_transmission`;
- `network_input_cost_incidence`;
- `network_downstream_cost_transmission`;
- `network_price_cost_pass_through`;
- `network_output_cost_base`;
- `network_output_cost_cyclical`;
- `network_jobs_output_base`;
- `network_jobs_output_exposure`.

Their V3 registry classifications are **assumed/model-risk sensitivity parameters**. Making them executable and bounded does not make them empirical estimates.

The practical consequence is intentionally conservative: the structural registry now contains 37 required parameters rather than 29, while the direct-empirical numerator is unchanged. Model completeness therefore reports lower empirical coverage because previously hidden assumptions are now visible in the denominator.

## 3. Structural dependence is explicit

The V3 structural registry supports `CORR` records with:

- left and right parameter names;
- a correlation coefficient;
- provenance kind;
- source identifier;
- vintage;
- notes.

The current registry declares two moderate positive dependence assumptions among related network-transmission parameters. They are explicitly labelled assumptions, not estimated covariance coefficients.

Structural robustness builds the declared sampled-parameter correlation matrix and obtains correlated standardized Gaussian draws through a Cholesky factorization. Normal and lognormal parameter draws then consume those standardized draws subject to each parameter's declared bounds and distribution.

The sampler fails closed if the declared matrix is not positive semidefinite or is otherwise inconsistent. It does not silently revert to independent sampling.

The inflation-persistence / expectations-weight identity remains a derived constraint rather than an independently sampled relationship.

## 4. Innovation covariance is not parameter-uncertainty covariance

`output_inflation_shock_correlation` remains the frozen contemporaneous correlation from the 75-quarter production-form residual panel. It shapes the macro innovation process.

It is **not** interpreted as a correlation between uncertainty in the estimated output-shock and inflation-shock scale parameters. These are different statistical objects and remain separate in the registry and robustness sampler.

## 5. Robustness reporting

The structural-robustness response now reports:

- `structuralSamplingDependence`;
- `correlationPairCount`;
- `correlationMatrixValid`;
- the existing sampled-parameter count, bounds/provenance status and Monte Carlo precision fields.

`parameterProvenanceComplete` requires both a complete structural registry and a valid structural sampling correlation matrix.

Wilson intervals and Monte Carlo standard errors continue to describe **simulation-sampling precision only**. They are not confidence intervals for economic parameters, causal effects, bargaining outcomes or political probabilities.

## 6. Negotiation-room event contract

New room events pass a typed action schema before any mutable room state changes. The live action contract:

- rejects malformed JSON, duplicate keys and wrong scalar types through the common strict JSON parser;
- validates action-specific required fields and ranges;
- rejects unknown action fields;
- validates enumerations such as `side`;
- rejects unsupported schema versions;
- performs no mutation on rejected events.

Accepted events are persisted in canonical JSON with `schemaVersion: 1`.

Valid legacy unversioned scalar JSON events remain replayable so existing local event logs are not discarded, but newly persisted events are versioned and canonical.

The room remains a local research workflow and is not an accredited store for classified, protected, privileged or negotiation-sensitive information.

## 7. What V3 does not claim

V3 does not:

- certify the provisional U.S. USEEIO proxy as current-vintage empirical U.S. input-output data;
- activate OECD bilateral intermediate sourcing;
- identify the new network-transmission coefficients empirically;
- convert model-risk correlation assumptions into estimated covariance;
- turn Monte Carlo recommendation frequency into a political forecast;
- prove global optimality outside the declared finite policy/search design.

Those empirical promotions require separate source, crosswalk, estimation and certification work and should be reviewed independently from this software-consistency tranche.
