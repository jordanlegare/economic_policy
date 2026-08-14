(function(root, factory) {
  'use strict';
  const api = factory(root);
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.AmbassadorQuickLookWidgets = api;
  if (typeof document !== 'undefined') api.boot();
})(typeof window !== 'undefined' ? window : null, function(root) {
  'use strict';

  const SCREEN = Object.freeze({
    scoreFloor: 50,
    maxScoreGap: 15,
    maxRecessionRisk: 35,
    maxEffectiveTariffGap: 15
  });

  let bound = false;
  const hasNumber = value => Number.isFinite(Number(value));
  const num = (value, fallback=0) => hasNumber(value) ? Number(value) : fallback;
  const fmt = (value, digits=1) => hasNumber(value) ? Number(value).toFixed(digits) : '—';
  const signed = (value, suffix='%', digits=1) => hasNumber(value)
    ? `${Number(value) >= 0 ? '+' : ''}${Number(value).toFixed(digits)}${suffix}` : '—';
  const average = values => Array.isArray(values) && values.length
    ? values.reduce((sum, value) => sum + num(value), 0) / values.length : 0;
  const esc = value => String(value ?? '').replace(/[&<>"']/g, char => ({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
  }[char]));

  function sectorImpactSummary(scenario, country='canada', metric='output') {
    const external = root?.JointDashboardOutcomes?.sectorImpactSummary;
    if (typeof external === 'function') return external(scenario, country, metric);
    const sectors = Array.isArray(scenario?.sectors) ? scenario.sectors : [];
    const values = sectors.map(sector => num(sector?.[country]?.[metric], NaN)).filter(Number.isFinite);
    if (!values.length) return {count:0, negative:0, positive:0, average:0, leaders:[]};
    const negative = values.filter(value => value < 0).length;
    const leaders = sectors.map(sector => ({
      name: sector.name || sector.code || 'Sector',
      code: sector.code || '',
      value: num(sector?.[country]?.[metric], 0)
    })).sort((a,b) => Math.abs(b.value) - Math.abs(a.value)).slice(0,3);
    return {
      count: values.length,
      negative,
      positive: values.length - negative,
      average: values.reduce((sum, value) => sum + value, 0) / values.length,
      leaders
    };
  }

  function resolutionChecks(scenario, context={}) {
    if (!scenario) return [];
    const canadaScore = num(scenario.canadaScore);
    const usScore = num(scenario.usScore);
    const weakest = Math.min(canadaScore, usScore);
    const scoreGap = Math.abs(canadaScore - usScore);
    const recessionRisk = num(scenario.recessionRisk, 100);
    const tariffGap = Math.abs(num(context.usEffective) - num(context.canadaEffective));
    return [
      {
        id:'bilateral-value', label:'Both sides receive modeled value',
        pass: weakest >= SCREEN.scoreFloor,
        detail:`Weakest side ${fmt(weakest,0)}/100 · screen ≥${SCREEN.scoreFloor}`
      },
      {
        id:'growth-floor', label:'Both economies clear the growth floor',
        pass: scenario.sustainedBilateralGrowth === true,
        detail:`Bilateral floor ${fmt(scenario.bilateralGrowthFloor)}%`
      },
      {
        id:'balance', label:'Deal value is reasonably balanced',
        pass: scoreGap <= SCREEN.maxScoreGap,
        detail:`${fmt(scoreGap,0)}-point score gap · screen ≤${SCREEN.maxScoreGap}`
      },
      {
        id:'macro-risk', label:'Macro downside stays contained',
        pass: recessionRisk <= SCREEN.maxRecessionRisk,
        detail:`${fmt(recessionRisk,0)}% recession risk · screen ≤${SCREEN.maxRecessionRisk}%`
      },
      {
        id:'border-symmetry', label:'Border burden is not highly asymmetric',
        pass: tariffGap <= SCREEN.maxEffectiveTariffGap,
        detail:`${fmt(tariffGap)}pp effective-tariff gap · screen ≤${SCREEN.maxEffectiveTariffGap}pp`
      }
    ];
  }

  function resolutionStatus(checks) {
    const passed = (checks || []).filter(check => check.pass).length;
    if (passed >= 5) return {passed, label:'Strong resolution window', tone:'positive'};
    if (passed === 4) return {passed, label:'Resolution window open', tone:'positive'};
    if (passed === 3) return {passed, label:'Narrow resolution window', tone:'caution'};
    return {passed, label:'Material blockers remain', tone:'negative'};
  }

  function sparklinePath(values, width=280, height=60, padding=5) {
    const series = Array.isArray(values) ? values.map(Number).filter(Number.isFinite) : [];
    if (!series.length) return '';
    if (series.length === 1) return `M ${padding} ${height / 2}`;
    const low = Math.min(...series), high = Math.max(...series), range = high - low || 1;
    const x = index => padding + index * (width - 2 * padding) / (series.length - 1);
    const y = value => padding + (high - value) * (height - 2 * padding) / range;
    return series.map((value,index) => `${index ? 'L' : 'M'} ${x(index).toFixed(2)} ${y(value).toFixed(2)}`).join(' ');
  }

  function delta(scenario, baseline, field, suffix=' pp') {
    if (!hasNumber(scenario?.[field]) || !hasNumber(baseline?.[field])) return 'No matched delta';
    return `${signed(num(scenario[field]) - num(baseline[field]), suffix, 1)} vs no-tariff`;
  }

  function sectorPressureLeaders(scenario, limit=4) {
    const sectors = Array.isArray(scenario?.sectors) ? scenario.sectors : [];
    const signals = [];
    for (const sector of sectors) {
      for (const country of ['canada','us']) {
        const value = num(sector?.[country]?.output, NaN);
        if (!Number.isFinite(value)) continue;
        signals.push({
          name: sector.name || sector.code || 'Sector',
          country: country === 'canada' ? 'Canada' : 'U.S.',
          value
        });
      }
    }
    return signals.sort((a,b) => Math.abs(b.value) - Math.abs(a.value)).slice(0,limit);
  }

  function buildAmbassadorWidgets(scenario, baseline, context={}) {
    if (!scenario) return [];
    const checks = resolutionChecks(scenario, context);
    const resolution = resolutionStatus(checks);
    const canadaScore = num(scenario.canadaScore), usScore = num(scenario.usScore);
    const weakest = Math.min(canadaScore, usScore), scoreGap = Math.abs(canadaScore-usScore);
    const usCoverage = average(context.usCoverage), canadaCoverage = average(context.canadaCoverage);
    const usRelief = 100 - usCoverage, canadaRelief = 100 - canadaCoverage;
    const tariffGap = Math.abs(num(context.usEffective) - num(context.canadaEffective));
    const canadaOutput = sectorImpactSummary(scenario, 'canada', 'output');
    const usOutput = sectorImpactSummary(scenario, 'us', 'output');
    const canadaJobs = sectorImpactSummary(scenario, 'canada', 'jobs');
    const usJobs = sectorImpactSummary(scenario, 'us', 'jobs');
    const sectorLeaders = sectorPressureLeaders(scenario);
    const terminalRate = Array.isArray(scenario.rates) && scenario.rates.length
      ? scenario.rates[scenario.rates.length - 1] : null;

    return [
      {
        id:'resolution', kind:'resolution', featured:true,
        kicker:'Possible resolution', title:'Resolution window',
        headline:resolution.label, tone:resolution.tone,
        summary:`${resolution.passed}/5 transparent quick-look conditions clear`,
        checks, panel:'decision-overview'
      },
      {
        id:'balance', kind:'balance', kicker:'Fairness at a glance', title:'Deal balance',
        headline:`CA ${fmt(canadaScore,0)} · US ${fmt(usScore,0)}`,
        summary:`Weakest side ${fmt(weakest,0)}/100 · ${fmt(scoreGap,0)}-point gap`,
        bars:[
          {label:'Canada', value:canadaScore, max:100},
          {label:'United States', value:usScore, max:100}
        ],
        metrics:[
          {label:'Weakest-side value', value:`${fmt(weakest,0)}/100`},
          {label:'Growth protection', value:scenario.sustainedBilateralGrowth ? 'Protected' : 'Not protected'}
        ],
        panel:'strategies'
      },
      {
        id:'growth', kind:'spark', kicker:'Prosperity', title:'Growth protection',
        headline:`CA ${fmt(scenario.growth)}% · US ${fmt(scenario.usGrowth)}%`,
        summary:`Bilateral floor ${fmt(scenario.bilateralGrowthFloor)}% · recession risk ${fmt(scenario.recessionRisk,0)}%`,
        metrics:[
          {label:'Canada vs no-tariff', value:delta(scenario,baseline,'growth')},
          {label:'U.S. vs no-tariff', value:delta(scenario,baseline,'usGrowth')}
        ],
        series:scenario.growthPath, seriesLabel:'Canada GDP path', panel:'projection', drillSeries:'growthPath'
      },
      {
        id:'border', kind:'balance', kicker:'Border terms', title:'Tariff burden',
        headline:`US ${fmt(context.usEffective)}% · CA ${fmt(context.canadaEffective)}%`,
        summary:`${fmt(tariffGap)}pp coverage-adjusted tariff gap`,
        bars:[
          {label:'U.S. burden', value:num(context.usEffective), max:Math.max(25,num(context.usEffective),num(context.canadaEffective))},
          {label:'Canada burden', value:num(context.canadaEffective), max:Math.max(25,num(context.usEffective),num(context.canadaEffective))}
        ],
        metrics:[
          {label:'U.S. sector relief', value:`${fmt(usRelief,0)}pt`},
          {label:'Canada sector relief', value:`${fmt(canadaRelief,0)}pt`}
        ],
        panel:'decision-overview'
      },
      {
        id:'trade', kind:'spark', kicker:'Commercial exchange', title:'Trade momentum',
        headline:`CA ${signed(scenario.exports)} · US ${signed(scenario.usExportChange)}`,
        summary:hasNumber(scenario.tradeBalanceGapUsd)
          ? `US$${fmt(scenario.tradeBalanceGapUsd)}B bilateral accounting gap` : 'Bilateral accounting gap unavailable',
        metrics:[
          {label:'Canada exports vs no-tariff', value:delta(scenario,baseline,'exports')},
          {label:'U.S. exports vs no-tariff', value:delta(scenario,baseline,'usExportChange')}
        ],
        series:scenario.exportPath, seriesLabel:'Canada export path', panel:'projection', drillSeries:'exportPath'
      },
      {
        id:'households', kind:'spark', kicker:'People & prices', title:'Household pressure',
        headline:`Inflation ${fmt(scenario.inflation)}%`,
        summary:`Cost of living ${fmt(scenario.costOfLiving)}% · real income ${signed(scenario.realIncomeGrowth)}`,
        metrics:[
          {label:'Inflation vs no-tariff', value:delta(scenario,baseline,'inflation')},
          {label:'Terminal policy rate', value:hasNumber(terminalRate)?`${fmt(terminalRate,2)}%`:'—'}
        ],
        series:scenario.inflationPath, seriesLabel:'Inflation path', panel:'projection', drillSeries:'inflationPath'
      },
      {
        id:'fiscal', kind:'plain', kicker:'Public finance', title:'Tariff take',
        headline:`US$${fmt(scenario.usTariffRevenueUsd)}B · C$${fmt(scenario.canadaTariffRevenueCad)}B`,
        summary:'Annualized post-elasticity tariff receipts',
        metrics:[
          {label:'Canada debt metric', value:hasNumber(scenario.debt)?`${fmt(scenario.debt)}%`:'—'},
          {label:'Fiscal impulse', value:hasNumber(scenario.fiscal)?signed(scenario.fiscal):'—'}
        ],
        panel:'fiscal-ledger'
      },
      {
        id:'sectors', kind:'sectors', kicker:'Complete economy', title:'20-sector reach',
        headline:`CA ${canadaOutput.negative}/20 · US ${usOutput.negative}/20 output-negative`,
        summary:'Breadth of modeled deal effects across the full NAICS economy',
        metrics:[
          {label:'Canada output', value:`${canadaOutput.positive} protected · ${canadaOutput.negative} negative`},
          {label:'U.S. output', value:`${usOutput.positive} protected · ${usOutput.negative} negative`},
          {label:'Canada jobs', value:`${canadaJobs.positive} protected · ${canadaJobs.negative} negative`},
          {label:'U.S. jobs', value:`${usJobs.positive} protected · ${usJobs.negative} negative`}
        ],
        leaders:sectorLeaders, panel:'sectors', sectorMetric:'output'
      }
    ];
  }

  function currentScenario() {
    try {
      if (typeof selected !== 'undefined' && selected) return selected;
      if (typeof result !== 'undefined') return result?.scenarios?.[0] || null;
    } catch (_) {}
    return null;
  }

  function matchedBaseline(scenario) {
    if (!scenario) return null;
    try {
      if (typeof noTariff === 'undefined') return null;
      return noTariff?.scenarios?.find(item => item.id === scenario.id)
        || noTariff?.scenarios?.[0] || null;
    } catch (_) { return null; }
  }

  function browserDealContext() {
    const external = root?.JointDashboardOutcomes?.evaluatedDealContext;
    let context = typeof external === 'function' ? external() : {};
    const country = document.querySelector('.country-switch button.active')?.dataset.country || 'canada';
    context = {...context, sectorCountry:country};
    if (!Array.isArray(context.usCoverage)) {
      try { context.usCoverage = typeof positions !== 'undefined' ? [...positions.us] : []; } catch (_) { context.usCoverage=[]; }
    }
    if (!Array.isArray(context.canadaCoverage)) {
      try { context.canadaCoverage = typeof positions !== 'undefined' ? [...positions.canada] : []; } catch (_) { context.canadaCoverage=[]; }
    }
    if (!hasNumber(context.usTariff)) context.usTariff = num(document.querySelector('#usTariff')?.value);
    if (!hasNumber(context.canadaTariff)) context.canadaTariff = num(document.querySelector('#retaliatoryTariff')?.value);
    if (!hasNumber(context.usEffective)) context.usEffective = num(context.usTariff) * average(context.usCoverage) / 100;
    if (!hasNumber(context.canadaEffective)) context.canadaEffective = num(context.canadaTariff) * average(context.canadaCoverage) / 100;
    return context;
  }

  function renderBars(bars) {
    if (!bars?.length) return '';
    return `<div class="ambassador-bars">${bars.map(bar => {
      const max = Math.max(num(bar.max,100),1);
      const width = Math.max(0,Math.min(100,num(bar.value)/max*100));
      return `<div><span><b>${esc(bar.label)}</b><em>${fmt(bar.value,1)}</em></span><i><u style="width:${width.toFixed(1)}%"></u></i></div>`;
    }).join('')}</div>`;
  }

  function renderMetrics(metrics) {
    if (!metrics?.length) return '';
    return `<div class="ambassador-widget-metrics">${metrics.map(metric =>
      `<div><span>${esc(metric.label)}</span><b>${esc(metric.value)}</b>${metric.note?`<small>${esc(metric.note)}</small>`:''}</div>`
    ).join('')}</div>`;
  }

  function renderSparkline(widget) {
    const path = sparklinePath(widget.series);
    if (!path) return '';
    const values = widget.series.map(Number).filter(Number.isFinite);
    return `<div class="ambassador-spark"><svg viewBox="0 0 280 60" role="img" aria-label="${esc(widget.seriesLabel || widget.title)}"><path d="${path}"></path></svg><span>${fmt(values[0])}</span><span>${fmt(values[values.length-1])}</span></div>`;
  }

  function renderResolution(widget) {
    return `<div class="ambassador-resolution"><div class="ambassador-resolution-score ${esc(widget.tone)}"><strong>${widget.checks.filter(check=>check.pass).length}/5</strong><span>conditions clear</span></div><div class="ambassador-resolution-checks">${widget.checks.map(check =>
      `<div class="${check.pass?'pass':'block'}"><i>${check.pass?'✓':'!'}</i><span><b>${esc(check.label)}</b><small>${esc(check.detail)}</small></span></div>`
    ).join('')}</div></div>`;
  }

  function renderSectorLeaders(leaders) {
    if (!leaders?.length) return '';
    return `<div class="ambassador-sector-signals"><span>Largest output signals</span>${leaders.map(item =>
      `<div><b>${esc(item.name)}</b><small>${esc(item.country)}</small><em class="${item.value<0?'negative':'positive'}">${signed(item.value)}</em></div>`
    ).join('')}</div>`;
  }

  function renderWidget(widget) {
    return `<article class="ambassador-widget ${widget.featured?'featured':''}" data-widget="${esc(widget.id)}" data-panel="${esc(widget.panel || '')}"${widget.drillSeries?` data-series="${esc(widget.drillSeries)}"`:''}${widget.sectorMetric?` data-sector-metric="${esc(widget.sectorMetric)}"`:''} tabindex="0">
      <div class="ambassador-widget-head"><div><span>${esc(widget.kicker)}</span><h3>${esc(widget.title)}</h3></div><i>↗</i></div>
      <strong class="ambassador-widget-headline ${esc(widget.tone || '')}">${esc(widget.headline)}</strong>
      <p>${esc(widget.summary || '')}</p>
      ${widget.kind==='resolution'?renderResolution(widget):''}
      ${renderBars(widget.bars)}
      ${renderSparkline(widget)}
      ${renderMetrics(widget.metrics)}
      ${renderSectorLeaders(widget.leaders)}
    </article>`;
  }

  function installBoard() {
    if (document.querySelector('#ambassadorQuickLook')) return true;
    const content = document.querySelector('#dashboardView .content');
    const tools = document.querySelector('#dashboardView .dashboard-stack-tools');
    if (!content || !tools) return false;
    const section = document.createElement('section');
    section.id = 'ambassadorQuickLook';
    section.className = 'ambassador-quick-look';
    section.innerHTML = `<div class="ambassador-quick-look-head"><div><div class="eyebrow">Ambassador quick look</div><h2>Deal-resolution widgets</h2><p>Preconfigured from the currently selected deal so both delegations can read the same headline economics before opening the detailed model panels.</p></div><div class="ambassador-quick-look-state"><span id="ambassadorQuickLookFreshness">Current evaluated deal</span><button id="ambassadorRunUpdated" type="button" hidden>Run updated deal →</button></div></div><div id="ambassadorQuickLookPackage" class="ambassador-quick-look-package">Waiting for the selected deal…</div><div id="ambassadorWidgetGrid" class="ambassador-widget-grid"></div><div class="ambassador-widget-foot"><b>Current /api/evaluate payload</b><span>Selected package + matched no-tariff comparison. The resolution screen is a transparent UI heuristic—not a political acceptance probability, forecast, or legal conclusion.</span></div>`;
    const banner = document.querySelector('#dealAttributionBanner');
    if (banner) banner.insertAdjacentElement('afterend', section);
    else content.insertBefore(section, tools);
    return true;
  }

  function freshness() {
    const state = document.querySelector('#ambassadorQuickLookFreshness');
    const button = document.querySelector('#ambassadorRunUpdated');
    if (!state) return;
    const staged = root?.EvaluationRunController?.state?.().staged === true;
    state.classList.toggle('staged', staged);
    state.textContent = staged ? 'Inputs changed · showing last evaluated deal' : 'Current evaluated deal';
    if (button) button.hidden = !staged;
  }

  function render() {
    if (!installBoard()) return;
    const scenario = currentScenario();
    const grid = document.querySelector('#ambassadorWidgetGrid');
    if (!scenario || !grid) {
      const packageNode = document.querySelector('#ambassadorQuickLookPackage');
      if (packageNode) packageNode.textContent = 'Waiting for the selected deal…';
      freshness();
      return;
    }
    const baseline = matchedBaseline(scenario);
    const context = browserDealContext();
    const widgets = buildAmbassadorWidgets(scenario, baseline, context);
    grid.innerHTML = widgets.map(renderWidget).join('');
    const packageNode = document.querySelector('#ambassadorQuickLookPackage');
    if (packageNode) packageNode.textContent = `Selected deal · ${scenario.name || scenario.id || 'modeled package'} · Canada ${fmt(scenario.canadaScore,0)}/100 · U.S. ${fmt(scenario.usScore,0)}/100`;
    freshness();
  }

  function drill(widget) {
    if (!widget) return;
    const series = widget.dataset.series;
    if (series) document.querySelector(`.tabs button[data-series="${series}"]`)?.click();
    const metric = widget.dataset.sectorMetric;
    if (metric) {
      const select = document.querySelector('#sectorMetric');
      if (select) {
        select.value = metric;
        select.dispatchEvent(new Event('change', {bubbles:true}));
      }
    }
    const panel = widget.dataset.panel;
    if (!panel) return;
    const details = document.querySelector(`details[data-dashboard-panel="${panel}"]`);
    if (!details) return;
    details.open = true;
    details.scrollIntoView({behavior:'smooth',block:'start'});
  }

  function bind() {
    if (bound) return;
    bound = true;
    const cards = document.querySelector('#cards');
    if (cards && typeof MutationObserver === 'function')
      new MutationObserver(() => setTimeout(render,0)).observe(cards,{childList:true});
    document.addEventListener('click', event => {
      if (event.target?.closest?.('#ambassadorRunUpdated')) {
        document.querySelector('#run')?.click();
        return;
      }
      const widget = event.target?.closest?.('.ambassador-widget[data-panel]');
      if (widget) drill(widget);
      if (event.target?.closest?.('.card,.country-switch button')) setTimeout(render,0);
    }, true);
    document.addEventListener('keydown', event => {
      if ((event.key === 'Enter' || event.key === ' ') && event.target?.matches?.('.ambassador-widget[data-panel]')) {
        event.preventDefault();
        drill(event.target);
      }
    });
    document.addEventListener('input', () => setTimeout(freshness,0), true);
    document.addEventListener('change', event => {
      setTimeout(freshness,0);
      if (event.target?.matches?.('#sectorMetric')) setTimeout(render,0);
    }, true);
  }

  function boot() {
    const start = () => setTimeout(() => { installBoard(); bind(); render(); }, 0);
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
    else start();
  }

  return {
    SCREEN,
    resolutionChecks,
    resolutionStatus,
    sparklinePath,
    sectorImpactSummary,
    buildAmbassadorWidgets,
    boot,
    render
  };
});
