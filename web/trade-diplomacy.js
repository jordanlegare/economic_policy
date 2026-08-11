(() => {
  'use strict';

  const LEDGER_KEY='canada-us-trade-diplomacy-decision-ledger-v1';
  const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const f=(v,d=1)=>Number(v||0).toFixed(d);
  const signed=(v,s='')=>`${Number(v||0)>=0?'+':''}${f(v)}${s}`;

  function ledger(){try{return JSON.parse(localStorage.getItem(LEDGER_KEY)||'[]')}catch(_){return[]}}
  function saveLedger(items){try{localStorage.setItem(LEDGER_KEY,JSON.stringify(items.slice(0,20)))}catch(_){}}

  function packageById(id){
    const n=result?.negotiation;
    if(!n)return null;
    if(n.recommendedPackage?.id===id)return n.recommendedPackage;
    return (n.frontier||[]).find(p=>p.id===id)||null;
  }

  function inject(){
    if(document.querySelector('#tradeDiplomacyOps'))return;
    const anchor=document.querySelector('#computationalNegotiation')||document.querySelector('#diplomatCommand')||document.querySelector('.impact-strip');
    if(!anchor)return;
    const section=document.createElement('section');section.id='tradeDiplomacyOps';section.className='trade-ops';
    section.innerHTML=`<div class="ops-head"><div><div class="eyebrow">Canada–U.S. trade diplomacy operations</div><h2>From model output to an executable negotiating process</h2><p>Treaty-aware issue management, robustness screening, round sequencing, implementation safeguards, domestic-authority gates, evidence provenance and a decision ledger.</p></div><div class="ops-badge">OPERATIONS LAYER</div></div>
      <div id="opsSummary" class="ops-summary"></div>
      <div id="opsDoctrine" class="ops-doctrine"></div>
      <div class="ops-grid"><section class="ops-pane"><div class="ops-section-head"><div><div class="eyebrow">Issue architecture</div><h3>CUSMA/USMCA vs parallel bilateral tracks</h3></div></div><div id="opsIssues"></div></section>
      <section class="ops-pane"><div class="eyebrow">Round playbook</div><h3>Sequenced negotiation plan</h3><div id="opsRounds"></div></section></div>
      <div class="ops-grid"><section class="ops-pane"><div class="eyebrow">Robust decision support</div><h3>Does the package survive harder assumptions?</h3><div id="opsRobustCases" class="robust-grid"></div><div class="ops-table-wrap"><table class="ops-table"><thead><tr><th>Package</th><th>Worst-case surplus</th><th>Case wins</th><th>All cases</th></tr></thead><tbody id="opsRobustPackages"></tbody></table></div></section>
      <section class="ops-pane"><div class="eyebrow">Implementation architecture</div><h3>Verification, cure and response</h3><div id="opsGuardrails"></div></section></div>
      <div class="ops-grid"><section class="ops-pane"><div class="eyebrow">Domestic feasibility</div><h3>Mandate and stakeholder gates</h3><div id="opsGates"></div></section>
      <section class="ops-pane"><div class="eyebrow">Evidence and provenance</div><h3>What must be refreshed before the room</h3><div id="opsEvidence"></div></section></div>
      <div class="ops-grid"><section class="ops-pane"><div class="eyebrow">Decision discipline</div><h3>Delegation decision ledger</h3><p>Record a local snapshot of the package actually discussed or chosen. This is browser-local working data—not a secure records system.</p><div class="decision-box"><select id="decisionStatus"><option>Proposed</option><option>Bridge option</option><option>Held in reserve</option><option>Rejected</option><option>Accepted for further drafting</option></select><textarea id="decisionRationale" placeholder="Rationale, condition, red line, counterpart signal, authority required…"></textarea><div class="decision-actions"><button id="recordDecision" type="button">Record snapshot</button><button id="copyOpsSnapshot" class="secondary" type="button">Copy operational snapshot</button></div></div><div id="decisionLedger"></div></section>
      <section class="ops-pane"><div class="eyebrow">Room strategy</div><h3>Bank · link · protect</h3><div id="opsRoomStrategy"></div><div class="ops-warning"><b>Security boundary:</b> browser local storage and this research server are not a classified, protected, or production diplomatic records environment. Do not place controlled or classified material here without an approved secure deployment architecture.</div></section></div>`;
    anchor.insertAdjacentElement('afterend',section);
    document.querySelector('#recordDecision').addEventListener('click',recordDecision);
    document.querySelector('#copyOpsSnapshot').addEventListener('click',copySnapshot);
    renderLedger();
  }

  function render(){
    inject();
    const p=result?.tradeDiplomacy;if(!p||!document.querySelector('#tradeDiplomacyOps'))return;
    const preferred=packageById(p.recommendedRobustPackageId)||result?.negotiation?.recommendedPackage;
    document.querySelector('#opsSummary').innerHTML=`<div><span>Operational readiness</span><b>${f(p.operationalReadiness,0)}/100</b><small>${esc(p.readinessLabel)}</small></div><div><span>Robust package</span><b>${esc(p.recommendedRobustPackageId||'—')}</b><small>${preferred?esc(preferred.strategyName):'No package'}</small></div><div><span>Worst-case surplus floor</span><b>${signed(p.recommendedWorstCaseSurplus)}</b><small>Across ${p.robustCases} harder assumption sets</small></div><div><span>Bridge package</span><b>${esc(p.bridgePackageId||'—')}</b><small>Preserved as a controlled fallback</small></div>`;
    document.querySelector('#opsDoctrine').textContent=p.operatingDoctrine||'';

    document.querySelector('#opsIssues').innerHTML=(p.issueTracks||[]).map(x=>`<div class="issue-track"><div><b>${esc(x.label)}</b><small>${esc(x.linkGroup)}</small><span class="track-badge ${x.parallelTrack?'parallel':''}">${x.parallelTrack?'Parallel bilateral':'CUSMA/USMCA-linked'}</span></div><div><small>${esc(x.forum)}</small>${esc(x.objective)}</div><div><small>Joint value</small><span class="track-score">${f(x.jointValue,0)}</span></div><div><small>Sensitivity</small><span class="track-score ${x.domesticSensitivity>=85?'sensitive':''}">${f(x.domesticSensitivity,0)}</span></div></div>`).join('');

    document.querySelector('#opsRounds').innerHTML=(p.roundPlan||[]).map(x=>`<div class="round-step"><i>${x.order}</i><div><b>${esc(x.phase)}</b><p>${esc(x.negotiatingMove)}</p><em>Exit: ${esc(x.exitCriteria)}</em></div></div>`).join('');

    const robust=(p.robustPackages||[]);const winnerId=p.recommendedRobustPackageId;
    document.querySelector('#opsRobustCases').innerHTML=(p.robustnessCases||[]).map(x=>`<div class="robust-card ${x.winnerPackageId===winnerId?'winner':''}"><small>${esc(x.label)}</small><b>${esc(x.winnerPackageId)}</b><p>${esc(x.description)}</p></div>`).join('');
    document.querySelector('#opsRobustPackages').innerHTML=robust.slice(0,10).map(x=>`<tr><th>${esc(x.packageId)}<small>${esc(x.strategyName)}</small></th><td>${signed(x.worstCaseSurplus)}</td><td>${x.caseWins}</td><td class="${x.clearsAllCases?'yes':'no'}">${x.clearsAllCases?'CLEARS':'BREACH'}</td></tr>`).join('');

    document.querySelector('#opsGuardrails').innerHTML=(p.guardrails||[]).map(x=>`<div class="guardrail"><b>${esc(x.mechanism)}</b><p><strong>Trigger:</strong> ${esc(x.trigger)}</p><p><strong>Evidence:</strong> ${esc(x.evidence)}</p><p><strong>Response:</strong> ${esc(x.response)}</p><small>${esc(x.cadence)}</small></div>`).join('');
    document.querySelector('#opsGates').innerHTML=(p.stakeholderGates||[]).map(x=>`<div class="gate"><b>${esc(x.country)} · ${esc(x.gate)}</b><p>${esc(x.rationale)}</p><small class="${x.formalAuthority?'formal':''}">${x.formalAuthority?'AUTHORITY GATE':'CONSULTATION / IMPLEMENTATION GATE'} · sensitivity ${f(x.sensitivity,0)}</small></div>`).join('');
    document.querySelector('#opsEvidence').innerHTML=(p.evidenceLedger||[]).map(x=>`<div class="evidence-row"><b>${esc(x.source)}</b><p>${esc(x.purpose)}</p><small>${esc(x.status)} · ${esc(x.refreshRule)}</small></div>`).join('');

    const treaty=(p.issueTracks||[]).filter(x=>!x.parallelTrack).sort((a,b)=>b.jointValue-a.jointValue);
    const parallel=(p.issueTracks||[]).filter(x=>x.parallelTrack).sort((a,b)=>b.domesticSensitivity-a.domesticSensitivity);
    const bank=treaty.filter(x=>x.jointValue>=85&&x.domesticSensitivity<80).slice(0,3);
    const link=treaty.filter(x=>x.jointValue>=80).slice(0,4);
    const protect=[...(p.issueTracks||[])].sort((a,b)=>b.domesticSensitivity-a.domesticSensitivity).slice(0,4);
    document.querySelector('#opsRoomStrategy').innerHTML=`<div class="guardrail"><b>Bank early</b><p>${bank.length?bank.map(x=>esc(x.label)).join(' · '):'No low-sensitivity early harvest currently dominates.'}</p></div><div class="guardrail"><b>Link conditionally</b><p>${link.map(x=>esc(x.label)).join(' · ')}</p></div><div class="guardrail"><b>Protect / mandate-check</b><p>${protect.map(x=>esc(x.label)).join(' · ')}</p></div>${parallel.length?`<div class="guardrail"><b>Keep parallel unless deliberately linked</b><p>${parallel.map(x=>esc(x.label)).join(' · ')}</p></div>`:''}`;
    enhanceBriefing();renderLedger();
  }

  function snapshot(){
    const p=result?.tradeDiplomacy,n=result?.negotiation,preferred=packageById(p?.recommendedRobustPackageId)||n?.recommendedPackage;
    return {timestamp:new Date().toISOString(),status:document.querySelector('#decisionStatus')?.value||'Proposed',rationale:document.querySelector('#decisionRationale')?.value?.trim()||'',packageId:p?.recommendedRobustPackageId||preferred?.id||'',strategy:preferred?.strategyName||'',readiness:p?.operationalReadiness||0,readinessLabel:p?.readinessLabel||'',worstCaseSurplus:p?.recommendedWorstCaseSurplus||0,canadaUtility:preferred?.canadaUtility||0,usUtility:preferred?.usUtility||0,stability:preferred?.stabilityScore||0,model:{candidates:n?.candidatesExamined||0,pareto:n?.paretoFrontierSize||0,robustCases:p?.robustCases||0}};
  }
  function recordDecision(){const s=snapshot(),items=ledger();items.unshift(s);saveLedger(items);document.querySelector('#decisionRationale').value='';renderLedger();}
  function renderLedger(){const el=document.querySelector('#decisionLedger');if(!el)return;const items=ledger();el.innerHTML=items.length?items.slice(0,8).map(x=>`<div class="decision-entry"><b>${esc(x.status)} · ${esc(x.packageId)} · ${new Date(x.timestamp).toLocaleString()}</b><span>${esc(x.strategy)} · readiness ${f(x.readiness,0)}/100 · stability ${f(x.stability,0)}/100</span>${x.rationale?`<p>${esc(x.rationale)}</p>`:''}</div>`).join(''):'<p>No decision snapshots recorded in this browser.</p>';}
  function copySnapshot(){const text=JSON.stringify(snapshot(),null,2);if(navigator.clipboard?.writeText)navigator.clipboard.writeText(text);}

  function enhanceBriefing(){
    const sheet=document.querySelector('#briefingSheet'),p=result?.tradeDiplomacy;if(!sheet||!p||sheet.querySelector('.trade-ops-annex'))return;
    const preferred=packageById(p.recommendedRobustPackageId)||result?.negotiation?.recommendedPackage;
    const annex=document.createElement('section');annex.className='trade-ops-annex';
    annex.innerHTML=`<h2>Trade diplomacy operations annex</h2><p><b>Operational readiness:</b> ${f(p.operationalReadiness,0)}/100 — ${esc(p.readinessLabel)}. <b>Robust package:</b> ${esc(p.recommendedRobustPackageId)}${preferred?` (${esc(preferred.strategyName)})`:''}. <b>Worst-case modeled surplus floor:</b> ${signed(p.recommendedWorstCaseSurplus)} across ${p.robustCases} harder assumption sets.</p><h3>Round sequence</h3><ol>${(p.roundPlan||[]).map(x=>`<li><b>${esc(x.phase)}:</b> ${esc(x.negotiatingMove)}</li>`).join('')}</ol><h3>Authority and implementation</h3><ul>${(p.stakeholderGates||[]).filter(x=>x.formalAuthority).map(x=>`<li><b>${esc(x.country)} — ${esc(x.gate)}:</b> ${esc(x.rationale)}</li>`).join('')}</ul><p><b>Doctrine:</b> ${esc(p.operatingDoctrine)}</p>`;
    sheet.appendChild(annex);
  }

  function start(){inject();const cards=document.querySelector('#cards');if(cards)new MutationObserver(render).observe(cards,{childList:true});const brief=document.querySelector('#briefingSheet');if(brief)new MutationObserver(enhanceBriefing).observe(brief,{childList:true});render();}
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',start);else start();
})();

