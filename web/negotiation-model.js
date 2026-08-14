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

// Executive trade-deal overview. This is intentionally the only negotiation-model
// dashboard surface: deal terms and outcomes first, process plumbing elsewhere.
(() => {
  'use strict';

  let evaluated = null;
  let initialized = false;
  const number = (value, fallback=0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const fmtDeal = (value, digits=1) => number(value).toFixed(digits);
  const signedDeal = (value, suffix='%') => `${number(value) >= 0 ? '+' : ''}${fmtDeal(value)}${suffix}`;
  const averageDeal = values => Array.isArray(values) && values.length
    ? values.reduce((sum, value) => sum + number(value), 0) / values.length
    : 0;

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

  function evaluatedSnapshot() {
    let best = null, rec = null;
    try {
      best = result?.scenarios?.[0] || null;
      rec = result?.recommendation || null;
    } catch (_) {}
    if (!best) return null;
    const state = window.EvaluationController?.state?.() || {};
    const controls = state.lastEvaluatedControls || {};
    const usCoverage = typeof positions !== 'undefined' && Array.isArray(positions?.us)
      ? [...positions.us] : [];
    const canadaCoverage = typeof positions !== 'undefined' && Array.isArray(positions?.canada)
      ? [...positions.canada] : [];
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

  function installStyles() {
    if (document.querySelector('#dealOverviewStyles')) return;
    const style = document.createElement('style');
    style.id = 'dealOverviewStyles';
    style.textContent = `
      .decision-overview-compat{display:none!important}
      .deal-overview{display:grid;gap:14px}
      .deal-hero{display:grid;grid-template-columns:minmax(0,1fr) 240px;gap:22px;align-items:start;padding:24px;background:linear-gradient(128deg,#173d35 0%,#214c42 65%,#2b5d50 100%);color:#fff;box-shadow:0 12px 30px #173c3422}
      .deal-hero .eyebrow{color:#b7d0c8}.deal-hero h1{margin:7px 0 8px;font-size:34px;line-height:1.05;letter-spacing:-.035em}.deal-hero p{margin:0;max-width:760px;color:#c8d9d3;font-size:11px;line-height:1.55}
      .deal-verdict{border-left:1px solid #ffffff33;padding-left:18px;text-align:right}.deal-verdict span{display:block;font-size:8px;letter-spacing:.13em;text-transform:uppercase;color:#b7d0c8;font-weight:700}.deal-verdict b{display:block;margin-top:7px;font-size:20px}.deal-verdict small{display:block;margin-top:6px;color:#c8d9d3;font-size:8px;line-height:1.45}
      .deal-pending{display:flex;justify-content:space-between;gap:14px;align-items:center;padding:9px 12px;background:#fff4de;border-left:3px solid #c98a20;color:#72501d;font-size:9px}.deal-pending[hidden]{display:none!important}.deal-pending button{border:1px solid #c98a20;background:#fff8e8;color:#72501d;padding:6px 9px}
      .deal-section{background:#fff;border:1px solid var(--line)}.deal-section-head{display:flex;justify-content:space-between;gap:14px;align-items:end;padding:14px 16px 0}.deal-section-head h3{margin:4px 0 0;font-size:14px}.deal-section-head p{margin:0;max-width:620px;color:#72807a;font-size:9px;line-height:1.45}
      .deal-terms{display:grid;grid-template-columns:repeat(4,1fr);padding:14px 16px 16px}.deal-term{padding:13px 14px;background:#f6f3ed;border-right:1px solid #e2ded5}.deal-term:last-child{border-right:0}.deal-term span,.deal-metric span,.deal-quality span{display:block;font-size:7px;text-transform:uppercase;letter-spacing:.08em;color:#7a8782;font-weight:700}.deal-term b{display:block;margin-top:5px;font-size:22px}.deal-term small{display:block;margin-top:4px;color:#7a8782;font-size:8px;line-height:1.4}
      .deal-outcomes{display:grid;grid-template-columns:repeat(6,1fr);padding:14px 16px 16px}.deal-metric{padding:12px;border-right:1px solid var(--line)}.deal-metric:last-child{border-right:0}.deal-metric b{display:block;margin-top:5px;font-size:18px}.deal-metric small{display:block;margin-top:4px;color:#87918d;font-size:8px}
      .deal-quality-grid{display:grid;grid-template-columns:1fr 1fr 1.25fr;gap:0;padding:14px 16px 16px}.deal-quality{padding:13px 15px;border-right:1px solid var(--line)}.deal-quality:last-child{border-right:0}.deal-quality b{display:block;margin-top:5px;font-size:16px}.deal-quality p{margin:5px 0 0;color:#697670;font-size:9px;line-height:1.45}
      .deal-actions{display:flex;gap:7px;flex-wrap:wrap;padding:0 16px 16px}.deal-actions button{border:1px solid var(--line);background:#fff;padding:8px 10px;color:var(--ink)}.deal-actions button:hover{border-color:var(--green);color:var(--green)}
      @media(max-width:980px){.deal-hero{grid-template-columns:1fr}.deal-verdict{text-align:left;border-left:0;border-top:1px solid #ffffff33;padding:12px 0 0}.deal-terms{grid-template-columns:1fr 1fr}.deal-term:nth-child(2){border-right:0}.deal-outcomes{grid-template-columns:repeat(3,1fr)}.deal-metric:nth-child(3){border-right:0}.deal-quality-grid{grid-template-columns:1fr}.deal-quality{border-right:0;border-bottom:1px solid var(--line)}}
      @media(max-width:620px){.deal-hero h1{font-size:27px}.deal-terms,.deal-outcomes{grid-template-columns:1fr 1fr}.deal-term,.deal-metric{border-right:0;border-bottom:1px solid var(--line)}}`;
    document.head.appendChild(style);
  }

  function installOverview() {
    if (initialized) return;
    const panel = document.querySelector('details[data-dashboard-panel="decision-overview"]');
    const body = panel?.querySelector('.dashboard-panel-body');
    if (!panel || !body) return;
    initialized = true;
    installStyles();
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

  function captureEvaluation() {
    const next = evaluatedSnapshot();
    if (next) evaluated = next;
    renderOverview();
  }

  function updatePending() {
    const pending = document.querySelector('#dealPending');
    if (!pending) return;
    const staged = window.EvaluationRunController?.state?.().staged === true;
    pending.hidden = !staged;
  }

  function bindOverview() {
    installOverview();
    const cards = document.querySelector('#cards');
    if (cards && typeof MutationObserver === 'function')
      new MutationObserver(() => setTimeout(captureEvaluation, 0)).observe(cards, {childList:true});
    document.addEventListener('input', () => setTimeout(updatePending, 0), true);
    document.addEventListener('change', () => setTimeout(updatePending, 0), true);
    document.addEventListener('click', event => {
      if (event.target?.closest?.('#run')) {
        const pending = document.querySelector('#dealPending');
        if (pending) pending.hidden = true;
      }
    }, true);
    setTimeout(captureEvaluation, 0);
  }

  window.DealOverview = {dealForm, reliefLeaders};
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', bindOverview);
  else bindOverview();
})();
