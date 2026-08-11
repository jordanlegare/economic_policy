(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const fmt = (value, digits = 2) => Number(value ?? 0).toFixed(digits);
  const pct = value => `${(100 * Number(value || 0)).toFixed(1)}%`;
  let roomState = null;
  let refreshTimer = null;

  function inject() {
    if (document.querySelector('#robustRoom')) return;
    const anchor = document.querySelector('#calibrationTrust') || document.querySelector('#computationalNegotiation') || document.querySelector('#diplomatCommand') || document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'robustRoom';
    section.className = 'robust-room';
    section.innerHTML = `
      <div class="robust-head"><div><div class="eyebrow">Uncertainty & diplomat room</div><h2>What survives uncertainty, and what should the delegation do next?</h2><p>Second-stage parameter Monte Carlo, downside risk, minimax regret and a persistent round-by-round negotiation ledger.</p></div><div id="robustBadge" class="robust-badge">AWAITING EVALUATION</div></div>
      <div id="robustSummary" class="robust-summary"></div>
      <div id="robustParameters" class="parameter-chips"></div>
      <div class="robust-table-wrap"><table class="robust-table"><thead><tr><th>Package</th><th>Both clear BATNAs</th><th>Canada CVaR10</th><th>U.S. CVaR10</th><th>Canada 95% interval</th><th>U.S. 95% interval</th><th>Max regret</th><th>Wins draws</th></tr></thead><tbody id="robustRows"></tbody></table></div>
      <div class="room-grid">
        <section class="room-panel"><div class="eyebrow">Round control</div><h3>Negotiation state</h3><div id="roomStatus" class="room-status">Loading room state…</div><form id="roundForm" class="room-form"><label>Round<input name="round" type="number" min="1" value="1"></label><label>Phase<select name="phase"><option>preparation</option><option>mandate and facts</option><option>early harvest</option><option>conditional exchange</option><option>package round</option><option>closure architecture</option><option>post-agreement management</option></select></label><button class="wide" type="submit">Update round</button></form></section>
        <section class="room-panel"><div class="eyebrow">Mandate & red lines</div><h3>Authority envelope</h3><form id="mandateForm" class="room-form"><label>Issue<select name="issueId"><option value="us-tariff-relief">U.S. tariff relief</option><option value="canada-tariff-relief">Canadian tariff relief</option><option value="border-facilitation">Border facilitation</option><option value="procurement">Procurement</option><option value="supply-chain">Supply chain</option></select></label><label>Authority<select name="authority"><option value="delegation_discretion">Delegation discretion</option><option value="senior_approval_required">Senior approval</option><option value="ministerial">Ministerial</option><option value="legal_constraint">Legal constraint</option><option value="non_negotiable">Non-negotiable</option></select></label><label>Max Canada move<input name="maxCanadaMove" type="number" min="0" max="100" value="100"></label><label>Min U.S. move<input name="minUsMove" type="number" min="0" max="100" value="0"></label><label class="wide"><span><input name="hardRedLine" type="checkbox"> Hard red line</span></label><label class="wide">Note<input name="note" placeholder="Mandate rationale or approval condition"></label><button class="wide" type="submit">Save mandate rule</button></form><div id="mandateList" class="room-list"></div></section>
        <section class="room-panel"><div class="eyebrow">Offer history</div><h3>Record an offer</h3><form id="offerForm" class="room-form"><label>Side<select name="side"><option value="canada">Canada</option><option value="us">United States</option></select></label><label>Package<select name="packageId" id="roomPackageSelect"></select></label><label class="wide">Note<input name="note" placeholder="Opening, bridge, response, conditional offer…"></label><button class="wide" type="submit">Record offer</button></form><div id="offerList" class="room-list"></div></section>
        <section class="room-panel"><div class="eyebrow">Concession accounting</div><h3>Reciprocity ledger</h3><form id="concessionForm" class="room-form"><label>Side<select name="side"><option value="canada">Canada</option><option value="us">United States</option></select></label><label>Issue<select name="issueId"><option value="us-tariff-relief">U.S. tariff relief</option><option value="canada-tariff-relief">Canadian tariff relief</option><option value="border-facilitation">Border facilitation</option><option value="procurement">Procurement</option><option value="supply-chain">Supply chain</option></select></label><label>Magnitude<input name="magnitude" type="number" min="0" step="0.1" value="0"></label><label>Own-cost estimate<input name="estimatedOwnCost" type="number" min="0" step="0.1" value="0"></label><label>Counterpart value<input name="estimatedCounterpartValue" type="number" min="0" step="0.1" value="0"></label><label><span><input name="reciprocal" type="checkbox"> Reciprocated</span></label><label><span><input name="conditional" type="checkbox" checked> Conditional</span></label><label class="wide">Note<input name="note" placeholder="What was exchanged and on what condition?"></label><button class="wide" type="submit">Record concession</button></form><div id="concessionBalance" class="room-status"></div><div id="concessionList" class="room-list"></div></section>
        <section class="room-panel full"><div class="eyebrow">Counteroffers</div><h3>Mandate-aware next moves</h3><div id="counterofferList" class="room-list"></div></section>
        <section class="room-panel"><div class="eyebrow">Playbooks</div><h3>If they ask for X…</h3><form id="playbookForm" class="room-form"><label>Issue<select name="issueId"><option value="us-tariff-relief">U.S. tariff relief</option><option value="canada-tariff-relief">Canadian tariff relief</option><option value="border-facilitation">Border facilitation</option><option value="procurement">Procurement</option><option value="supply-chain">Supply chain</option></select></label><label>Authority<select name="authority"><option value="delegation_discretion">Delegation discretion</option><option value="senior_approval_required">Senior approval</option><option value="ministerial">Ministerial</option></select></label><label class="wide">Trigger<input name="trigger" placeholder="If the U.S. asks for…"></label><label class="wide">Response<textarea name="response" placeholder="Then respond with…"></textarea></label><button class="wide" type="submit">Add playbook</button></form><div id="playbookList" class="room-list"></div></section>
        <section class="room-panel"><div class="eyebrow">Post-round debrief</div><h3>Capture what changed</h3><form id="debriefForm" class="room-form"><label class="wide">Summary<textarea name="summary" required placeholder="What happened this round?"></textarea></label><label class="wide">Counterpart signals<input name="counterpartSignals" placeholder="Flexibility, resistance, priorities, credibility signals"></label><label class="wide">Unresolved<input name="unresolved" placeholder="Open issues and evidence requests"></label><label class="wide">Next actions<input name="nextActions" placeholder="What should happen before the next round?"></label><button class="wide" type="submit">Save debrief</button></form><div id="debriefList" class="room-list"></div></section>
      </div><div id="roomWarning" class="room-warning">Local research workflow only. Do not enter protected or classified information.</div>`;
    anchor.insertAdjacentElement('afterend', section);
    bindForms();
  }

  async function postRoom(payload) {
    const response = await fetch('/api/room', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(payload)});
    roomState = await response.json();
    renderRoom();
    if (!response.ok) throw new Error('Room event rejected by server validation');
  }

  async function refreshRoom() {
    try {
      roomState = await fetch('/api/room', {cache:'no-store'}).then(r => r.json());
      renderRoom();
    } catch (error) {
      const status = document.querySelector('#roomStatus');
      if (status) status.textContent = 'Room API unavailable';
    }
  }

  function value(form, name) { return new FormData(form).get(name); }
  function bindForms() {
    document.querySelector('#roundForm')?.addEventListener('submit', async event => {
      event.preventDefault(); const f=event.currentTarget;
      await postRoom({action:'set-round',round:+value(f,'round'),phase:value(f,'phase')});
    });
    document.querySelector('#mandateForm')?.addEventListener('submit', async event => {
      event.preventDefault(); const f=event.currentTarget, hard=f.elements.hardRedLine.checked;
      await postRoom({action:hard?'red-line':'set-mandate',issueId:value(f,'issueId'),authority:value(f,'authority'),maxCanadaMove:+value(f,'maxCanadaMove'),minUsMove:+value(f,'minUsMove'),hardRedLine:hard,note:value(f,'note')});
    });
    document.querySelector('#offerForm')?.addEventListener('submit', async event => {
      event.preventDefault(); const f=event.currentTarget;
      await postRoom({action:'offer',side:value(f,'side'),packageId:value(f,'packageId'),note:value(f,'note')});
    });
    document.querySelector('#concessionForm')?.addEventListener('submit', async event => {
      event.preventDefault(); const f=event.currentTarget;
      await postRoom({action:'concession',side:value(f,'side'),issueId:value(f,'issueId'),magnitude:+value(f,'magnitude'),estimatedOwnCost:+value(f,'estimatedOwnCost'),estimatedCounterpartValue:+value(f,'estimatedCounterpartValue'),reciprocal:f.elements.reciprocal.checked,conditional:f.elements.conditional.checked,note:value(f,'note')});
    });
    document.querySelector('#playbookForm')?.addEventListener('submit', async event => {
      event.preventDefault(); const f=event.currentTarget;
      await postRoom({action:'playbook',issueId:value(f,'issueId'),authority:value(f,'authority'),trigger:value(f,'trigger'),response:value(f,'response')});
    });
    document.querySelector('#debriefForm')?.addEventListener('submit', async event => {
      event.preventDefault(); const f=event.currentTarget;
      await postRoom({action:'debrief',summary:value(f,'summary'),counterpartSignals:value(f,'counterpartSignals'),unresolved:value(f,'unresolved'),nextActions:value(f,'nextActions')});
    });
    document.querySelector('#counterofferList')?.addEventListener('click', async event => {
      const button=event.target.closest('[data-record-package]'); if(!button)return;
      await postRoom({action:'offer',side:'canada',packageId:button.dataset.recordPackage,note:`Recorded from ${button.dataset.category} counteroffer suggestion`});
    });
  }

  function renderRobustness() {
    inject();
    if (typeof result === 'undefined' || !result?.robustness) return;
    const r=result.robustness, recommended=r.packages?.find(p=>p.packageId===r.recommendedPackageId);
    const badge=document.querySelector('#robustBadge');
    badge.textContent=r.empiricallyCalibrated?'ROBUST + EMPIRICAL':'ROBUST · MODEL RISK';
    document.querySelector('#robustSummary').innerHTML=recommended ? `
      <div><span>Robust package</span><b>${esc(recommended.packageId)}</b><small>${esc(recommended.strategyId)}</small></div>
      <div><span>Both clear reservation</span><b>${pct(recommended.jointClearProbability)}</b><small>required ${pct(r.requiredJointClearProbability)}</small></div>
      <div><span>Worst-country CVaR10</span><b>${fmt(Math.min(recommended.canadaCvar10Surplus,recommended.usCvar10Surplus))}</b><small>average surplus in worst 10% tail</small></div>
      <div><span>Maximum regret</span><b>${fmt(recommended.maxRegret)}</b><small>${r.secondStageMonteCarloDraws} common-random draws</small></div>` : '<div><span>No robust package available</span></div>';
    document.querySelector('#robustParameters').innerHTML=(r.parameterDistributions||[]).map(d=>`<div class="parameter-chip"><b>${esc(d.name)}</b> μ ${fmt(d.mean,3)} · σ ${fmt(d.standardDeviation,3)}<small>${esc(d.evidenceClass)} · ${esc(d.source)}</small></div>`).join('');
    document.querySelector('#robustRows').innerHTML=(r.packages||[]).map(p=>`<tr class="${p.packageId===r.recommendedPackageId?'recommended':''}"><td>${esc(p.packageId)}${p.packageId===r.recommendedPackageId?' ★':''}</td><td>${pct(p.jointClearProbability)}</td><td>${fmt(p.canadaCvar10Surplus)}</td><td>${fmt(p.usCvar10Surplus)}</td><td>${fmt(p.canadaCi95?.[0])}…${fmt(p.canadaCi95?.[1])}</td><td>${fmt(p.usCi95?.[0])}…${fmt(p.usCi95?.[1])}</td><td>${fmt(p.maxRegret)}</td><td>${pct(p.rankWinProbability)}</td></tr>`).join('');
    const select=document.querySelector('#roomPackageSelect');
    if(select){const current=select.value;select.innerHTML=(result.negotiation?.frontier||[]).map(p=>`<option value="${esc(p.id)}">${esc(p.id)} · ${esc(p.strategyName)}</option>`).join('');if([...select.options].some(o=>o.value===current))select.value=current;}
  }

  function roomItems(items, render) {
    return items?.length ? items.slice().reverse().slice(0,20).map(render).join('') : '<div class="room-empty">No entries recorded.</div>';
  }

  function renderRoom() {
    if(!roomState)return;
    const roundForm=document.querySelector('#roundForm');
    if(roundForm){roundForm.elements.round.value=roomState.round||1;roundForm.elements.phase.value=roomState.phase||'preparation';}
    document.querySelector('#roomStatus').textContent=`Round ${roomState.round} · ${roomState.phase} · revision ${roomState.revision} · local append-only history`;
    document.querySelector('#mandateList').innerHTML=(roomState.mandate||[]).map(m=>`<div class="room-item ${m.hardRedLine?'red':''}"><b>${esc(m.issueId)} · ${esc(m.authority)}</b><span>Canada ≤ ${fmt(m.maxCanadaMove,0)} · U.S. ≥ ${fmt(m.minUsMove,0)}${m.hardRedLine?' · HARD RED LINE':''}</span><small>${esc(m.note||'No note')}</small></div>`).join('');
    document.querySelector('#offerList').innerHTML=roomItems(roomState.offers,o=>`<div class="room-item"><b>R${o.round} · ${esc(o.side)} · ${esc(o.packageId)}</b><span>Current modeled joint clearance ${pct(o.currentJointClearProbability)}</span><small>${esc(o.note)}</small></div>`);
    const cb=roomState.concessionBalance||{};
    document.querySelector('#concessionBalance').textContent=`Estimated concession balance · Canada ${fmt(cb.canadaGiven)} · U.S. ${fmt(cb.usGiven)} · U.S./Canada ${fmt(cb.usToCanadaRatio)}×`;
    document.querySelector('#concessionList').innerHTML=roomItems(roomState.concessions,c=>`<div class="room-item"><b>R${c.round} · ${esc(c.side)} · ${esc(c.issueId)} · ${fmt(c.magnitude)}</b><span>${c.conditional?'conditional':'unconditional'} · ${c.reciprocal?'reciprocated':'not yet reciprocated'}</span><small>${esc(c.note)}</small></div>`);
    document.querySelector('#counterofferList').innerHTML=roomState.counteroffers?.length?roomState.counteroffers.map(c=>`<div class="room-item counteroffer"><div><b>${esc(c.category)} · ${esc(c.packageId)}</b><div class="metrics">Both clear ${pct(c.jointClearProbability)} · CA CVaR ${fmt(c.canadaCvar10Surplus)} · US CVaR ${fmt(c.usCvar10Surplus)} · max regret ${fmt(c.maxRegret)} · ${esc(c.authorityStatus)}</div><small>${esc(c.rationale)}</small></div><button class="room-action" data-record-package="${esc(c.packageId)}" data-category="${esc(c.category)}">Record as offer</button></div>`).join(''):'<div class="room-empty">Run an evaluation to generate mandate-aware counteroffers.</div>';
    const automatic=(roomState.automaticPlaybook||[]).map(p=>`<div class="room-item"><b>Automatic · ${esc(p.trigger)}</b><span>${esc(p.response)}</span></div>`).join('');
    const custom=roomItems(roomState.playbooks,p=>`<div class="room-item"><b>${esc(p.issueId)} · ${esc(p.authority)}</b><span>If: ${esc(p.trigger)}</span><small>Then: ${esc(p.response)}</small></div>`);
    document.querySelector('#playbookList').innerHTML=automatic+custom;
    document.querySelector('#debriefList').innerHTML=roomItems(roomState.debriefs,d=>`<div class="room-item"><b>Round ${d.round} · ${esc(d.summary)}</b><span>Signals: ${esc(d.counterpartSignals)}</span><small>Unresolved: ${esc(d.unresolved)} · Next: ${esc(d.nextActions)}</small></div>`);
    document.querySelector('#roomWarning').textContent=roomState.warning||'Local research workflow only.';
  }

  function scheduleRefresh() {
    clearTimeout(refreshTimer);
    refreshTimer=setTimeout(()=>{renderRobustness();refreshRoom();},50);
  }

  function start() {
    inject();
    renderRobustness();
    refreshRoom();
    const cards=document.querySelector('#cards');
    if(cards)new MutationObserver(scheduleRefresh).observe(cards,{childList:true});
  }

  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',start);else start();
})();
