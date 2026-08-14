(() => {
  'use strict';

  // Retired dashboard surface: Computational negotiation support.
  // Keep package-identity helpers available to non-UI consumers and tests.
  function materialPackageKey(package_) {
    const issues = (package_?.issues || []).map(issue =>
      `${String(issue?.label ?? '')}|${Number(issue?.canadaMove || 0).toFixed(6)}|${Number(issue?.usMove || 0).toFixed(6)}`
    ).sort().join('||');
    return `${String(package_?.strategyName || '')}||${issues}`;
  }

  function bestUniqueFrontierPackages(frontier, limit=9) {
    const seen = new Set(), unique = [];
    for (const package_ of frontier || []) {
      const key = materialPackageKey(package_);
      if (seen.has(key)) continue;
      seen.add(key);
      unique.push(package_);
      if (unique.length >= limit) break;
    }
    return unique;
  }

  window.NegotiationFrontier = {materialPackageKey, bestUniqueFrontierPackages};
})();

// Outcome-first bilateral dashboard. The full-economy and deal metrics are exposed
// as one attributable scenario ledger while the detailed panels remain the drill-down.
(() => {
  'use strict';

  let evaluated = null;
  let initialized = false;
  const number = (value, fallback=0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const hasNumber = value => Number.isFinite(Number(value));
  const fmtDeal = (value, digits=1) => hasNumber(value) ? Number(value).toFixed(digits) : '—';
  const signedDeal = (value, suffix='%', digits=1) => hasNumber(value)
    ? `${Number(value) >= 0 ? '+' : ''}${Number(value).toFixed(digits)}${suffix}` : '—';
  const averageDeal = values => Array.isArray(values) && values.length
    ? values.reduce((sum, value) => sum + number(value), 0) / values.length
    : 0;
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
  }[c]));

  function dealForm(snapshot) {
    if (!snapshot) return 'Evaluating deal';
    const usCoverage = averageDeal(snapshot.usCoverage);
    const caCoverage = averageDeal(snapshot.canadaCoverage);
    const usRelief = 100 - usCoverage;
    const caRelief = 100 - caCoverage;
    const usEffective = number(snapshot.usTariff) * usCoverage / 100;
    const caEffective = number(snapshot.canadaTariff) * caCoverage / 100;
    if (usEffective < 1 && caEffective < 1) return 'Near-free trade';
    if (Math.abs(usEffective - caEffective) >= 10)
      return usEffective > caEffective ? 'U.S.-weighted protection' : 'Canada-weighted protection';
    if (usRelief >= 10 && caRelief >= 10) return 'Mutual sector relief';
    if (usCoverage >= 90 && caCoverage >= 90) return 'Broad tariff coverage';
    return 'Targeted sector bargain';
  }

  function reliefLeaders(coverage, limit=3) {
    const names = typeof sectorNames !== 'undefined' ? sectorNames : [];
    return (coverage || []).map((value, index) => ({
      name:names[index] || `Sector ${index + 1}`,
      relief:Math.max(0, 100 - number(value))
    })).filter(item => item.relief >= 1)
      .sort((a,b) => b.relief - a.relief)
      .slice(0, limit);
  }

  function currentScenario() {
    try { return selected || result?.scenarios?.[0] || null; } catch (_) { return null; }
  }

  function leadingScenario() {
    try { return result?.scenarios?.[0] || null; } catch (_) { return null; }
  }

  function matchedBaseline(scenario) {
    if (!scenario) return null;
    try {
      return noTariff?.scenarios?.find(item => item.id === scenario.id)
        || noTariff?.scenarios?.[0] || null;
    } catch (_) { return null; }
  }

  function sectorImpactSummary(scenario, country='canada', metric='output') {
    const sectors = Array.isArray(scenario?.sectors) ? scenario.sectors : [];
    const values = sectors.map(sector => number(sector?.[country]?.[metric], NaN)).filter(Number.isFinite);
    if (!values.length) return {count:0, negative:0, positive:0, average:0, leaders:[]};
    const negative = values.filter(value => value < 0).length;
    const average = values.reduce((sum, value) => sum + value, 0) / values.length;
    const leaders = sectors.map(sector => ({
      name:sector.name || sector.code || 'Sector',
      code:sector.code || '',
      value:number(sector?.[country]?.[metric], 0)
    })).sort((a,b) => Math.abs(b.value) - Math.abs(a.value)).slice(0,3);
    return {count:values.length, negative, positive:values.length-negative, average, leaders};
  }

  function deltaNote(scenario, baseline, field, label='vs matched no-tariff') {
    if (!hasNumber(scenario?.[field]) || !hasNumber(baseline?.[field])) return 'Modeled scenario level';
    return `${signedDeal(number(scenario[field])-number(baseline[field]), ' pp')} ${label}`;
  }

  function evaluatedDealContext() {
    let rec = null;
    try { rec = result?.recommendation || null; } catch (_) {}
    const state = window.EvaluationController?.state?.() || {};
    const controls = state.lastEvaluatedControls || {};
    const usCoverage = Array.isArray(rec?.usSectorCoverage) && rec.usSectorCoverage.length
      ? rec.usSectorCoverage : (state.displayedCoverage?.us || []);
    const canadaCoverage = Array.isArray(rec?.canadaSectorCoverage) && rec.canadaSectorCoverage.length
      ? rec.canadaSectorCoverage : (state.displayedCoverage?.canada || []);
    const usTariff = number(controls.usTariff,
      typeof tariff !== 'undefined' && tariff ? tariff.value : document.querySelector('#usTariff')?.value);
    const canadaTariff = number(controls.retaliatoryTariff, document.querySelector('#retaliatoryTariff')?.value);
    return {
      rec,
      usCoverage,
      canadaCoverage,
      usTariff,
      canadaTariff,
      usEffective:usTariff * averageDeal(usCoverage) / 100,
      canadaEffective:canadaTariff * averageDeal(canadaCoverage) / 100
    };
  }

  function activeSectorCountry() {
    return document.querySelector('.country-switch button.active')?.dataset.country || 'canada';
  }

  function toneFromSign(value, inverse=false) {
    if (!hasNumber(value) || Math.abs(Number(value)) < 1e-9) return 'neutral';
    const good = inverse ? Number(value) < 0 : Number(value) > 0;
    return good ? 'positive' : 'negative';
  }

  function buildImpactGroups(scenario, baseline, context={}) {
    if (!scenario) return [];
    const country = context.sectorCountry || 'canada';
    const output = sectorImpactSummary(scenario, country, 'output');
    const jobs = sectorImpactSummary(scenario, country, 'jobs');
    const prices = sectorImpactSummary(scenario, country, 'prices');
    const terminalRate = Array.isArray(scenario.rates) && scenario.rates.length
      ? scenario.rates[scenario.rates.length-1] : null;
    const weakestScore = Math.min(number(scenario.canadaScore, 0), number(scenario.usScore, 0));
    const growthProtected = scenario.sustainedBilateralGrowth === true;
    const sectorCountryLabel = country === 'us' ? 'United States' : 'Canada';

    return [
      {
        id:'growth', label:'Growth & activity', kicker:'Whole-economy outcome',
        items:[
          {label:'Canada GDP growth', value:`${fmtDeal(scenario.growth)}%`, note:deltaNote(scenario,baseline,'growth'), tone:toneFromSign(scenario.growth), panel:'projection', series:'growthPath'},
          {label:'U.S. GDP growth', value:`${fmtDeal(scenario.usGrowth)}%`, note:deltaNote(scenario,baseline,'usGrowth'), tone:toneFromSign(scenario.usGrowth), panel:'strategies'},
          {label:'Canada unemployment', value:`${fmtDeal(scenario.unemployment)}%`, note:'Terminal modeled labour-market rate', tone:'neutral', panel:'projection'},
          {label:'Bilateral growth floor', value:`${fmtDeal(scenario.bilateralGrowthFloor)}%`, note:'Lower of the two modeled GDP paths', tone:growthProtected?'positive':'caution', panel:'strategies'},
          {label:'Recession risk', value:`${fmtDeal(scenario.recessionRisk,0)}%`, note:'Any quarter across seeded paths', tone:number(scenario.recessionRisk)>40?'negative':number(scenario.recessionRisk)>25?'caution':'neutral', panel:'strategies'}
        ]
      },
      {
        id:'households', label:'Households & prices', kicker:'Canadian transmission',
        items:[
          {label:'Inflation', value:`${fmtDeal(scenario.inflation)}%`, note:deltaNote(scenario,baseline,'inflation'), tone:number(scenario.inflation)>3?'caution':'neutral', panel:'projection', series:'inflationPath'},
          {label:'Cost of living', value:`${fmtDeal(scenario.costOfLiving)}%`, note:deltaNote(scenario,baseline,'costOfLiving'), tone:'neutral', panel:'projection', series:'costPath'},
          {label:'Real income growth', value:signedDeal(scenario.realIncomeGrowth), note:deltaNote(scenario,baseline,'realIncomeGrowth'), tone:toneFromSign(scenario.realIncomeGrowth), panel:'strategies'},
          {label:'Terminal policy rate', value:hasNumber(terminalRate)?`${fmtDeal(terminalRate,2)}%`:'—', note:'Quarter-12 policy-rate path', tone:'neutral', panel:'projection', series:'rates'}
        ]
      },
      {
        id:'trade', label:'Trade & border', kicker:'Bilateral deal mechanics',
        items:[
          {label:'Canada exports', value:signedDeal(scenario.exports), note:deltaNote(scenario,baseline,'exports'), tone:toneFromSign(scenario.exports), panel:'sectors'},
          {label:'U.S. exports', value:signedDeal(scenario.usExportChange), note:deltaNote(scenario,baseline,'usExportChange'), tone:toneFromSign(scenario.usExportChange), panel:'sectors'},
          {label:'U.S. coverage-adjusted tariff', value:`${fmtDeal(context.usEffective)}%`, note:`${fmtDeal(context.usTariff,0)}% headline × ${fmtDeal(averageDeal(context.usCoverage),0)}% average coverage`, tone:'neutral', panel:'decision-overview'},
          {label:'Canada coverage-adjusted tariff', value:`${fmtDeal(context.canadaEffective)}%`, note:`${fmtDeal(context.canadaTariff,0)}% headline × ${fmtDeal(averageDeal(context.canadaCoverage),0)}% average coverage`, tone:'neutral', panel:'decision-overview'},
          {label:'Bilateral balance gap', value:hasNumber(scenario.tradeBalanceGapUsd)?`US$${fmtDeal(scenario.tradeBalanceGapUsd)}B`:'—', note:'Accounting diagnostic; not a welfare objective', tone:'neutral', panel:'fiscal-ledger'}
        ]
      },
      {
        id:'fiscal', label:'Public finances', kicker:'Revenue & policy stance',
        items:[
          {label:'U.S. tariff revenue', value:hasNumber(scenario.usTariffRevenueUsd)?`US$${fmtDeal(scenario.usTariffRevenueUsd)}B`:'—', note:'Annualized post-elasticity receipts', tone:'neutral', panel:'fiscal-ledger'},
          {label:'Canada tariff revenue', value:hasNumber(scenario.canadaTariffRevenueCad)?`C$${fmtDeal(scenario.canadaTariffRevenueCad)}B`:'—', note:'Annualized post-elasticity receipts', tone:'neutral', panel:'fiscal-ledger'},
          {label:'Canada debt', value:hasNumber(scenario.debt)?`${fmtDeal(scenario.debt)}%`:'—', note:'Modeled debt metric in the inspected package', tone:'neutral', panel:'projection', series:'debtPath'},
          {label:'Fiscal impulse', value:hasNumber(scenario.fiscal)?signedDeal(scenario.fiscal,'%'):'—', note:'Package-level fiscal setting', tone:'neutral', panel:'strategies'}
        ]
      },
      {
        id:'quality', label:'Agreement quality', kicker:'Modeled bilateral value',
        items:[
          {label:'Canada deal score', value:`${fmtDeal(scenario.canadaScore,0)}/100`, note:'Canada-side modeled bilateral value', tone:number(scenario.canadaScore)>=50?'positive':'caution', panel:'strategies'},
          {label:'U.S. deal score', value:`${fmtDeal(scenario.usScore,0)}/100`, note:'U.S.-side modeled bilateral value', tone:number(scenario.usScore)>=50?'positive':'caution', panel:'strategies'},
          {label:'Weakest-side score', value:`${fmtDeal(weakestScore,0)}/100`, note:'Minimum of Canada and U.S. deal scores', tone:weakestScore>=50?'positive':'caution', panel:'strategies'},
          {label:'Canada mandate score', value:`${fmtDeal(scenario.bocScore,0)}/100`, note:'Bank of Canada mandate component', tone:'neutral', panel:'strategies'},
          {label:'Canada public score', value:`${fmtDeal(scenario.federalScore,0)}/100`, note:'Canadian public/federal component', tone:'neutral', panel:'strategies'},
          {label:'Growth protection', value:growthProtected?'Protected':'Not protected', note:'Whether both modeled GDP paths clear the searched floor', tone:growthProtected?'positive':'caution', panel:'strategies'}
        ]
      },
      {
        id:'sectors', label:'Complete economy · 20 NAICS sectors', kicker:`${sectorCountryLabel} impact breadth`,
        items:[
          {label:'Output impact', value:`${output.negative} negative · ${output.positive} protected`, note:`Average ${signedDeal(output.average)} across ${output.count || 20} sectors`, tone:toneFromSign(output.average), panel:'sectors', sectorMetric:'output', leaders:output.leaders},
          {label:'Employment impact', value:`${jobs.negative} negative · ${jobs.positive} protected`, note:`Average ${signedDeal(jobs.average)} across ${jobs.count || 20} sectors`, tone:toneFromSign(jobs.average), panel:'sectors', sectorMetric:'jobs', leaders:jobs.leaders},
          {label:'Consumer-price impact', value:`${prices.positive} higher · ${prices.negative} lower`, note:`Average ${signedDeal(prices.average)} across ${prices.count || 20} sectors`, tone:toneFromSign(prices.average,true), panel:'sectors', sectorMetric:'prices', leaders:prices.leaders}
        ]
      }
    ];
  }

  function evaluatedSnapshot() {
    let best = null, rec = null;
    try {
      best = result?.scenarios?.[0] || null;
      rec = result?.recommendation || null;
    } catch (_) {}
    if (!best) return null;
    const state = window.EvaluationController?.state?.() || {};
    const controls = state.lastEvaluatedControls || {};
    const usCoverage = Array.isArray(rec?.usSectorCoverage) && rec.usSectorCoverage.length
      ? [...rec.usSectorCoverage]
      : typeof positions !== 'undefined' && Array.isArray(positions?.us) ? [...positions.us] : [];
    const canadaCoverage = Array.isArray(rec?.canadaSectorCoverage) && rec.canadaSectorCoverage.length
      ? [...rec.canadaSectorCoverage]
      : typeof positions !== 'undefined' && Array.isArray(positions?.canada) ? [...positions.canada] : [];
    return {
      best,
      rec,
      usTariff:number(controls.usTariff, typeof tariff !== 'undefined' ? tariff?.value : 0),
      canadaTariff:number(controls.retaliatoryTariff, document.querySelector('#retaliatoryTariff')?.value),
      usCoverage,
      canadaCoverage,
      capturedAt:Date.now()
    };
  }

  function ensurePrincipalBriefUi() {
    if (document.querySelector('#diplomaticBriefing')) return;
    const dialog = document.createElement('dialog');
    dialog.id = 'diplomaticBriefing';
    dialog.innerHTML = '<div class="briefing-toolbar"><b>Principal decision brief</b><div>'
      + '<button id="copyBriefing" type="button">Copy principal brief</button>'
      + '<button id="saveBriefingPdfDialog" type="button" onclick="window.print()">Save principal PDF</button>'
      + '<button id="closeBriefing" type="button">Close</button>'
      + '</div></div><div id="briefingSheet" class="briefing-sheet"></div>';
    document.body.appendChild(dialog);
    document.querySelector('#closeBriefing')?.addEventListener('click', () => dialog.close());
  }

  function installImpactLedger() {
    const sidebar = document.querySelector('#dashboardView .dashboard-sidebar');
    if (!sidebar || document.querySelector('#dealImpactLedger')) return;
    const ledger = document.createElement('section');
    ledger.id = 'dealImpactLedger';
    ledger.className = 'deal-impact-ledger';
    ledger.innerHTML = `<div class="deal-impact-hero">
      <div class="eyebrow">Possible deal outcome</div>
      <h2>Attributable impact ledger</h2>
      <p>Every member below is tied to the currently inspected modeled package. Where a matched no-tariff run exists, the note shows the scenario difference; otherwise the modeled level or terminal outcome is shown.</p>
      <div class="deal-impact-context"><span>INSPECTED PACKAGE</span><b id="impactLedgerPackage">Waiting for evaluation…</b></div>
      <div id="impactLedgerState" class="deal-impact-state">Model outcome · not a causal estimate or forecast.</div>
    </div><div id="impactLedgerGroups"></div>`;
    sidebar.insertBefore(ledger, sidebar.firstChild);
    const label = document.createElement('div');
    label.className = 'sidebar-controls-label';
    label.textContent = 'Deal controls';
    ledger.insertAdjacentElement('afterend', label);
  }

  function installAttributionBanner() {
    const content = document.querySelector('#dashboardView .content');
    const tools = document.querySelector('#dashboardView .dashboard-stack-tools');
    if (!content || !tools || document.querySelector('#dealAttributionBanner')) return;
    const banner = document.createElement('section');
    banner.id = 'dealAttributionBanner';
    banner.className = 'deal-attribution-banner';
    banner.innerHTML = `<div><div class="eyebrow">Joint dashboard · outcome-first view</div><h1>Possible deal, traced through the whole economy.</h1><p>The dashboard now follows one inspected policy package from border terms through growth, household prices, trade, fiscal effects, bilateral value, the 12-quarter path and all 20 NAICS sectors. Scenario differences are model comparisons, not causal estimates.</p></div><div class="deal-attribution-chips">
      <div class="deal-attribution-chip"><span>Inspected package</span><b id="attributionPackage">Evaluating…</b><small id="attributionPackageKind">Searched policy package</small></div>
      <div class="deal-attribution-chip"><span>Bilateral value</span><b id="attributionScores">—</b><small>Canada / United States</small></div>
      <div class="deal-attribution-chip"><span>Comparator</span><b>Matched no-tariff run</b><small>Used for available delta notes</small></div>
      <div class="deal-attribution-chip"><span>Sector lens</span><b id="attributionSectorCountry">Canada · 20 sectors</b><small>Output · jobs · consumer prices</small></div>
    </div>`;
    content.insertBefore(banner, tools);
  }

  function relabelOutcomePanels() {
    const labels = {
      'fiscal-ledger':['Deal outcome · public finance','Fiscal & trade consequences'],
      'strategies':['Possible deals','Compare attributable policy packages'],
      'projection':['Time path','12-quarter outcome path'],
      'sectors':['Complete economy · 20 NAICS sectors','Sector-by-sector deal impact']
    };
    Object.entries(labels).forEach(([id, [small, title]]) => {
      const node = document.querySelector(`details[data-dashboard-panel="${id}"] .dashboard-panel-title`);
      if (node) node.innerHTML = `<small>${esc(small)}</small><b>${esc(title)}</b>`;
    });
  }

  function installOverview() {
    if (initialized) return;
    const panel = document.querySelector('details[data-dashboard-panel="decision-overview"]');
    const body = panel?.querySelector('.dashboard-panel-body');
    if (!panel || !body) return;
    initialized = true;
    installImpactLedger();
    installAttributionBanner();
    relabelOutcomePanels();
    ensurePrincipalBriefUi();
    const title = panel.querySelector('.dashboard-panel-title');
    if (title) title.innerHTML = '<small>Deal on the table</small><b>Bilateral deal overview</b>';
    body.innerHTML = `
      <div id="dealOverview" class="deal-overview">
        <section class="deal-hero"><div><div class="eyebrow">Current evaluated bilateral deal</div><h1 id="dealHeadline">Evaluating the deal on the table…</h1><p id="dealSummary">Trade terms and whole-economy effects will appear after the current run completes.</p></div><div class="deal-verdict"><span>Deal screen</span><b id="dealVerdict">—</b><small id="dealVerdictNote">Model-based bilateral assessment</small></div></section>
        <div id="dealPending" class="deal-pending" hidden><span>Delegation inputs changed after the last run. Figures below remain tied to the last evaluated deal.</span><button id="dealRunNow" type="button">Run updated deal →</button></div>
        <section class="deal-section"><div class="deal-section-head"><div><div class="eyebrow">Terms</div><h3>What the deal actually does at the border</h3></div><p>Coverage-adjusted tariffs multiply the submitted headline rate by simple average sector coverage. They summarize the form of the deal; the fiscal ledger uses trade-weighted post-elasticity receipts.</p></div><div class="deal-terms">
          <div class="deal-term"><span>U.S. coverage-adjusted tariff</span><b id="dealUsEffective">—</b><small id="dealUsEffectiveNote">—</small></div>
          <div class="deal-term"><span>Canada coverage-adjusted tariff</span><b id="dealCaEffective">—</b><small id="dealCaEffectiveNote">—</small></div>
          <div class="deal-term"><span>U.S. sector relief</span><b id="dealUsRelief">—</b><small>average exemption from full headline coverage</small></div>
          <div class="deal-term"><span>Canada sector relief</span><b id="dealCaRelief">—</b><small>average exemption from full headline coverage</small></div>
        </div></section>
        <section class="deal-section"><div class="deal-section-head"><div><div class="eyebrow">Economic result</div><h3>What each side gets from this deal</h3></div><p>Leading evaluated policy package under the submitted delegation trade settings.</p></div><div class="deal-outcomes">
          <div class="deal-metric"><span>Canada GDP</span><b id="dealCanadaGdp">—</b><small>modeled growth</small></div>
          <div class="deal-metric"><span>U.S. GDP</span><b id="dealUsGdp">—</b><small>modeled growth</small></div>
          <div class="deal-metric"><span>Canada exports</span><b id="dealCanadaExports">—</b><small>terminal change</small></div>
          <div class="deal-metric"><span>U.S. exports</span><b id="dealUsExports">—</b><small>terminal change</small></div>
          <div class="deal-metric"><span>Canada inflation</span><b id="dealInflation">—</b><small>terminal rate</small></div>
          <div class="deal-metric"><span>Recession risk</span><b id="dealRecession">—</b><small>any quarter</small></div>
        </div></section>
        <section class="deal-section"><div class="deal-section-head"><div><div class="eyebrow">Deal quality</div><h3>Is the bargain balanced and durable?</h3></div><p>These are model screens, not political acceptance probabilities or legal conclusions.</p></div><div class="deal-quality-grid">
          <div class="deal-quality"><span>Bilateral value</span><b id="dealPartyScores">—</b><p id="dealPartyScoreNote">—</p></div>
          <div class="deal-quality"><span>Growth protection</span><b id="dealGrowthFloor">—</b><p id="dealGrowthFloorNote">—</p></div>
          <div class="deal-quality"><span>Where relief is concentrated</span><b id="dealReliefShape">—</b><p id="dealReliefSectors">—</p></div>
        </div><div class="deal-actions"><button id="openBriefing" type="button">Open principal brief</button><button id="printBriefing" type="button">Save principal brief PDF</button><button id="dealOpenFiscal" type="button">Fiscal & trade ledger</button><button id="dealOpenStrategies" type="button">Compare policy packages</button></div></section>
      </div>
      <div class="decision-overview-compat" aria-hidden="true"><div class="confidence"><b id="confidence">—</b></div><div class="brief"><div><strong id="signal"></strong><p id="rationale"></p></div><div id="regime"></div><div id="neutral"></div><div id="gap"></div></div><div class="impact-strip"><div id="impactGrowth"></div><div id="impactCost"></div><div id="impactExports"></div><div id="impactRisk"></div></div><div class="live-impact"><b id="negotiationSync"></b><span id="liveDealImpact"></span></div></div>`;
    document.querySelector('#dealRunNow')?.addEventListener('click', () => document.querySelector('#run')?.click());
    document.querySelector('#dealOpenFiscal')?.addEventListener('click', () => openPanel('fiscal-ledger'));
    document.querySelector('#dealOpenStrategies')?.addEventListener('click', () => openPanel('strategies'));
  }

  function openPanel(id) {
    const panel = document.querySelector(`details[data-dashboard-panel="${id}"]`);
    if (!panel) return;
    panel.open = true;
    panel.scrollIntoView({behavior:'smooth', block:'start'});
  }

  function listRelief(items) {
    return items.length
      ? items.map(item => `${item.name} ${fmtDeal(item.relief,0)}pt`).join(' · ')
      : 'No material sector exemptions';
  }

  function setText(selector, value) {
    const node = document.querySelector(selector);
    if (node) node.textContent = value;
  }

  function renderOverview() {
    installOverview();
    if (!evaluated) evaluated = evaluatedSnapshot();
    const snapshot = evaluated;
    if (!snapshot) return;
    const best = snapshot.best, rec = snapshot.rec || {};
    const usCoverage = averageDeal(snapshot.usCoverage), caCoverage = averageDeal(snapshot.canadaCoverage);
    const usRelief = 100 - usCoverage, caRelief = 100 - caCoverage;
    const usEffective = snapshot.usTariff * usCoverage / 100;
    const caEffective = snapshot.canadaTariff * caCoverage / 100;
    const form = dealForm(snapshot);
    const verified = rec.verifiedWinWin === true;
    const growthProtected = best.sustainedBilateralGrowth === true && rec.growthConstraintMet !== false;
    const verdict = verified && growthProtected ? 'Bilateral gain holds' : verified ? 'Win-win screen clears' : growthProtected ? 'Growth floor holds' : 'Material trade-off';
    const verdictNote = verified
      ? `Model win-win · Canada ${fmtDeal(best.canadaScore,0)}/100 · U.S. ${fmtDeal(best.usScore,0)}/100`
      : 'At least one bilateral value or verification condition is unresolved.';
    setText('#dealHeadline', `${form}: ${fmtDeal(usEffective)}% U.S. vs ${fmtDeal(caEffective)}% Canadian coverage-adjusted tariff`);
    setText('#dealSummary', `Average sector coverage is U.S. ${fmtDeal(usCoverage,0)}% and Canada ${fmtDeal(caCoverage,0)}%. The leading package produces Canada GDP ${fmtDeal(best.growth)}% and U.S. GDP ${fmtDeal(best.usGrowth)}%, with ${fmtDeal(best.recessionRisk,0)}% modeled recession risk.`);
    setText('#dealVerdict', verdict);
    setText('#dealVerdictNote', verdictNote);
    setText('#dealUsEffective', `${fmtDeal(usEffective)}%`);
    setText('#dealUsEffectiveNote', `${fmtDeal(snapshot.usTariff,0)}% headline × ${fmtDeal(usCoverage,0)}% average coverage`);
    setText('#dealCaEffective', `${fmtDeal(caEffective)}%`);
    setText('#dealCaEffectiveNote', `${fmtDeal(snapshot.canadaTariff,0)}% headline × ${fmtDeal(caCoverage,0)}% average coverage`);
    setText('#dealUsRelief', `${fmtDeal(usRelief,0)}pt`);
    setText('#dealCaRelief', `${fmtDeal(caRelief,0)}pt`);
    setText('#dealCanadaGdp', `${fmtDeal(best.growth)}%`);
    setText('#dealUsGdp', `${fmtDeal(best.usGrowth)}%`);
    setText('#dealCanadaExports', signedDeal(best.exports));
    setText('#dealUsExports', signedDeal(best.usExportChange));
    setText('#dealInflation', `${fmtDeal(best.inflation)}%`);
    setText('#dealRecession', `${fmtDeal(best.recessionRisk,0)}%`);
    const scoreGap = Math.abs(number(best.canadaScore) - number(best.usScore));
    setText('#dealPartyScores', `CA ${fmtDeal(best.canadaScore,0)} · US ${fmtDeal(best.usScore,0)}`);
    setText('#dealPartyScoreNote', `${fmtDeal(scoreGap,0)}-point modeled value gap · weakest side ${fmtDeal(Math.min(number(best.canadaScore), number(best.usScore)),0)}/100`);
    setText('#dealGrowthFloor', `${fmtDeal(best.bilateralGrowthFloor)}%`);
    setText('#dealGrowthFloorNote', growthProtected
      ? `Both GDP paths clear the searched ${fmtDeal(rec.gdpGrowthFloor)}% floor.`
      : `The searched ${fmtDeal(rec.gdpGrowthFloor)}% bilateral growth floor is not fully protected.`);
    const usLeaders = reliefLeaders(snapshot.usCoverage), caLeaders = reliefLeaders(snapshot.canadaCoverage);
    setText('#dealReliefShape', form);
    setText('#dealReliefSectors', `U.S. relief: ${listRelief(usLeaders)}. Canada relief: ${listRelief(caLeaders)}.`);
    updatePending();
  }

  function renderSectorLeaders(leaders) {
    if (!leaders?.length) return '';
    return `<div class="impact-sector-members">${leaders.map(item =>
      `<span><b>${esc(item.name)}</b><em>${signedDeal(item.value)}</em></span>`).join('')}</div>`;
  }

  function renderImpactGroups(groups) {
    const root = document.querySelector('#impactLedgerGroups');
    if (!root) return;
    root.innerHTML = (groups || []).map(group => `<section class="impact-family" data-family="${esc(group.id)}">
      <div class="impact-family-head"><div><i class="impact-family-dot"></i><span><small>${esc(group.kicker)}</small><b>${esc(group.label)}</b></span></div><span class="impact-family-count">${group.items.length} members</span></div>
      <div>${group.items.map(item => `<div class="impact-member"${item.panel?` data-panel="${esc(item.panel)}"`:''}${item.series?` data-series="${esc(item.series)}"`:''}${item.sectorMetric?` data-sector-metric="${esc(item.sectorMetric)}"`:''}>
        <span class="impact-member-label">${esc(item.label)}</span><b class="impact-member-value ${esc(item.tone || 'neutral')}">${esc(item.value)}</b><small class="impact-member-note">${esc(item.note)}</small>${renderSectorLeaders(item.leaders)}</div>`).join('')}</div>
    </section>`).join('');
  }

  function renderOutcomeContext() {
    installOverview();
    const scenario = currentScenario();
    const baseline = matchedBaseline(scenario);
    const context = {...evaluatedDealContext(), sectorCountry:activeSectorCountry()};
    if (!scenario) {
      setText('#impactLedgerPackage', 'Waiting for evaluation…');
      return;
    }
    const lead = leadingScenario();
    const leading = !!lead && scenario.id === lead.id;
    setText('#impactLedgerPackage', scenario.name || scenario.id || 'Inspected package');
    setText('#attributionPackage', scenario.name || scenario.id || 'Inspected package');
    setText('#attributionPackageKind', leading ? 'Leading searched package' : 'Inspected alternative package');
    setText('#attributionScores', `CA ${fmtDeal(scenario.canadaScore,0)} · US ${fmtDeal(scenario.usScore,0)}`);
    setText('#attributionSectorCountry', `${context.sectorCountry === 'us' ? 'United States' : 'Canada'} · 20 sectors`);
    renderImpactGroups(buildImpactGroups(scenario, baseline, context));
    renderOutcomeStatus();
  }

  function renderOutcomeStatus() {
    const state = document.querySelector('#impactLedgerState');
    if (!state) return;
    const staged = window.EvaluationRunController?.state?.().staged === true;
    state.classList.toggle('staged', staged);
    state.textContent = staged
      ? 'Controls changed · metrics remain tied to the last completed evaluation until the updated deal finishes.'
      : 'Completed model outcome · matched no-tariff deltas where available · not a causal estimate or forecast.';
  }

  function captureEvaluation() {
    const next = evaluatedSnapshot();
    if (next) evaluated = next;
    renderOverview();
    renderOutcomeContext();
  }

  function updatePending() {
    const pending = document.querySelector('#dealPending');
    if (!pending) return;
    const staged = window.EvaluationRunController?.state?.().staged === true;
    pending.hidden = !staged;
  }

  function drillFromLedger(target) {
    const member = target.closest?.('.impact-member[data-panel]');
    if (!member) return;
    const panel = member.dataset.panel;
    if (member.dataset.series) {
      document.querySelector(`.tabs button[data-series="${member.dataset.series}"]`)?.click();
    }
    if (member.dataset.sectorMetric) {
      const select = document.querySelector('#sectorMetric');
      if (select) {
        select.value = member.dataset.sectorMetric;
        select.dispatchEvent(new Event('change', {bubbles:true}));
      }
    }
    openPanel(panel);
  }

  function bindOverview() {
    installOverview();
    const cards = document.querySelector('#cards');
    if (cards && typeof MutationObserver === 'function')
      new MutationObserver(() => setTimeout(captureEvaluation, 0)).observe(cards, {childList:true});
    document.addEventListener('input', () => setTimeout(() => { updatePending(); renderOutcomeStatus(); }, 0), true);
    document.addEventListener('change', event => {
      setTimeout(() => { updatePending(); renderOutcomeStatus(); }, 0);
      if (event.target?.matches?.('#sectorMetric')) setTimeout(renderOutcomeContext, 0);
    }, true);
    document.addEventListener('click', event => {
      if (event.target?.closest?.('#run')) {
        const pending = document.querySelector('#dealPending');
        if (pending) pending.hidden = true;
      }
      if (event.target?.closest?.('.card,.country-switch button')) setTimeout(renderOutcomeContext, 0);
      drillFromLedger(event.target);
    }, true);
    setTimeout(captureEvaluation, 0);
  }

  window.DealOverview = {dealForm, reliefLeaders};
  window.JointDashboardOutcomes = {buildImpactGroups, sectorImpactSummary, evaluatedDealContext};
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', bindOverview);
  else bindOverview();
})();