(() => {
  'use strict';

  const PAGE={width:612,height:792,left:54,right:54,top:738,bottom:58};
  const contentWidth=PAGE.width-PAGE.left-PAGE.right;
  const cp1252=new Map([[0x20ac,0x80],[0x201a,0x82],[0x0192,0x83],[0x201e,0x84],[0x2026,0x85],[0x2020,0x86],[0x2021,0x87],[0x02c6,0x88],[0x2030,0x89],[0x0160,0x8a],[0x2039,0x8b],[0x0152,0x8c],[0x017d,0x8e],[0x2018,0x91],[0x2019,0x92],[0x201c,0x93],[0x201d,0x94],[0x2022,0x95],[0x2013,0x96],[0x2014,0x97],[0x02dc,0x98],[0x2122,0x99],[0x0161,0x9a],[0x203a,0x9b],[0x0153,0x9c],[0x017e,0x9e],[0x0178,0x9f]]);

  function cleanText(value){
    return String(value??'')
      .replace(/\u00a0/g,' ')
      .replace(/↔/g,'<->').replace(/≠/g,'!=').replace(/≤/g,'<=').replace(/≥/g,'>=')
      .replace(/✓/g,'PASS').replace(/⚠/g,'WARNING').replace(/→/g,'->')
      .replace(/[\t\r]+/g,' ').replace(/ +/g,' ').trim();
  }

  function binaryText(value){
    const text=cleanText(value);let out='';
    for(const ch of text){
      const code=ch.codePointAt(0);
      if(code<=0x7f||(code>=0xa0&&code<=0xff))out+=String.fromCharCode(code);
      else if(cp1252.has(code))out+=String.fromCharCode(cp1252.get(code));
      else out+='?';
    }
    return out;
  }

  function pdfLiteral(value){
    return `(${binaryText(value).replace(/\\/g,'\\\\').replace(/\(/g,'\\(').replace(/\)/g,'\\)')})`;
  }

  function textWidth(text,size,bold=false){
    let units=0;
    for(const ch of cleanText(text)){
      if(/[ ilI1.,:;!'|`]/.test(ch))units+=0.28;
      else if(/[MW@%&QG]/.test(ch))units+=0.78;
      else if(/[A-Z0-9]/.test(ch))units+=0.57;
      else units+=0.50;
    }
    return units*size*(bold?1.035:1);
  }

  function wrapText(text,size,width,bold=false){
    const paragraphs=String(text??'').split(/\n+/);const lines=[];
    for(const paragraph of paragraphs){
      const words=cleanText(paragraph).split(/\s+/).filter(Boolean);
      if(!words.length){lines.push('');continue;}
      let line='';
      for(const word of words){
        const candidate=line?`${line} ${word}`:word;
        if(!line||textWidth(candidate,size,bold)<=width){line=candidate;continue;}
        lines.push(line);line=word;
        while(textWidth(line,size,bold)>width&&line.length>1){
          let cut=line.length-1;
          while(cut>1&&textWidth(line.slice(0,cut)+'-',size,bold)>width)cut--;
          lines.push(line.slice(0,cut)+'-');line=line.slice(cut);
        }
      }
      if(line)lines.push(line);
    }
    return lines;
  }

  function collectBlocks(root){
    const blocks=[];
    const specialClass=node=>node.classList?.contains('briefing-kicker')?'kicker':node.classList?.contains('briefing-meta')?'meta':node.classList?.contains('briefing-decision')?'callout':node.classList?.contains('briefing-warning')?'warning':node.classList?.contains('briefing-disclaimer')?'disclaimer':null;
    function walk(node){
      if(!node||node.nodeType!==1)return;
      const tag=node.tagName;
      if(tag==='H1'){blocks.push({type:'h1',text:node.innerText});return;}
      if(tag==='H2'){blocks.push({type:'h2',text:node.innerText});return;}
      if(tag==='H3'){blocks.push({type:'h3',text:node.innerText});return;}
      if(tag==='P'){blocks.push({type:'p',text:node.innerText});return;}
      if(tag==='LI'){
        const parent=node.parentElement?.tagName;
        const prefix=parent==='OL'?`${Array.from(node.parentElement.children).indexOf(node)+1}. `:'- ';
        blocks.push({type:'bullet',text:prefix+node.innerText});return;
      }
      const special=specialClass(node);
      if(special){blocks.push({type:special,text:node.innerText});return;}
      Array.from(node.children).forEach(walk);
    }
    Array.from(root.children).forEach(walk);
    return blocks.filter(block=>cleanText(block.text));
  }

  function blockStyle(type){
    const styles={
      kicker:{font:'F2',size:8.5,leading:11,before:0,after:5,indent:0},
      meta:{font:'F1',size:8.3,leading:11,before:0,after:10,indent:0},
      h1:{font:'F2',size:19,leading:23,before:2,after:10,indent:0},
      h2:{font:'F2',size:13.2,leading:16,before:11,after:5,indent:0},
      h3:{font:'F2',size:10.8,leading:14,before:8,after:3,indent:0},
      p:{font:'F1',size:10.1,leading:13.5,before:1,after:6,indent:0},
      bullet:{font:'F1',size:9.8,leading:13,before:0,after:2.5,indent:12},
      callout:{font:'F2',size:10.2,leading:14,before:3,after:8,indent:10},
      warning:{font:'F2',size:9.2,leading:12.5,before:8,after:6,indent:10},
      disclaimer:{font:'F1',size:8.3,leading:11,before:8,after:4,indent:0}
    };
    return styles[type]||styles.p;
  }

  function layoutBlocks(blocks){
    const pages=[[]];let page=pages[0],y=PAGE.top;
    const newPage=()=>{page=[];pages.push(page);y=PAGE.top;};
    const ensure=space=>{if(y-space<PAGE.bottom)newPage();};
    for(const block of blocks){
      const style=blockStyle(block.type);const bold=style.font==='F2';
      const width=contentWidth-style.indent;const lines=wrapText(block.text,style.size,width,bold);
      const reserve=style.before+style.leading*Math.min(lines.length,block.type.startsWith('h')?Math.max(2,lines.length):1)+style.after;
      ensure(reserve);
      y-=style.before;
      for(const line of lines){
        ensure(style.leading);
        if(line)page.push({text:line,x:PAGE.left+style.indent,y,font:style.font,size:style.size});
        y-=style.leading;
      }
      y-=style.after;
    }
    return pages;
  }

  function pageStream(lines,pageNumber,totalPages){
    const commands=['0.82 G 54 754 m 558 754 l S'];
    commands.push(`BT /F2 8 Tf 0.28 g 1 0 0 1 54 763 Tm ${pdfLiteral('CANADA-U.S. TRADE DIPLOMACY · WORKING BRIEF')} Tj ET`);
    for(const line of lines)commands.push(`BT /${line.font} ${line.size.toFixed(2)} Tf 0 g 1 0 0 1 ${line.x.toFixed(2)} ${line.y.toFixed(2)} Tm ${pdfLiteral(line.text)} Tj ET`);
    commands.push('0.82 G 54 42 m 558 42 l S');
    commands.push(`BT /F1 7.8 Tf 0.38 g 1 0 0 1 54 28 Tm ${pdfLiteral('Illustrative analytical support · not an official negotiating mandate or legal position')} Tj ET`);
    commands.push(`BT /F1 7.8 Tf 0.38 g 1 0 0 1 510 28 Tm ${pdfLiteral(`Page ${pageNumber} of ${totalPages}`)} Tj ET`);
    return commands.join('\n');
  }

  function buildPdf(blocks,title='Canada-United States Negotiation Brief'){
    const pages=layoutBlocks(blocks);const objects=[];
    const pageStart=6,contentStart=pageStart+pages.length;
    objects[1]='<< /Type /Catalog /Pages 2 0 R >>';
    objects[2]=`<< /Type /Pages /Count ${pages.length} /Kids [${pages.map((_,i)=>`${pageStart+i} 0 R`).join(' ')}] >>`;
    objects[3]='<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>';
    objects[4]='<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>';
    const stamp=new Date().toISOString().replace(/[-:T]/g,'').slice(0,14)+'Z';
    objects[5]=`<< /Title ${pdfLiteral(title)} /Producer ${pdfLiteral('Canada-U.S. Diplomatic Policy Studio')} /CreationDate ${pdfLiteral('D:'+stamp)} >>`;
    pages.forEach((lines,i)=>{
      const pageId=pageStart+i,streamId=contentStart+i,stream=pageStream(lines,i+1,pages.length);
      objects[pageId]=`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${PAGE.width} ${PAGE.height}] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents ${streamId} 0 R >>`;
      objects[streamId]=`<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`;
    });
    let pdf='%PDF-1.4\n%\xE2\xE3\xCF\xD3\n';const offsets=[0];
    for(let i=1;i<objects.length;i++){offsets[i]=pdf.length;pdf+=`${i} 0 obj\n${objects[i]}\nendobj\n`;}
    const xref=pdf.length;pdf+=`xref\n0 ${objects.length}\n0000000000 65535 f \n`;
    for(let i=1;i<objects.length;i++)pdf+=`${String(offsets[i]).padStart(10,'0')} 00000 n \n`;
    pdf+=`trailer\n<< /Size ${objects.length} /Root 1 0 R /Info 5 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
    const bytes=new Uint8Array(pdf.length);for(let i=0;i<pdf.length;i++)bytes[i]=pdf.charCodeAt(i)&0xff;return bytes;
  }

  function filename(){
    const d=new Date(),pad=v=>String(v).padStart(2,'0');
    return `Canada-US-Negotiation-Brief_${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}_${pad(d.getHours())}${pad(d.getMinutes())}.pdf`;
  }

  function flashSaved(){
    const buttons=[document.querySelector('#printBriefing'),document.querySelector('#saveBriefingPdfDialog')].filter(Boolean);
    buttons.forEach(button=>{const old=button.textContent;button.textContent='PDF saved';setTimeout(()=>button.textContent=old,1400);});
  }

  async function saveBlob(blob,name){
    if(typeof window.showSaveFilePicker==='function'){
      try{
        const handle=await window.showSaveFilePicker({suggestedName:name,types:[{description:'PDF document',accept:{'application/pdf':['.pdf']}}]});
        const writable=await handle.createWritable();await writable.write(blob);await writable.close();return true;
      }catch(error){if(error?.name==='AbortError')return false;}
    }
    const url=URL.createObjectURL(blob),anchor=document.createElement('a');anchor.href=url;anchor.download=name;anchor.style.display='none';document.body.appendChild(anchor);anchor.click();anchor.remove();setTimeout(()=>URL.revokeObjectURL(url),1500);return true;
  }

  async function saveBriefingPdf(){
    const sheet=document.querySelector('#briefingSheet');
    if(!sheet||!cleanText(sheet.innerText)){
      document.querySelector('#openBriefing')?.click();setTimeout(saveBriefingPdf,100);return;
    }
    await new Promise(resolve=>setTimeout(resolve,25));
    const blocks=collectBlocks(sheet);if(!blocks.length)return;
    const bytes=buildPdf(blocks);const saved=await saveBlob(new Blob([bytes],{type:'application/pdf'}),filename());if(saved)flashSaved();
  }

  function relabel(){
    const top=document.querySelector('#printBriefing');if(top)top.textContent='Save briefing PDF';
    const dialog=document.querySelector('#diplomaticBriefing');
    if(dialog){
      const printButton=Array.from(dialog.querySelectorAll('.briefing-toolbar button')).find(button=>button.getAttribute('onclick')?.includes('window.print')||button.textContent.trim()==='Print');
      if(printButton){printButton.id='saveBriefingPdfDialog';printButton.textContent='Save PDF';}
    }
  }

  window.BriefingPdf={buildPdf,collectBlocks,save:saveBriefingPdf};
  window.print=saveBriefingPdf;
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',relabel);else relabel();
})();
