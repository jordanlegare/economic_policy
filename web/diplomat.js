(() => {
  'use strict';

  const LIMITS_KEY = 'canada-policy-diplomatic-redlines-v1';
  const NOTES_KEY = 'canada-policy-diplomatic-notes-v1';
  const defaults = { minCanada: 45, minUs: 45, maxRecession: 40, minGrowth: 0, maxInflation: 3.5 };
  let limits = loadJson(LIMITS_KEY, defaults);

  function loadJson(key, fallback) {
    try { return {...fallback, ...JSON.parse(localStorage.getItem(key) || '{}')}; }
    catch (_) { return {...fallback}; }
  }
  function saveJson(key, value) { try { localStorage.setItem(key, JSON.stringify(value)); } catch (_) {} }
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const n = value => Number(value || 0);
  const f = (value, digits=1) => n(value).toFixed(digits);
  const canadaScore = scenario => n(scenario.canadaScore);

  function checks(scenario) {
    const detail = [
      ['Canada floor', canadaScore(scenario) >= limits.minCanada, `${f(canadaScore(scenario),0)} ≥ ${limits.minCanada}`],
      ['U.S. floor', n(scenario.usScore) >= limits.minUs, `${f(scenario.usScore,0)} ≥ ${limits.minUs}`],
      ['Recession ceiling', n(scenario.recessionRisk) <= limits.maxRecession, `${f(scenario.recessionRisk,0)}% ≤ ${limits.maxRecession}%`],
      ['Bilateral growth floor', n(scenario.bilateralGrowthFloor) >= limits.minGrowth, `${f(scenario.bilateralGrowthFloor)}% ≥ ${limits.minGrowth}%`],
      ['Inflation ceiling', n(scenario.inflation) <= limits.maxInflation, `${f(scenario.inflation)}% ≤ ${limits.maxInflation}%`]
    ];
    return {pass: detail.every(x => x[1]), detail};
  }

  function packageSet() {
    if (typeof result === 'undefined' || !result?.scenarios?.length) return null;
    const scenarios = result.scenarios;
    const preferred = scenarios[0];
    const remaining = scenarios.filter(s => s.id !== preferred.id);
    const bridge = [...remaining].sort((a,b) => {
      const fairness = s => Math.min(canadaScore(s), n(s.usScore)) - .22 * Math.abs(canadaScore(s)-n(s.usScore)) - .035*n(s.recessionRisk);
      return fairness(b) - fairness(a);
    })[0] || preferred;
    const compliant = remaining.filter(s => s.id !== bridge.id && checks(s).pass);
    const fallback = compliant[0] || scenarios.find(s => s.id === 'statusquo') || remaining.find(s => s.id !== bridge.id) || preferred;
    return {preferred, bridge, fallback};
  }

  function packageCard(kind, label, scenario, why) {
    const gate = checks(scenario);
    return `<article class="package-lane ${kind}" data-diplomat-scenario="${esc(scenario.id)}">
      <div class="package-type">${esc(label)}</div>
      <h3>${esc(scenario.name)}</h3>
      <p>${esc(why)}</p>
      <div class="package-scores">
        <div><span>Canada</span><b>${f(canadaScore(scenario),0)}</b></div>
        <div><span>United States</span><b>${f(scenario.usScore,0)}</b></div>
        <div><span>Shared growth floor</span><b>${f(scenario.bilateralGrowthFloor)}%</b></div>
      </div>
      <div class="package-check ${gate.pass?'pass':'fail'}">${gate.pass?'✓ Clears current red lines':'⚠ Breaches at least one current red line'} · click to inspect</div>
    </article>`;
  }

  function concessionRows() {
    if (!result?.recommendation) return [];
    const us = result.recommendation.usSectorCoverage || [];
    const ca = result.recommendation.canadaSectorCoverage || [];
    const names = typeof sectorNames !== 'undefined' ? sectorNames : us.map((_,i)=>`Sector ${i+1}`);
    return names.map((name, i) => {
      const usRelief = 100 - n(us[i]);
      const caRelief = 100 - n(ca[i]);
      const total = usRelief + caRelief;
      let posture = 'Hold';
      if (usRelief > 8 && caRelief > 8) posture = 'Mutual move';
      else if (usRelief > caRelief + 8) posture = 'U.S. move';
      else if (caRelief > usRelief + 8) posture = 'Canada move';
      return {name, usRelief, caRelief, total, posture};
    }).sort((a,b) => b.total - a.total);
  }

  function talkingPoints(packages) {
    const best = packages.preferred;
    const rec = result.recommendation;
    const concessions = concessionRows();
    const lead = concessions[0];
    const gap = Math.abs(canadaScore(best)-n(best.usScore));
    const floor = n(best.bilateralGrowthFloor);
    const points = [
      `Lead on shared prosperity: the preferred package keeps the weakest modeled Canada–U.S. quarterly growth outcome at ${f(floor)}%.`,
      `Frame fairness explicitly: Canada scores ${f(canadaScore(best),0)}/100 and the United States ${f(best.usScore,0)}/100, a ${f(gap,0)}-point modeled gap.`,
      `Acknowledge risk rather than oversell precision: modeled recession exposure is ${f(best.recessionRisk,0)}% and terminal inflation is ${f(best.inflation)}%.`,
      lead ? `Use ${lead.name} as an early test of reciprocity: modeled relief implies roughly ${f(lead.usRelief,0)} points of U.S. coverage relief and ${f(lead.caRelief,0)} points of Canadian coverage relief from full coverage.` : 'Sequence sector concessions gradually and preserve reciprocal movement.',
      `Keep ${packages.fallback.name} prepared as the fallback if the preferred package cannot clear political or economic red lines.`
    ];
    if (rec?.gdpGrowthFloor !== undefined) points.push(`Describe the ${f(rec.gdpGrowthFloor)}% searched GDP floor as a negotiation guardrail, not as a forecast guarantee.`);
    return points;
  }

  function institutionalizeLabels() {
    const live = document.querySelector('.party-live-strip b');
    if (live && /LeBlanc|Greer/i.test(live.textContent)) live.innerHTML = '<i></i> Canada ↔ U.S. delegations connected';
    const title = document.querySelector('#partyTitle');
    if (title && /LeBlanc/i.test(title.textContent)) title.textContent = 'Canada delegation trade table';
    else if (title && /Greer/i.test(title.textContent)) title.textContent = 'U.S. delegation trade table';
    const delegation = document.querySelector('#delegationName');
    if (delegation && /LeBlanc/i.test(delegation.textContent)) delegation.textContent = 'Canada delegation';
    else if (delegation && /Greer/i.test(delegation.textContent)) delegation.textContent = 'U.S. delegation';
  }

  function renderStatus(packages) {
    const scenarios = result.scenarios;
    const top = scenarios[0], next = scenarios[1];
    const margin = next ? n(top.score)-n(next.score) : 0;
    const robustness = margin >= 8 ? 'Clear model separation' : margin >= 3 ? 'Moderate separation' : 'Close alternatives';
    const passCount = scenarios.slice(0,5).filter(s => checks(s).pass).length;
    const weakest = Math.min(canadaScore(packages.preferred), n(packages.preferred.usScore));
    document.querySelector('#diplomatRobustness').textContent = robustness;
    document.querySelector('#diplomatRobustnessNote').textContent = next ? `Top-two score spread ${f(margin)} points; this is a decision-robustness cue, not statistical confidence.` : 'Single result available.';
    document.querySelector('#diplomatFairness').textContent = `${f(weakest,0)}/100 weakest party`;
    document.querySelector('#diplomatFairnessNote').textContent = `Canada ${f(canadaScore(packages.preferred),0)} · U.S. ${f(packages.preferred.usScore,0)}`;
    document.querySelector('#diplomatRedlineStatus').textContent = `${passCount} of top 5 clear`;
    document.querySelector('#diplomatRedlineNote').textContent = 'Against your locally saved negotiation red lines.';
    document.querySelector('#diplomatGrowth').textContent = `${f(packages.preferred.bilateralGrowthFloor)}%`;
    document.querySelector('#diplomatGrowthNote').textContent = 'Weakest quarterly Canada/U.S. growth path in the preferred package.';
  }

  function renderPackages(packages) {
    document.querySelector('#diplomatPackages').innerHTML = [
      packageCard('preferred','Preferred package',packages.preferred,'Highest-ranked package under the current model, bilateral allocation and sector search.'),
      packageCard('bridge','Bridge package',packages.bridge,'Alternative chosen for balance between the two parties, useful when the leading package is politically difficult.'),
      packageCard('fallback','Fallback package',packages.fallback,'Best available fallback that clears your current red lines where possible; otherwise the status-quo reference.')
    ].join('');
  }

  function renderRedlines(packages) {
    const gate = checks(packages.preferred);
    const failed = gate.detail.filter(x => !x[1]);
    const target = document.querySelector('#redlineSummary');
    target.classList.toggle('alert', failed.length > 0);
    target.innerHTML = failed.length
      ? `<strong>${failed.length} red line${failed.length===1?'':'s'} breached.</strong><br>${failed.map(x=>`${esc(x[0])}: ${esc(x[2])}`).join('<br>')}`
      : '<strong>Preferred package clears all current red lines.</strong><br>Use these thresholds as political guardrails, not model calibration knobs.';
  }

  function renderConcessions() {
    const rows = concessionRows().slice(0,7);
    document.querySelector('#concessionList').innerHTML = rows.map(row => {
      const cls = row.posture === 'Hold' ? 'hold' : row.posture === 'Mutual move' ? '' : 'ask';
      return `<div class="concession-row"><div><b>${esc(row.name)}</b><small>${esc(row.posture)}</small></div>
        <span class="concession-chip ${cls}">U.S. relief ${f(row.usRelief,0)}pt</span>
        <span class="concession-chip ${cls}">CA relief ${f(row.caRelief,0)}pt</span></div>`;
    }).join('') || '<p>No sector recommendation available yet.</p>';
  }

  function renderTalkingPoints(packages) {
    const points = talkingPoints(packages);
    document.querySelector('#diplomatTalkingPoints').innerHTML = points.map(p => `<li>${esc(p)}</li>`).join('');
  }

  function renderMatrix() {
    const scenarios = result.scenarios.slice(0,7);
    document.querySelector('#diplomatMatrixBody').innerHTML = scenarios.map((s,i) => {
      const gate = checks(s);
      return `<tr><td>${i+1}</td><th scope="row">${esc(s.name)}</th><td>${f(canadaScore(s),0)}</td><td>${f(s.usScore,0)}</td><td>${f(s.growth)}%</td><td>${f(s.usGrowth)}%</td><td>${f(s.inflation)}%</td><td>${f(s.recessionRisk,0)}%</td><td class="${gate.pass?'yes':'no'}">${gate.pass?'CLEARS':'BREACH'}</td></tr>`;
    }).join('');
  }

  function briefingText(packages) {
    const best = packages.preferred;
    const gate = checks(best);
    const concessions = concessionRows().slice(0,5);
    const notes = document.querySelector('#diplomatNotes')?.value?.trim();
    const points = talkingPoints(packages);
    return `CANADA–UNITED STATES NEGOTIATION BRIEF\nGenerated ${new Date().toLocaleString()}\n\nSITUATION\nThe model currently ranks “${best.name}” first. Canada modeled score: ${f(canadaScore(best),0)}/100. U.S. modeled score: ${f(best.usScore,0)}/100. Weakest bilateral quarterly growth path: ${f(best.bilateralGrowthFloor)}%. Recession exposure: ${f(best.recessionRisk,0)}%.\n\nRECOMMENDATION\nOpen with ${best.name}. Preserve ${packages.bridge.name} as the bridge package and ${packages.fallback.name} as the fallback. Treat model rankings as structured evidence, not as negotiating instructions.\n\nRED LINES\n${gate.detail.map(x => `${x[1]?'PASS':'BREACH'} — ${x[0]}: ${x[2]}`).join('\n')}\n\nPRIORITY CONCESSION AREAS\n${concessions.map(x => `• ${x.name}: ${x.posture}; U.S. coverage relief ${f(x.usRelief,0)}pt; Canadian coverage relief ${f(x.caRelief,0)}pt.`).join('\n')}\n\nTALKING POINTS\n${points.map(x => `• ${x}`).join('\n')}\n\nDIPLOMAT NOTES\n${notes || 'No additional notes recorded.'}\n\nANALYTICAL CAUTION\nThis is an illustrative scenario comparator. The searched growth floor, scores and package ordering are model outputs, not forecasts, legal commitments or evidence of causal certainty.`;
  }

  function briefingHtml(packages) {
    const best = packages.preferred;
    const gate = checks(best);
    const concessions = concessionRows().slice(0,5);
    const notes = document.querySelector('#diplomatNotes')?.value?.trim();
    const points = talkingPoints(packages);
    return `<div class="briefing-kicker">Working brief · analytical support</div>
      <h1>Canada–United States negotiation brief</h1>
      <div class="briefing-meta">Generated ${esc(new Date().toLocaleString())} · Current model state · Not an official negotiating instruction</div>
      <h2>Decision</h2><div class="briefing-decision"><b>Open with ${esc(best.name)}.</b><p>Keep <b>${esc(packages.bridge.name)}</b> ready as the bridge package and <b>${esc(packages.fallback.name)}</b> as the fallback.</p></div>
      <h2>Situation</h2><p>The preferred package produces a modeled Canada score of <b>${f(canadaScore(best),0)}/100</b> and U.S. score of <b>${f(best.usScore,0)}/100</b>. Its weakest Canada–U.S. quarterly growth outcome is <b>${f(best.bilateralGrowthFloor)}%</b>, terminal inflation is <b>${f(best.inflation)}%</b>, and modeled recession exposure is <b>${f(best.recessionRisk,0)}%</b>.</p>
      <h2>Red lines</h2><ul>${gate.detail.map(x=>`<li><b>${x[1]?'PASS':'BREACH'}</b> — ${esc(x[0])}: ${esc(x[2])}</li>`).join('')}</ul>
      <h2>Priority concession map</h2><ul>${concessions.map(x=>`<li><b>${esc(x.name)}</b>: ${esc(x.posture)}; U.S. coverage relief ${f(x.usRelief,0)} points; Canadian coverage relief ${f(x.caRelief,0)} points.</li>`).join('')}</ul>
      <h2>Suggested talking points</h2><ul>${points.map(x=>`<li>${esc(x)}</li>`).join('')}</ul>
      <h2>Diplomat notes</h2><p>${notes ? esc(notes).replace(/\n/g,'<br>') : '<em>No additional notes recorded.</em>'}</p>
      <div class="briefing-warning"><b>Use with judgment.</b> The package ordering is sensitive to assumptions, risk preferences and the modeled transmission channels. Present the results as a disciplined comparison of trade-offs, not as an oracle.</div>
      <div class="briefing-disclaimer">Illustrative research tool. Not an official Bank of Canada, Government of Canada, or United States government model, forecast, recommendation, negotiating mandate or legal position.</div>`;
  }

  function openBriefing() {
    const packages = packageSet(); if (!packages) return;
    document.querySelector('#briefingSheet').innerHTML = briefingHtml(packages);
    const dialog = document.querySelector('#diplomaticBriefing');
    if (typeof dialog.showModal === 'function') dialog.showModal(); else dialog.setAttribute('open','');
  }

  function copyBriefing() {
    const packages = packageSet(); if (!packages) return;
    const text = briefingText(packages);
    if (navigator.clipboard?.writeText) navigator.clipboard.writeText(text).then(()=>flashCopy());
    else {
      const area = document.createElement('textarea'); area.value = text; document.body.appendChild(area); area.select(); document.execCommand('copy'); area.remove(); flashCopy();
    }
  }
  function flashCopy() { const b=document.querySelector('#copyBriefing'); const old=b.textContent; b.textContent='Copied'; setTimeout(()=>b.textContent=old,1200); }

  function renderDiplomat() {
    institutionalizeLabels();
    if (typeof result === 'undefined' || !result?.scenarios?.length || !document.querySelector('#diplomatCommand')) return;
    const packages = packageSet();
    renderStatus(packages); renderPackages(packages); renderRedlines(packages); renderConcessions(); renderTalkingPoints(packages); renderMatrix();
  }

  function bind() {
    document.querySelector('#openBriefing').addEventListener('click', openBriefing);
    document.querySelector('#copyBriefing').addEventListener('click', copyBriefing);
    document.querySelector('#printBriefing').addEventListener('click', () => { openBriefing(); setTimeout(()=>window.print(),60); });
    document.querySelector('#closeBriefing').addEventListener('click', () => document.querySelector('#diplomaticBriefing').close());
    document.querySelector('#diplomatCommand').addEventListener('click', event => {
      const card = event.target.closest('[data-diplomat-scenario]');
      if (!card || typeof result === 'undefined') return;
      const scenario = result.scenarios.find(s => s.id === card.dataset.diplomatScenario);
      if (scenario && typeof selected !== 'undefined') { selected = scenario; if (typeof render === 'function') render(); }
    });
    document.querySelectorAll('[data-redline]').forEach(input => {
      input.value = limits[input.dataset.redline];
      input.addEventListener('input', () => {
        limits[input.dataset.redline] = Number(input.value);
        saveJson(LIMITS_KEY, limits); renderDiplomat();
      });
    });
    const notes = document.querySelector('#diplomatNotes');
    try { notes.value = localStorage.getItem(NOTES_KEY) || ''; } catch (_) {}
    notes.addEventListener('input', () => { try { localStorage.setItem(NOTES_KEY, notes.value); } catch (_) {} document.querySelector('#notesSaved').textContent='Saved locally'; });
  }

  function inject() {
    if (document.querySelector('#diplomatCommand')) return;
    document.title = 'Canada–U.S. Diplomatic Policy Studio';
    const brandStrong = document.querySelector('.brand strong');
    const brandSpan = document.querySelector('.brand span');
    if (brandStrong) brandStrong.textContent = 'Canada–U.S. Diplomatic Policy Studio';
    if (brandSpan) { brandSpan.textContent = 'Negotiation intelligence · economic scenario lab'; brandSpan.insertAdjacentHTML('afterend','<span class="diplomat-mode-badge">Diplomatic mode</span>'); }
    const confidenceLabel = document.querySelector('.confidence span'); if (confidenceLabel) confidenceLabel.textContent = 'DATA COVERAGE INDICATOR';
    const canadaTab=document.querySelector('[data-negotiator="canada"]'); if(canadaTab) canadaTab.textContent='🇨🇦 Canada delegation';
    const usTab=document.querySelector('[data-negotiator="us"]'); if(usTab) usTab.textContent='🇺🇸 U.S. delegation';
    institutionalizeLabels();

    const anchor = document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id='diplomatCommand'; section.className='diplomat-command';
    section.innerHTML = `<div class="diplomat-command-head"><div><div class="eyebrow">Diplomatic decision desk</div><h2>Turn model output into a negotiating package</h2><p>Use the model to structure options, expose trade-offs and prepare language for the room. Political judgment, legal review and mandate authority remain outside the engine.</p></div><div class="diplomat-actions"><button id="openBriefing" class="primary" type="button">Open briefing note</button><button id="printBriefing" type="button">Print briefing</button></div></div>
      <div class="diplomat-status"><div><span>Decision robustness</span><b id="diplomatRobustness">Evaluating…</b><small id="diplomatRobustnessNote"></small></div><div><span>Fairness floor</span><b id="diplomatFairness">—</b><small id="diplomatFairnessNote"></small></div><div><span>Red-line status</span><b id="diplomatRedlineStatus">—</b><small id="diplomatRedlineNote"></small></div><div><span>Shared growth floor</span><b id="diplomatGrowth">—</b><small id="diplomatGrowthNote"></small></div></div>
      <div class="diplomat-body"><div id="diplomatPackages" class="package-lanes"></div>
      <div class="diplomat-workgrid"><section class="diplomat-pane"><div class="eyebrow">Mandate discipline</div><h3>Red lines</h3><p>These thresholds are stored only in this browser and do not alter the economic model.</p><div class="redline-grid">
        <label for="rlCanada">Minimum Canada score</label><input id="rlCanada" data-redline="minCanada" type="number" min="0" max="100" step="1">
        <label for="rlUs">Minimum U.S. score</label><input id="rlUs" data-redline="minUs" type="number" min="0" max="100" step="1">
        <label for="rlRecession">Maximum recession risk %</label><input id="rlRecession" data-redline="maxRecession" type="number" min="0" max="100" step="1">
        <label for="rlGrowth">Minimum bilateral growth %</label><input id="rlGrowth" data-redline="minGrowth" type="number" min="-3" max="5" step="0.1">
        <label for="rlInflation">Maximum inflation %</label><input id="rlInflation" data-redline="maxInflation" type="number" min="0" max="10" step="0.1"></div><div id="redlineSummary" class="redline-summary"></div></section>
      <section class="diplomat-pane"><div class="eyebrow">Reciprocity map</div><h3>Where movement buys the most room</h3><p>Coverage relief is measured against full headline-sector coverage. It is a bargaining cue, not a legal tariff schedule.</p><div id="concessionList" class="concession-list"></div></section>
      <section class="diplomat-pane"><div class="eyebrow">In-room language</div><h3>Talking points</h3><ol id="diplomatTalkingPoints" class="talking-points"></ol><textarea id="diplomatNotes" class="diplomat-notes" placeholder="Private working notes, sequencing, sensitivities, names to brief…"></textarea><div class="notes-foot"><span>Stored in this browser only</span><span id="notesSaved">Working notes</span></div></section></div>
      <div class="package-matrix-wrap"><table class="package-matrix"><thead><tr><th>#</th><th>Package</th><th>Canada</th><th>U.S.</th><th>CA GDP</th><th>U.S. GDP</th><th>Inflation</th><th>Recession</th><th>Red lines</th></tr></thead><tbody id="diplomatMatrixBody"></tbody></table></div>
      <div class="diplomat-callout"><b>Protocol:</b> separate model facts from negotiating positions. “The model estimates…” is analytically defensible; “Canada must…” is a political instruction and should come from mandate authority, not this tool.</div></div>`;
    anchor.insertAdjacentElement('afterend', section);

    const dialog = document.createElement('dialog'); dialog.id='diplomaticBriefing';
    dialog.innerHTML='<div class="briefing-toolbar"><b>Negotiation briefing</b><div><button id="copyBriefing" type="button">Copy text</button><button type="button" onclick="window.print()">Print</button><button id="closeBriefing" type="button">Close</button></div></div><div id="briefingSheet" class="briefing-sheet"></div>';
    document.body.appendChild(dialog);
    bind();
    const cards=document.querySelector('#cards'); if(cards) new MutationObserver(renderDiplomat).observe(cards,{childList:true});
    const party=document.querySelector('#partyView'); if(party) new MutationObserver(institutionalizeLabels).observe(party,{childList:true,subtree:true,characterData:true});
    renderDiplomat();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', inject); else inject();
})();
