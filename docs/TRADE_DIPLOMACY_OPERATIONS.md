# Canada–U.S. Trade Diplomacy Operations Platform

This layer turns the existing macro-policy and bargaining engines into a structured negotiation-operations system for Canada–United States trade diplomacy.

It remains a research decision-support tool. It is **not** an official Government of Canada, Government of the United States, CUSMA/USMCA secretariat, Bank of Canada, USTR, or other government system. It does not contain classified or privileged material, and its modeled utilities do not reveal either government's actual negotiating mandate.

## Design objective

The platform should help a delegation answer six distinct questions without conflating them:

1. **What is happening economically?** — stochastic macro and sector scenario engine.
2. **What packages are mutually better than the outside options?** — BATNA, reservation-value, issue-linkage and Pareto bargaining engine.
3. **Which packages remain attractive under harder assumptions?** — robust-package screen.
4. **Which issues belong in which legal/forum track?** — treaty-aware issue registry.
5. **How should a package be sequenced, verified and protected against defection?** — round plan and implementation guardrails.
6. **Who still needs to authorize, review or implement it?** — domestic-authority and stakeholder gates.

The model is deliberately designed to stop at the boundary of political authority. It may say that a package is efficient or robust under stated assumptions. It must not say that Canada or the United States “will accept” a package unless that conclusion comes from actual mandate authority outside the model.

## Public 2025–26 review context used to seed the issue registry

The default issue map is informed by publicly stated themes around the first six-year CUSMA/USMCA review, including market-access predictability, rules of origin, North American supply-chain resilience/economic security, autos, steel and aluminum, agriculture, regulatory compatibility, labor/environment and digital/electronic-payment issues.

These are **public-theme defaults**, not inferred negotiating positions. They must be refreshed before a real negotiation round.

Primary public references used when this layer was designed:

- Global Affairs Canada, 2025 CUSMA consultation report: https://international.canada.ca/en/global-affairs/consultations/trade/2025-09-19-cusma/report
- Global Affairs Canada, CUSMA consultation page: https://international.canada.ca/en/global-affairs/consultations/trade/2025-09-19-cusma
- USTR, USMCA joint-review public-comment notice: https://ustr.gov/about/policy-offices/press-office/press-releases/2025/september/ustr-seeks-public-comment-joint-review-usmca
- USTR, 2026 review-related rounds and public statements: https://ustr.gov/

## Treaty-aware issue architecture

The platform distinguishes **CUSMA/USMCA-linked tracks** from **parallel bilateral or instrument-specific tracks**.

That distinction is essential. A useful diplomatic platform must not imply that every bilateral trade dispute is governed by, amendable through, or exchangeable under the same CUSMA/USMCA provision.

Default tracks include:

- goods market access and tariff measures;
- rules of origin and automotive integration;
- customs, border and regulatory compatibility;
- agriculture and agri-food;
- digital trade / electronic-payment interfaces;
- labor and environment;
- dispute prevention, enforcement and review;
- economic security and non-market inputs;
- energy, critical minerals and strategic infrastructure;
- steel/aluminum as a parallel economic-security/remedy track;
- procurement as an instrument-specific parallel track unless the applicable legal basis is confirmed;
- persistent bilateral files such as softwood lumber as separate tracks unless negotiators deliberately establish a lawful linkage.

Every issue carries:

- forum/legal-reference text;
- Canadian and U.S. modeled priority signals;
- joint-value signal;
- domestic sensitivity;
- implementation complexity;
- verification evidence;
- an issue-linkage group.

The legal-reference text is a routing aid, **not legal advice**. Counsel must verify the exact agreement article, statute, regulation, remedy or parallel instrument before any commitment is offered.

## Robust-package screen

The bargaining engine already produces individually rational, Pareto-efficient packages. The operations layer subjects those packages to six additional reduced-form stress cases:

1. baseline mandate;
2. tighter Canadian domestic constraint;
3. tighter U.S. domestic constraint;
4. higher bilateral trade sensitivity;
5. lower cooperation / higher political cost of large moves;
6. implementation stress / weaker enforcement.

For each frontier package, the platform calculates:

- worst-case bilateral surplus floor;
- average minimum surplus across stress cases;
- number of stress cases in which it is the preferred package;
- whether both sides remain above modeled reservation values in all cases;
- a robustness score that also considers stability and Nash gains.

This is **not an acceptance-probability model**. It is a robustness screen for decision discipline.

## Round playbook

The platform generates a six-stage operating sequence:

1. **Mandate and facts** — freeze data vintage, legal forum, authority and red lines.
2. **Early harvest** — bank high-joint-value, lower-sensitivity implementation gains.
3. **Conditional exchange** — trade costly moves only through explicit if/then linkage.
4. **Package round** — table the robust preferred package while preserving a bridge package.
5. **Closure architecture** — legal drafting, sequencing, verification, notice-and-cure and lawful response mechanisms.
6. **Post-agreement management** — monitor implementation before slippage becomes a new tariff/dispute cycle.

## Implementation and enforcement architecture

A serious trade package needs more than a score. The platform therefore generates guardrails for:

- reciprocal implementation tranches;
- origin/circumvention verification;
- border and regulatory service levels;
- unilateral-deviation risk;
- periodic review without automatically reopening the entire package.

Each guardrail specifies:

- trigger;
- evidence;
- response logic;
- review cadence.

The response language is intentionally generic and conditioned on lawful authority. The model must not invent a legal right to retaliate or “snap back” a concession.

## Domestic-authority and stakeholder gates

The platform explicitly separates:

- **formal authority gates** — mandate, legal review and joint implementation ownership;
- **domestic-coalition / consultation gates** — federal–provincial/territorial coordination, congressional consultation where applicable, states, Indigenous groups, labor, agriculture, industry and affected communities.

The tool does not assert that every listed consultation is a formal legal prerequisite. It treats them as implementation and coalition risks that should be surfaced before closure.

## Evidence and provenance

Every real negotiating round should preserve a reproducible evidence package containing:

- timestamped macro/trade baseline;
- tariff and measure inventory;
- source and data vintage;
- model commit/version;
- assumptions and stress cases;
- legal/forum mapping for each issue;
- verification metric owner for every commitment;
- decision snapshot and rationale.

The browser decision ledger in the research app is convenience-only. It is stored in local browser storage and is **not** a secure diplomatic records system.

## Security boundary

The current research server is not suitable for classified, protected, privileged or otherwise controlled negotiating information.

A production diplomatic deployment would need, at minimum:

- approved identity provider and multi-factor authentication;
- role-based / attribute-based access control;
- encryption in transit and at rest;
- managed secrets and key rotation;
- immutable audit logging;
- approved records retention and legal-hold controls;
- secure document ingestion and malware scanning;
- environment separation and deployment accreditation;
- database-backed negotiation/decision history rather than browser local storage;
- resilient multi-user concurrency and session isolation;
- provenance signatures for model/data versions;
- an explicit classification/handling model;
- independent security assessment.

Until that architecture exists, use the app for public/unclassified research and scenario preparation only.

## What “best” means here

The platform should optimize neither national capitulation nor a synthetic “50/50” compromise. It should make trade-offs inspectable, preserve outside options, distinguish legal tracks, expose implementation incentives, preserve alternatives, and make the evidence behind every recommendation reproducible.

A successful output is not “the computer found the deal.” It is a disciplined package of:

- facts;
- assumptions;
- options;
- outside options;
- robust trade-offs;
- linked concessions;
- legal/forum routing;
- authority gates;
- implementation controls;
- and a record of why a human delegation chose what it chose.
