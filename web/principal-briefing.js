(() => {
  'use strict';

  const REDLINE_KEY='canada-policy-diplomatic-redlines-v1';
  const NOTES_KEY='canada-policy-diplomatic-notes-v1';
  const DECISION_LEDGER_KEY='canada-us-trade-diplomacy-decision-ledger-v1';
  const DEFAULT_REDLINES={minCanada:45,minUs:45,maxRecession:40,minGrowth:0,maxInflation:3.5};
  const PAGE={width:612,height:792,left:54,right:54,top:726,bottom:56};
  const WIDTH=PAGE.width-PAGE.left-PAGE.right;
  const COLORS={
    navy:[0.055,0.125,0.205],navy2:[0.085,0.205,0.325],red:[0.70,0.11,0.16],
    ink:[0.10,0.13,0.17],slate:[0.35,0.39,0.43],muted:[0.48,0.52,0.56],
    line:[0.82,0.84,0.86],panel:[0.955,0.965,0.973],warm:[0.985,0.955,0.945],
    green:[0.08,0.42,0.26],greenBg:[0.925,0.975,0.945],amber:[0.62,0.38,0.04],amberBg:[0.985,0.965,0.91],
    white:[1,1,1]
  };
  const cp1252=new Map([[0x20ac,0x80],[0x201a,0x82],[0x0192,0x83],[0x201e,0x84],[0x2026,0x85],[0x2020,0x86],[0x2021,0x87],[0x02c6,0x88],[0x2030,0x89],[0x0160,0x8a],[0x2039,0x8b],[0x0152,0x8c],[0x017d,0x8e],[0x2018,0x91],[0x2019,0x92],[0x201c,0x93],[0x201d,0x94],[0x2022,0x95],[0x2013,0x96],[0x2014,0x97],[0x02dc,0x98],[0x2122,0x99],[0x0161,0x9a],[0x203a,0x9b],[0x0153,0x9c],[0x017e,0x9e],[0x0178,0x9f]]);

  const n=v=>Number(v||0);
  const fmt=(v,d=1)=>Number(v||0).toFixed(d);
  const signed=(v,d=1)=>`${n(v)>=0?'+':''}${fmt(v,d)}`;
  const pct=v=>`${(100*n(v)).toFixed(1)}%`;
  const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const cap=s=>String(s||'').replace(/[_-]+/g,' ').replace(/\b\w/g,c=>c.toUpperCase());

  function cleanText(value){
    return String(value??'').replace(/\u00a0/g,' ').replace(/↔/g,'<->').replace(/≠/g,'!=').replace(/≤/g,'<=').replace(/≥/g,'>=').replace(/✓/g,'PASS').replace(/⚠/g,'WARNING').replace(/→/g,'->').replace(/[\t\r]+/g,' ').replace(/ +/g,' ').trim();
  }
  function binaryText(value){
    let out='';for(const ch of cleanText(value)){const code=ch.codePointAt(0);if(code<=0x7f||(code>=0xa0&&code<=0xff))out+=String.fromCharCode(code);else if(cp1252.has(code))out+=String.fromCharCode(cp1252.get(code));else out+='?';}return out;
  }
  function pdfLiteral(value){return `(${binaryText(value).replace(/\\/g,'\\\\').replace(/\(/g,'\\(').replace(/\)/g,'\\)')})`;}
  function textWidth(text,size,bold=false){let units=0;for(const ch of cleanText(text)){if(/[ ilI1.,:;!'|`]/.test(ch))units+=0.28;else if(/[MW@%&QG]/.test(ch))units+=0.78;else if(/[A-Z0-9]/.test(ch))units+=0.57;else units+=0.50;}return units*size*(bold?1.035:1);}
  function wrapText(text,size,width,bold=false){
    const paragraphs=String(text??'').split(/\n+/),lines=[];
    for(const paragraph of paragraphs){const words=cleanText(paragraph).split(/\s+/).filter(Boolean);if(!words.length){lines.push('');continue;}let line='';for(const word of words){const candidate=line?`${line} ${word}`:word;if(!line||textWidth(candidate,size,bold)<=width){line=candidate;continue;}lines.push(line);line=word;while(textWidth(line,size,bold)>width&&line.length>1){let cut=line.length-1;while(cut>1&&textWidth(line.slice(0,cut)+'-',size,bold)>width)cut--;lines.push(line.slice(0,cut)+'-');line=line.slice(cut);}}if(line)lines.push(line);}return lines;
  }
  function rgb(c,stroke=false){return `${c.map(v=>Number(v).toFixed(3)).join(' ')} ${stroke?'RG':'rg'}`;}
  function safeJson(key,fallback){try{return JSON.parse(localStorage.getItem(key)||'null')??fallback}catch(_){return fallback}}
  function hostname(url){try{return new URL(url).hostname.replace(/^www\./,'')}catch(_){return ''}}
  function issueLabel(id){return ({'us-tariff-relief':'U.S. tariff relief','canada-tariff-relief':'Canadian retaliatory-tariff relief','border-facilitation':'Border and standards facilitation','procurement':'Reciprocal procurement access','supply-chain':'North American supply-chain commitment'})[id]||cap(id);}

  function packageById(r,id){
    const a=r?.negotiation;if(!a||!id)return null;
    if(a.recommendedPackage?.id===id)return a.recommendedPackage;
    return (a.frontier||[]).find(p=>p.id===id)||null;
  }
  function metricsById(r,id){return (r?.robustness?.packages||[]).find(p=>p.packageId===id)||null;}
  function latest(items,predicate=()=>true){return [...(items||[])].filter(predicate).sort((a,b)=>n(b.revision)-n(a.revision))[0]||null;}
  function moves(pkg,side,limit=4){
    const field=side==='canada'?'canadaMove':'usMove';
    return (pkg?.issues||[]).filter(i=>n(i[field])>0.01).sort((a,b)=>n(b[field])-n(a[field])).slice(0,limit).map(i=>({label:i.label||issueLabel(i.id),value:n(i[field])}));
  }
  function moveSentence(items){return items.length?items.map(x=>`${x.label} (${fmt(x.value,0)}/100 package intensity)`).join('; '):'No material movement in the current package definition.';}

  function authorityFor(pkg,room){
    const rules=new Map((room?.mandate||[]).map(x=>[x.issueId,x]));let blocked=false,escalation=false;const reasons=[];
    for(const issue of pkg?.issues||[]){const rule=rules.get(issue.id);if(!rule)continue;const ca=n(issue.canadaMove),us=n(issue.usMove);const caExceeds=ca>n(rule.maxCanadaMove)+1e-9,usBelow=us+1e-9<n(rule.minUsMove);if(caExceeds||usBelow){if(rule.hardRedLine)blocked=true;else escalation=true;reasons.push(`${issue.label||issueLabel(issue.id)} ${caExceeds?'exceeds Canada\'s authorized move':'does not meet required U.S. reciprocity'}`);}if(ca>1e-9&&rule.authority&&rule.authority!=='delegation_discretion'){escalation=true;reasons.push(`${issue.label||issueLabel(issue.id)} requires ${cap(rule.authority)}`);}}
    return {blocked,escalation,status:blocked?'Blocked by recorded red line':escalation?'Requires authority escalation':'Within recorded delegation authority',reasons};
  }

  function diffPackages(current,previous){
    if(!current||!previous)return [];
    const old=new Map((previous.issues||[]).map(i=>[i.id,i])),changes=[];
    for(const issue of current.issues||[]){const before=old.get(issue.id)||{};const ca=n(issue.canadaMove)-n(before.canadaMove),us=n(issue.usMove)-n(before.usMove);if(Math.abs(ca)>=0.5)changes.push(`Canada move on ${issue.label||issueLabel(issue.id)} ${ca>0?'increased':'decreased'} by ${fmt(Math.abs(ca),0)} points.`);if(Math.abs(us)>=0.5)changes.push(`U.S. move on ${issue.label||issueLabel(issue.id)} ${us>0?'increased':'decreased'} by ${fmt(Math.abs(us),0)} points.`);}
    return changes.slice(0,5);
  }

  function packageSummary(r,pkg){
    if(!pkg)return null;const m=metricsById(r,pkg.id);return {
      id:pkg.id,strategy:pkg.strategyName||pkg.strategyId||'',stability:n(pkg.stabilityScore),canadaUtility:n(pkg.canadaUtility),usUtility:n(pkg.usUtility),canadaSurplus:n(pkg.canadaSurplus),usSurplus:n(pkg.usSurplus),
      jointClear:m?n(m.jointClearProbability):0,canadaClear:m?n(m.canadaClearProbability):0,usClear:m?n(m.usClearProbability):0,canadaCvar:m?n(m.canadaCvar10Surplus):0,usCvar:m?n(m.usCvar10Surplus):0,maxRegret:m?n(m.maxRegret):0,rankWin:m?n(m.rankWinProbability):0,
      canadaCi:m?.canadaCi95||[0,0],usCi:m?.usCi95||[0,0],issues:pkg.issues||[]
    };
  }

  function buildBriefingModel(input={}){
    const r=input.result||null,room=input.room||{},settingsInput=input.settings||{},now=input.now?new Date(input.now):new Date();
    const redlines=input.redlines||DEFAULT_REDLINES,notes=input.notes||'',ledger=input.ledger||[];
    const robustId=r?.robustness?.recommendedPackageId||r?.negotiation?.recommendedPackage?.id||'';
    const bestPkg=packageById(r,robustId)||r?.negotiation?.recommendedPackage||null;
    const bridgeCounter=(room?.counteroffers||[]).find(x=>String(x.category).toLowerCase()==='bridge');
    const bridgeId=bridgeCounter?.packageId||r?.tradeDiplomacy?.bridgePackageId||(r?.negotiation?.frontier||[]).find(p=>p.id!==bestPkg?.id)?.id||'';
    const bridgePkg=packageById(r,bridgeId);
    const best=packageSummary(r,bestPkg),bridge=packageSummary(r,bridgePkg);
    const bestAuthority=authorityFor(bestPkg,room),bridgeAuthority=authorityFor(bridgePkg,room);
    const usOffer=latest(room?.offers,x=>x.side==='us'),caOffer=latest(room?.offers,x=>x.side==='canada');
    const priorOffer=[...(room?.offers||[])].sort((a,b)=>n(b.revision)-n(a.revision))[1]||null;
    const latestOffer=latest(room?.offers);const latestPkg=packageById(r,latestOffer?.packageId),priorPkg=packageById(r,priorOffer?.packageId);
    const debrief=latest(room?.debriefs);const concession=room?.concessionBalance||{};const lastDecision=ledger[0]||null;
    const usAskPkg=packageById(r,usOffer?.packageId),caAskPkg=packageById(r,caOffer?.packageId)||bestPkg;
    const currentUsTariff=n(settingsInput.usTariff??settingsInput.us_tariff_canada??0),currentCaTariff=n(settingsInput.retaliatoryTariff??settingsInput.canada_retaliatory_tariff??0);
    const calibration=r?.calibration||{},robust=r?.robustness||{},neg=r?.negotiation||{},ops=r?.tradeDiplomacy||{};

    const whereWeAre=[
      `Round ${room?.round||1}, phase: ${room?.phase||'preparation'}. The robust engine currently selects ${best?`${best.id} (${best.strategy})`:'no package'} as the decision anchor.`,
      `Current modeled tariff settings: U.S. tariff ${fmt(currentUsTariff,1)}%; Canadian retaliatory tariff ${fmt(currentCaTariff,1)}%. Operational readiness is ${fmt(ops.operationalReadiness,0)}/100 (${ops.readinessLabel||'not rated'}).`,
      best?`Under the declared uncertainty distributions, both sides clear their modeled reservation values in ${pct(best.jointClear)} of draws; the required gate is ${pct(robust.requiredJointClearProbability||0)}.`:'No robust package metrics are available.'
    ];
    if(lastDecision)whereWeAre.push(`Latest recorded decision status: ${lastDecision.status||'Recorded'} on ${lastDecision.packageId||'unspecified package'}${lastDecision.rationale?` - ${lastDecision.rationale}`:''}.`);

    const whatTheyWant=usOffer&&usAskPkg?[
      `The latest recorded U.S. offer is ${usOffer.packageId} from round ${usOffer.round}. In package terms, the Canadian moves embedded in that offer are: ${moveSentence(moves(usAskPkg,'canada'))}`,
      usOffer.note?`Recorded U.S. offer note: ${usOffer.note}`:'No explanatory note was recorded with the U.S. offer.'
    ]:[`No U.S. package has been recorded in the Diplomat Room. Do not describe model-implied U.S. utility as a stated U.S. ask. The current bargaining model can only identify terms that create modeled U.S. value.`];

    const whatWeWant=caOffer&&packageById(r,caOffer.packageId)?[
      `Canada's latest recorded offer is ${caOffer.packageId}. The U.S. moves sought in that package are: ${moveSentence(moves(packageById(r,caOffer.packageId),'us'))}`,
      caOffer.note?`Recorded Canadian offer note: ${caOffer.note}`:'No explanatory note was recorded with the Canadian offer.'
    ]:[bestPkg?`No Canadian offer has been recorded this round. The robust package would seek: ${moveSentence(moves(bestPkg,'us'))}`:'No Canadian offer or robust package is available.'];

    const whatChanged=[];
    if(lastDecision&&best&&lastDecision.packageId&&lastDecision.packageId!==best.id)whatChanged.push(`The robust recommendation changed from ${lastDecision.packageId} at the latest decision snapshot to ${best.id} now.`);
    if(latestPkg&&priorPkg)whatChanged.push(...diffPackages(latestPkg,priorPkg));
    if(debrief?.summary)whatChanged.push(`Latest debrief: ${debrief.summary}`);
    if(debrief?.counterpartSignals)whatChanged.push(`Counterpart signal recorded: ${debrief.counterpartSignals}`);
    if(n(concession.canadaGiven)>0||n(concession.usGiven)>0)whatChanged.push(`Recorded concession balance is Canada ${fmt(concession.canadaGiven,1)} versus U.S. ${fmt(concession.usGiven,1)}; U.S./Canada ratio ${fmt(concession.usToCanadaRatio,2)}x.`);
    if(!whatChanged.length)whatChanged.push('No prior offer/debrief comparison is recorded. Treat this as the opening-state briefing.');

    const mandateLines=(room?.mandate||[]).filter(x=>x.hardRedLine||x.authority!=='delegation_discretion'||n(x.maxCanadaMove)<100||n(x.minUsMove)>0).map(x=>`${issueLabel(x.issueId)}: Canada move <= ${fmt(x.maxCanadaMove,0)}, required U.S. move >= ${fmt(x.minUsMove,0)}; ${x.hardRedLine?'HARD RED LINE; ':''}${cap(x.authority||'delegation_discretion')}${x.note?` - ${x.note}`:''}.`);
    const analyticalGuardrails=[`Canada score >= ${redlines.minCanada}; U.S. score >= ${redlines.minUs}; recession risk <= ${redlines.maxRecession}%; bilateral growth >= ${redlines.minGrowth}%; inflation <= ${redlines.maxInflation}%.`];

    const uncertainties=[];
    const missing=Object.entries(calibration.checks||{}).filter(([,v])=>!v).map(([k])=>cap(k));
    if(missing.length)uncertainties.push(`Empirical calibration is ${fmt(calibration.completeness,0)}% complete (${calibration.grade||'ungraded'}). Missing/uncertified layers: ${missing.join(', ')}.`);
    for(const d of (robust.parameterDistributions||[]).slice().sort((a,b)=>Math.abs(n(b.standardDeviation)/(Math.abs(n(b.mean))+1e-6))-Math.abs(n(a.standardDeviation)/(Math.abs(n(a.mean))+1e-6))).slice(0,6))uncertainties.push(`${cap(d.name)}: mean ${fmt(d.mean,3)}, standard deviation ${fmt(d.standardDeviation,3)}; evidence class ${d.evidenceClass||'unspecified'} (${d.source||'no source label'}).`);
    if(!robust.empiricallyCalibrated)uncertainties.push('The uncertainty layer is not fully empirically calibrated; reported probabilities are conditional model probabilities, not political-acceptance probabilities.');

    const decisions=[];
    if(!best)decisions.push('No robust package is available; do not authorize a package recommendation until the model produces a valid frontier.');
    else if(bestAuthority.blocked)decisions.push(`Do not authorize ${best.id} as drafted: it breaches a recorded hard red line. Decide whether to use ${bridge?.id||'the bridge option'} or seek revised terms.`);
    else if(bestAuthority.escalation)decisions.push(`Decide whether to seek the additional authority required to use ${best.id} as Canada's negotiating anchor. ${bestAuthority.reasons.join('; ')}`);
    else decisions.push(`Decide whether to authorize ${best.id} as Canada's negotiating anchor for the current round, with ${bridge?.id||'the bridge package'} held as the controlled fallback.`);
    if(best&&best.jointClear+1e-12<n(robust.requiredJointClearProbability))decisions.push(`The robust package misses the ${pct(robust.requiredJointClearProbability)} joint-clearance gate. If used, authorize it only as a conditional/iterative proposal rather than a settlement position.`);
    if(n(concession.canadaGiven)>0&&n(concession.usToCanadaRatio)<0.8)decisions.push('Canada is materially ahead in the recorded concession ledger. Decide whether to require measurable reciprocity before any additional unconditional Canadian move.');
    if(debrief?.nextActions)decisions.push(`Confirm or amend the debriefed next action: ${debrief.nextActions}`);

    const topCaMove=moves(bestPkg,'canada',2),topUsMove=moves(bestPkg,'us',2);
    const language=[
      {label:'Opening',text:best?`Canada is prepared to work from a package in the ${best.strategy||best.id} family because it preserves modeled gains for both economies. Any movement should be reciprocal, verifiable, and sequenced.`:'Canada is prepared to work from a reciprocal package once the economic and mandate screens are complete.'},
      {label:'Conditional exchange',text:`We can examine ${topCaMove.length?topCaMove.map(x=>x.label).join(' and '):'Canadian movement'} alongside measurable movement on ${topUsMove.length?topUsMove.map(x=>x.label).join(' and '):'U.S. market access'}, with matched implementation tranches rather than unilateral front-loading.`},
      {label:'Mandate',text:bestAuthority.blocked?'We do not have authority to cross the recorded red line in the package as drafted. We can continue working inside the authorized envelope.':bestAuthority.escalation?'Some elements require additional instructions. We can keep negotiating the structure while reserving those elements for approval.':'The package is inside the recorded delegation authority, subject to normal legal and implementation review.'},
      {label:'Analytical discipline',text:'Our analysis shows a range of outcomes under explicit assumptions. We are using those estimates to compare trade-offs, not presenting them as guarantees or as a forecast of political acceptance.'},
      {label:'Close',text:`If we can align the reciprocal elements, Canada is prepared to move to verification, cure periods, and implementation sequencing before finalizing relief.`}
    ];

    const evidence=(calibration.sources||[]).map(s=>({agency:s.agency||'Unknown agency',dataset:s.dataset||s.id||'Unnamed dataset',vintage:s.vintage||'unspecified',status:s.status||'unspecified',domain:hostname(s.url),id:s.id||''}));
    evidence.push({agency:'Model uncertainty layer',dataset:`${robust.secondStageMonteCarloDraws||0} second-stage common-random-number draws`,vintage:`seed ${robust.seed??'n/a'}`,status:robust.uncertaintyGrade||'model uncertainty',domain:'local model',id:'robustness'});
    evidence.push({agency:'Diplomat Room',dataset:'Append-only local negotiation event log',vintage:`revision ${room?.revision||0}`,status:'working negotiation record - not secure for protected information',domain:'local runtime',id:'room'});

    return {
      title:'Canada-United States Trade Negotiation',subtitle:'Principal Decision Brief',generatedAt:now.toISOString(),displayTime:now.toLocaleString('en-CA',{dateStyle:'medium',timeStyle:'short'}),round:room?.round||1,phase:room?.phase||'preparation',
      classification:'WORKING BRIEF - ANALYTICAL SUPPORT',snapshotId:calibration.snapshotId||'uncalibrated',calibrationGrade:calibration.grade||'ungraded',calibrationCompleteness:n(calibration.completeness),empiricalCertified:!!calibration.certifiedForEmpiricalUse,
      fingerprint:`Snapshot ${calibration.snapshotId||'n/a'} | uncertainty seed ${robust.seed??'n/a'} | room revision ${room?.revision||0}`,
      whereWeAre,whatTheyWant,whatWeWant,whatChanged,redLines:{mandate:mandateLines.length?mandateLines:['No non-default mandate restrictions are recorded in the room.'],analytical:analyticalGuardrails},
      best,bridge,bestAuthority,bridgeAuthority,batna:{canada:n(neg.batna?.canada),us:n(neg.batna?.us),canadaStrategy:neg.batna?.canadaStrategy||'',usStrategy:neg.batna?.usStrategy||'',canadaReservation:n(neg.reservation?.canada),usReservation:n(neg.reservation?.us)},
      uncertainties,decisionRequired:decisions,recommendedLanguage:language,evidenceSources:evidence,notes,
      caution:'Model outputs are analytical decision support. They are not an official negotiating mandate, legal position, forecast, or estimate of political acceptance. Protected or classified negotiation records require an accredited secure environment.'
    };
  }

  class PdfLayout{
    constructor(model){this.model=model;this.pages=[];this.page=null;this.y=PAGE.top;this.newPage(true);}
    newPage(cover=false){this.page={cover,commands:[]};this.pages.push(this.page);this.y=PAGE.top;if(!cover)this.bodyHeader();}
    command(c){this.page.commands.push(c);}
    rect(x,y,w,h,fill,stroke=null,lineWidth=1){if(fill)this.command(`${rgb(fill)} ${x.toFixed(2)} ${y.toFixed(2)} ${w.toFixed(2)} ${h.toFixed(2)} re f`);if(stroke)this.command(`${rgb(stroke,true)} ${lineWidth.toFixed(2)} w ${x.toFixed(2)} ${y.toFixed(2)} ${w.toFixed(2)} ${h.toFixed(2)} re S`);}
    line(x1,y1,x2,y2,color=COLORS.line,width=1){this.command(`${rgb(color,true)} ${width.toFixed(2)} w ${x1.toFixed(2)} ${y1.toFixed(2)} m ${x2.toFixed(2)} ${y2.toFixed(2)} l S`);}
    drawText(text,x,y,size=10,font='F1',color=COLORS.ink){this.command(`BT /${font} ${size.toFixed(2)} Tf ${rgb(color)} 1 0 0 1 ${x.toFixed(2)} ${y.toFixed(2)} Tm ${pdfLiteral(text)} Tj ET`);}
    ensure(h){if(this.y-h<PAGE.bottom)this.newPage(false);}
    text(text,opt={}){const size=opt.size||10.1,leading=opt.leading||size*1.36,font=opt.font||'F1',bold=font==='F2',x=opt.x??PAGE.left,width=opt.width??WIDTH,color=opt.color||COLORS.ink,before=opt.before??0,after=opt.after??5;const lines=wrapText(text,size,width,bold);this.ensure(before+Math.max(1,lines.length)*leading+after);this.y-=before;for(const line of lines){if(line)this.drawText(line,x,this.y,size,font,color);this.y-=leading;}this.y-=after;return lines.length*leading;}
    bodyHeader(){this.rect(0,758,PAGE.width,34,COLORS.navy);this.drawText('CANADA-U.S. TRADE NEGOTIATION | PRINCIPAL DECISION BRIEF',PAGE.left,771,7.8,'F2',COLORS.white);this.y=PAGE.top;}
    section(num,title){this.ensure(44);this.y-=7;this.drawText(String(num).padStart(2,'0'),PAGE.left,this.y,8.2,'F2',COLORS.red);this.drawText(title.toUpperCase(),PAGE.left+26,this.y,8.2,'F2',COLORS.navy2);this.y-=9;this.line(PAGE.left,this.y,PAGE.left+WIDTH,this.y,COLORS.line,.8);this.y-=17;this.drawText(title,PAGE.left,this.y,15.2,'F2',COLORS.navy);this.y-=20;}
    paragraph(text){this.text(text,{size:10.05,leading:13.7,after:6});}
    bullet(text){this.ensure(28);this.drawText('-',PAGE.left+3,this.y,10,'F2',COLORS.red);this.text(text,{x:PAGE.left+16,width:WIDTH-16,size:9.8,leading:13.2,after:4});}
    callout(title,lines,tone='decision'){
      const fill=tone==='warning'?COLORS.amberBg:tone==='positive'?COLORS.greenBg:COLORS.panel,accent=tone==='warning'?COLORS.amber:tone==='positive'?COLORS.green:COLORS.red;
      const body=Array.isArray(lines)?lines:[lines];let h=22;for(const t of body)h+=wrapText(t,9.6,WIDTH-34,false).length*12.8+3;h+=10;this.ensure(h+8);const y0=this.y-h+5;this.rect(PAGE.left,y0,WIDTH,h,fill,COLORS.line,.6);this.rect(PAGE.left,y0,4,h,accent);this.drawText(title.toUpperCase(),PAGE.left+14,this.y-12,8.2,'F2',accent);let yy=this.y-30;for(const t of body){for(const line of wrapText(t,9.6,WIDTH-34,false)){this.drawText(line,PAGE.left+14,yy,9.6,'F1',COLORS.ink);yy-=12.8;}yy-=3;}this.y=y0-8;
    }
    metricRow(metrics){const gap=8,w=(WIDTH-gap*(metrics.length-1))/metrics.length,h=66;this.ensure(h+10);const y0=this.y-h;metrics.forEach((m,i)=>{const x=PAGE.left+i*(w+gap);this.rect(x,y0,w,h,COLORS.panel,COLORS.line,.6);this.drawText(String(m.label).toUpperCase(),x+10,this.y-15,6.9,'F2',COLORS.muted);this.drawText(m.value,x+10,this.y-36,15,'F2',m.tone==='good'?COLORS.green:m.tone==='warn'?COLORS.amber:COLORS.navy);const noteLines=wrapText(m.note||'',7.1,w-20,false).slice(0,2);let yy=this.y-50;for(const line of noteLines){this.drawText(line,x+10,yy,7.1,'F1',COLORS.slate);yy-=9;}});this.y=y0-12;}
    packageCard(label,pkg,authority){
      if(!pkg){this.callout(label,['No package is available in the current frontier.'],'warning');return;}
      const termLines=pkg.issues.filter(i=>n(i.canadaMove)>0.1||n(i.usMove)>0.1).sort((a,b)=>n(b.canadaMove)+n(b.usMove)-n(a.canadaMove)-n(a.usMove)).slice(0,5).map(i=>`${i.label||issueLabel(i.id)} - Canada ${fmt(i.canadaMove,0)}/100; U.S. ${fmt(i.usMove,0)}/100.`);
      const h=170+Math.max(0,termLines.length-3)*12;this.ensure(h+8);const y0=this.y-h;this.rect(PAGE.left,y0,WIDTH,h,COLORS.white,COLORS.line,.8);this.rect(PAGE.left,y0,5,h,label.toLowerCase().includes('best')?COLORS.red:COLORS.navy2);this.drawText(label.toUpperCase(),PAGE.left+16,this.y-18,8,'F2',COLORS.muted);this.drawText(`${pkg.id} | ${pkg.strategy}`,PAGE.left+16,this.y-39,13.2,'F2',COLORS.navy);this.drawText(authority?.status||'',PAGE.left+16,this.y-56,8.2,'F2',authority?.blocked?COLORS.red:authority?.escalation?COLORS.amber:COLORS.green);
      const cols=[['Both clear',pct(pkg.jointClear)],['Canada CVaR10',signed(pkg.canadaCvar,2)],['U.S. CVaR10',signed(pkg.usCvar,2)],['Max regret',fmt(pkg.maxRegret,2)]];const cw=(WIDTH-32)/4;cols.forEach((v,i)=>{const x=PAGE.left+16+i*cw;this.drawText(v[0].toUpperCase(),x,this.y-79,6.5,'F2',COLORS.muted);this.drawText(v[1],x,this.y-96,11.5,'F2',COLORS.ink);});
      this.drawText(`Canada 95% surplus interval ${fmt(pkg.canadaCi[0],2)} to ${fmt(pkg.canadaCi[1],2)} | U.S. ${fmt(pkg.usCi[0],2)} to ${fmt(pkg.usCi[1],2)} | stability ${fmt(pkg.stability,0)}/100`,PAGE.left+16,this.y-116,7.7,'F1',COLORS.slate);
      let yy=this.y-137;for(const t of termLines){for(const line of wrapText(t,8.3,WIDTH-32,false).slice(0,2)){this.drawText(line,PAGE.left+16,yy,8.3,'F1',COLORS.ink);yy-=10.8;}}
      this.y=y0-10;
    }
    sourceRow(source,index){const title=`${index}. ${source.agency} - ${source.dataset}`;this.text(title,{size:8.8,leading:11.5,font:'F2',after:1});this.text(`Vintage: ${source.vintage} | Status: ${source.status}${source.domain?` | ${source.domain}`:''}`,{size:7.8,leading:10.2,color:COLORS.slate,after:5});}
    cover(){
      const m=this.model;this.rect(0,0,PAGE.width,PAGE.height,COLORS.white);this.rect(0,530,PAGE.width,262,COLORS.navy);this.rect(PAGE.left,570,5,145,COLORS.red);this.drawText(m.classification,PAGE.left+18,711,8.2,'F2',COLORS.white);this.drawText('CANADA - UNITED STATES',PAGE.left+18,671,12,'F2',[0.76,0.82,0.88]);this.drawText('TRADE NEGOTIATION',PAGE.left+18,642,24,'F2',COLORS.white);this.drawText('Principal Decision Brief',PAGE.left+18,607,21,'F2',COLORS.white);this.drawText(`Round ${m.round} | ${cap(m.phase)} | ${m.displayTime}`,PAGE.left+18,578,8.8,'F1',[0.82,0.86,0.90]);
      this.y=493;this.callout('Decision required today',m.decisionRequired.slice(0,3),m.bestAuthority?.blocked?'warning':'decision');
      const best=m.best;this.metricRow([
        {label:'Robust package',value:best?.id||'None',note:best?.strategy||'No strategy',tone:best?'':'warn'},
        {label:'Both clear',value:best?pct(best.jointClear):'n/a',note:`Required ${pct((typeof result!=='undefined'?result?.robustness?.requiredJointClearProbability:0)||0)}`,tone:best&&best.jointClear>=((typeof result!=='undefined'?result?.robustness?.requiredJointClearProbability:0)||0)?'good':'warn'},
        {label:'Worst-tail floor',value:best?signed(Math.min(best.canadaCvar,best.usCvar),2):'n/a',note:'Weaker-country CVaR10',tone:best&&Math.min(best.canadaCvar,best.usCvar)>=0?'good':'warn'},
        {label:'Calibration',value:`${fmt(m.calibrationCompleteness,0)}%`,note:`${m.calibrationGrade}${m.empiricalCertified?' | certified':' | partial'}`,tone:m.empiricalCertified?'good':'warn'}
      ]);
      this.text(m.whereWeAre[0]||'',{size:10.3,leading:14,font:'F2',color:COLORS.navy,after:8});this.text(m.caution,{size:7.8,leading:10.5,color:COLORS.muted,after:0});
    }
  }

  function buildPdf(model){
    const d=new PdfLayout(model);d.cover();d.newPage(false);
    d.section(1,'Where we are');model.whereWeAre.forEach(x=>d.bullet(x));
    d.section(2,'What they want');model.whatTheyWant.forEach(x=>d.bullet(x));
    d.section(3,'What we want');model.whatWeWant.forEach(x=>d.bullet(x));
    d.section(4,'What changed');model.whatChanged.forEach(x=>d.bullet(x));
    d.section(5,'Red lines');d.text('Mandate and authority constraints',{font:'F2',size:10.3,after:4});model.redLines.mandate.forEach(x=>d.bullet(x));d.text('Analytical guardrails',{font:'F2',size:10.3,before:4,after:4});model.redLines.analytical.forEach(x=>d.bullet(x));
    d.section(6,'Best package');d.packageCard('Best robust package',model.best,model.bestAuthority);
    d.section(7,'Bridge package');d.packageCard('Bridge package',model.bridge,model.bridgeAuthority);
    d.section(8,'BATNA');d.metricRow([{label:'Canada BATNA',value:fmt(model.batna.canada,2),note:model.batna.canadaStrategy},{label:'Canada reservation',value:fmt(model.batna.canadaReservation,2),note:'Deal must clear this modeled floor'},{label:'U.S. BATNA',value:fmt(model.batna.us,2),note:model.batna.usStrategy},{label:'U.S. reservation',value:fmt(model.batna.usReservation,2),note:'Deal must clear this modeled floor'}]);d.paragraph('BATNA and reservation values are model constructs used to screen individual rationality. They are not observed political walk-away points unless separately validated by the delegation.');
    d.section(9,'Key uncertainties');model.uncertainties.forEach(x=>d.bullet(x));
    d.section(10,'Decision required today');d.callout('Principal decision',model.decisionRequired,model.bestAuthority?.blocked?'warning':'decision');
    d.section(11,'Recommended language');for(const item of model.recommendedLanguage){d.text(item.label.toUpperCase(),{size:8,font:'F2',color:COLORS.red,after:2});d.text(`“${item.text}”`,{size:9.7,leading:13.2,font:'F3',color:COLORS.ink,after:8});}
    d.section(12,'Evidence sources');d.paragraph(`Calibration snapshot ${model.snapshotId} is ${fmt(model.calibrationCompleteness,0)}% complete (${model.calibrationGrade}). Evidence below separates official source material, model uncertainty, and local negotiation records.`);model.evidenceSources.slice(0,14).forEach((s,i)=>d.sourceRow(s,i+1));
    if(model.notes){d.section(13,'Principal / delegation notes');d.paragraph(model.notes);}
    d.callout('Analytical caution',[model.caution,model.fingerprint],'warning');

    const objects=[],pageStart=7,contentStart=pageStart+d.pages.length;
    objects[1]='<< /Type /Catalog /Pages 2 0 R >>';
    objects[2]=`<< /Type /Pages /Count ${d.pages.length} /Kids [${d.pages.map((_,i)=>`${pageStart+i} 0 R`).join(' ')}] >>`;
    objects[3]='<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>';
    objects[4]='<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>';
    objects[5]='<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique /Encoding /WinAnsiEncoding >>';
    const stamp=new Date(model.generatedAt).toISOString().replace(/[-:T]/g,'').slice(0,14)+'Z';
    objects[6]=`<< /Title ${pdfLiteral(`${model.title} - ${model.subtitle}`)} /Subject ${pdfLiteral('Principal-quality Canada-U.S. negotiation decision brief')} /Producer ${pdfLiteral('Canada-U.S. Diplomatic Policy Studio')} /CreationDate ${pdfLiteral('D:'+stamp)} >>`;
    d.pages.forEach((p,i)=>{
      const pageId=pageStart+i,streamId=contentStart+i;const commands=[...p.commands];
      if(!p.cover){commands.push(`${rgb(COLORS.line,true)} .7 w ${PAGE.left} 43 m ${PAGE.width-PAGE.right} 43 l S`);commands.push(`BT /F1 7.2 Tf ${rgb(COLORS.muted)} 1 0 0 1 ${PAGE.left} 28 Tm ${pdfLiteral(model.fingerprint)} Tj ET`);}
      commands.push(`BT /F1 7.4 Tf ${rgb(p.cover?[0.45,0.49,0.54]:COLORS.muted)} 1 0 0 1 520 28 Tm ${pdfLiteral(`Page ${i+1}/${d.pages.length}`)} Tj ET`);
      const stream=commands.join('\n');objects[pageId]=`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${PAGE.width} ${PAGE.height}] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> >> /Contents ${streamId} 0 R >>`;objects[streamId]=`<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`;
    });
    let pdf='%PDF-1.4\n%\xE2\xE3\xCF\xD3\n';const offsets=[0];for(let i=1;i<objects.length;i++){offsets[i]=pdf.length;pdf+=`${i} 0 obj\n${objects[i]}\nendobj\n`;}const xref=pdf.length;pdf+=`xref\n0 ${objects.length}\n0000000000 65535 f \n`;for(let i=1;i<objects.length;i++)pdf+=`${String(offsets[i]).padStart(10,'0')} 00000 n \n`;pdf+=`trailer\n<< /Size ${objects.length} /Root 1 0 R /Info 6 0 R >>\nstartxref\n${xref}\n%%EOF\n`;const bytes=new Uint8Array(pdf.length);for(let i=0;i<pdf.length;i++)bytes[i]=pdf.charCodeAt(i)&0xff;return bytes;
  }

  function previewHtml(m){
    const bullets=items=>`<ul>${items.map(x=>`<li>${esc(x)}</li>`).join('')}</ul>`;
    const pkg=(label,p,a)=>p?`<article class="principal-package"><div><small>${esc(label)}</small><h3>${esc(p.id)} · ${esc(p.strategy)}</h3><span class="principal-authority">${esc(a?.status||'')}</span></div><div class="principal-metrics"><span><b>${pct(p.jointClear)}</b><small>both clear</small></span><span><b>${signed(p.canadaCvar,2)}</b><small>Canada CVaR10</small></span><span><b>${signed(p.usCvar,2)}</b><small>U.S. CVaR10</small></span><span><b>${fmt(p.maxRegret,2)}</b><small>max regret</small></span></div></article>`:'<p>No package available.</p>';
    return `<div class="principal-preview"><header><div><span>${esc(m.classification)}</span><h1>${esc(m.title)}</h1><h2>${esc(m.subtitle)}</h2><p>Round ${m.round} · ${esc(cap(m.phase))} · ${esc(m.displayTime)}</p></div><aside><b>${fmt(m.calibrationCompleteness,0)}%</b><span>calibration completeness</span></aside></header><section class="principal-decision"><small>DECISION REQUIRED TODAY</small>${bullets(m.decisionRequired)}</section><div class="principal-grid"><section><h2>Where we are</h2>${bullets(m.whereWeAre)}</section><section><h2>What changed</h2>${bullets(m.whatChanged)}</section><section><h2>What they want</h2>${bullets(m.whatTheyWant)}</section><section><h2>What we want</h2>${bullets(m.whatWeWant)}</section></div><section><h2>Red lines</h2>${bullets(m.redLines.mandate)}</section><div class="principal-package-grid">${pkg('Best robust package',m.best,m.bestAuthority)}${pkg('Bridge package',m.bridge,m.bridgeAuthority)}</div><section><h2>BATNA</h2><p>Canada ${fmt(m.batna.canada,2)} (${esc(m.batna.canadaStrategy)}) · reservation ${fmt(m.batna.canadaReservation,2)}. U.S. ${fmt(m.batna.us,2)} (${esc(m.batna.usStrategy)}) · reservation ${fmt(m.batna.usReservation,2)}.</p></section><section><h2>Key uncertainties</h2>${bullets(m.uncertainties)}</section><section><h2>Recommended language</h2>${m.recommendedLanguage.map(x=>`<div class="principal-language"><b>${esc(x.label)}</b><p>“${esc(x.text)}”</p></div>`).join('')}</section><section><h2>Evidence sources</h2><ol>${m.evidenceSources.slice(0,12).map(x=>`<li><b>${esc(x.agency)}</b> — ${esc(x.dataset)} <small>${esc(x.vintage)} · ${esc(x.status)}${x.domain?` · ${esc(x.domain)}`:''}</small></li>`).join('')}</ol></section><footer>${esc(m.caution)}<br>${esc(m.fingerprint)}</footer></div>`;
  }

  async function roomState(){try{return await fetch('/api/room',{cache:'no-store'}).then(r=>r.ok?r.json():{})}catch(_){return{}}}
  function currentInputs(){return typeof settings!=='undefined'?settings:{};}
  function currentResult(){return typeof result!=='undefined'?result:null;}
  function currentRedlines(){return safeJson(REDLINE_KEY,DEFAULT_REDLINES);}
  function currentNotes(){try{return localStorage.getItem(NOTES_KEY)||''}catch(_){return''}}
  function currentLedger(){return safeJson(DECISION_LEDGER_KEY,[]);}
  async function currentModel(){return buildBriefingModel({result:currentResult(),room:await roomState(),settings:currentInputs(),redlines:currentRedlines(),notes:currentNotes(),ledger:currentLedger()});}

  function filename(){const d=new Date(),pad=v=>String(v).padStart(2,'0');return `Canada-US_Principal-Brief_R${(typeof roomStateCache!=='undefined'&&roomStateCache?.round)||''}_${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}_${pad(d.getHours())}${pad(d.getMinutes())}.pdf`.replace('_R_','_');}
  async function saveBlob(blob,name){if(typeof window.showSaveFilePicker==='function'){try{const handle=await window.showSaveFilePicker({suggestedName:name,types:[{description:'PDF document',accept:{'application/pdf':['.pdf']}}]});const writable=await handle.createWritable();await writable.write(blob);await writable.close();return true;}catch(error){if(error?.name==='AbortError')return false;}}const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;a.style.display='none';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1500);return true;}
  async function savePrincipalPdf(){const model=await currentModel();if(!model.best){document.querySelector('#openBriefing')?.click();return false;}const bytes=buildPdf(model);const saved=await saveBlob(new Blob([bytes],{type:'application/pdf'}),`Canada-US_Principal-Brief_R${model.round}_${model.generatedAt.slice(0,10)}.pdf`);if(saved){for(const b of [document.querySelector('#printBriefing'),document.querySelector('#saveBriefingPdfDialog')].filter(Boolean)){const old=b.textContent;b.textContent='Principal PDF saved';setTimeout(()=>b.textContent=old,1500);}}return saved;}
  async function refreshPreview(){const sheet=document.querySelector('#briefingSheet');if(!sheet||!currentResult()?.scenarios?.length)return;const model=await currentModel();sheet.innerHTML=previewHtml(model);sheet.classList.add('principal-briefing-sheet');const toolbar=document.querySelector('#diplomaticBriefing .briefing-toolbar b');if(toolbar)toolbar.textContent='Principal decision brief';}
  async function copyPrincipal(){const model=await currentModel();const text=[`${model.title.toUpperCase()} - ${model.subtitle.toUpperCase()}`,model.classification,`Generated ${model.displayTime}`,`Round ${model.round} - ${model.phase}`,'','DECISION REQUIRED TODAY',...model.decisionRequired.map(x=>`- ${x}`),'','WHERE WE ARE',...model.whereWeAre.map(x=>`- ${x}`),'','WHAT THEY WANT',...model.whatTheyWant.map(x=>`- ${x}`),'','WHAT WE WANT',...model.whatWeWant.map(x=>`- ${x}`),'','WHAT CHANGED',...model.whatChanged.map(x=>`- ${x}`),'','RED LINES',...model.redLines.mandate.map(x=>`- ${x}`),'','BEST PACKAGE',model.best?`${model.best.id} - ${model.best.strategy}; joint clearance ${pct(model.best.jointClear)}; Canada CVaR10 ${signed(model.best.canadaCvar,2)}; U.S. CVaR10 ${signed(model.best.usCvar,2)}; max regret ${fmt(model.best.maxRegret,2)}.`:'No package.','','BRIDGE PACKAGE',model.bridge?`${model.bridge.id} - ${model.bridge.strategy}; joint clearance ${pct(model.bridge.jointClear)}.`:'No bridge.','','BATNA',`Canada ${fmt(model.batna.canada,2)} / reservation ${fmt(model.batna.canadaReservation,2)}; U.S. ${fmt(model.batna.us,2)} / reservation ${fmt(model.batna.usReservation,2)}.`,'','KEY UNCERTAINTIES',...model.uncertainties.map(x=>`- ${x}`),'','RECOMMENDED LANGUAGE',...model.recommendedLanguage.map(x=>`${x.label}: ${x.text}`),'','EVIDENCE SOURCES',...model.evidenceSources.map((x,i)=>`${i+1}. ${x.agency} - ${x.dataset} (${x.vintage}; ${x.status})`),'',model.caution,model.fingerprint].join('\n');if(navigator.clipboard?.writeText)await navigator.clipboard.writeText(text);return text;}

  function relabel(){const top=document.querySelector('#printBriefing');if(top)top.textContent='Save principal brief PDF';const open=document.querySelector('#openBriefing');if(open)open.textContent='Open principal brief';const dialog=document.querySelector('#diplomaticBriefing');if(dialog){const save=document.querySelector('#saveBriefingPdfDialog')||Array.from(dialog.querySelectorAll('button')).find(b=>/Save PDF|Print/.test(b.textContent));if(save){save.id='saveBriefingPdfDialog';save.textContent='Save principal PDF';}}}
  function bind(){
    relabel();
    document.addEventListener('click',event=>{if(event.target.closest('#openBriefing')||event.target.closest('#printBriefing'))setTimeout(refreshPreview,0);},true);
    const copy=document.querySelector('#copyBriefing');if(copy)copy.addEventListener('click',event=>{event.preventDefault();event.stopImmediatePropagation();copyPrincipal().then(()=>{const old=copy.textContent;copy.textContent='Principal brief copied';setTimeout(()=>copy.textContent=old,1200);});},true);
    const dialog=document.querySelector('#diplomaticBriefing');if(dialog)new MutationObserver(relabel).observe(dialog,{childList:true,subtree:true});
  }

  window.PrincipalBriefing={buildModel:buildBriefingModel,buildPdf,previewHtml,save:savePrincipalPdf,copy:copyPrincipal};
  if(window.BriefingPdf)window.BriefingPdf.save=savePrincipalPdf;
  window.print=savePrincipalPdf;
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',bind);else bind();
})();
