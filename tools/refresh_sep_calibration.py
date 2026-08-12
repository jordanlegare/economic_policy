#!/usr/bin/env python3
"""Refresh/verify a frozen quarterly structural-estimation panel.

The estimation state uses Bank of Canada Staff Economic Projections (SEP)
real-time vintages. Statistics Canada revised GDP/labour observations are
retained as diagnostic columns only. --verify is deterministic and offline.
"""
from __future__ import annotations
import argparse,csv,hashlib,io,json,math,statistics,sys,urllib.request,zipfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path

VAL="https://www.bankofcanada.ca/valet/observations/{}/json"
STAT_GDP="https://www150.statcan.gc.ca/n1/en/tbl/csv/36100104-eng.zip"
STAT_LFS="https://www150.statcan.gc.ca/n1/en/tbl/csv/14100287-eng.zip"
PREFIX={"output_gap":"SWP-LYGAP","core_index":"SWP-PCPIX","policy_rate":"SWP-R1N","usdcad":"SWP-USCANPFX"}
START,END,NEUTRAL="2001Q1","2019Q4",2.75


def get(url):
    req=urllib.request.Request(url,headers={"User-Agent":"CanadaPolicyStudio/2.0"})
    with urllib.request.urlopen(req,timeout=60) as r:return r.read()

def qkey(q):return int(q[:4]),int(q[-1])
def qshift(q,n):
    y,k=qkey(q); x=y*4+k-1+n; return f"{x//4}Q{x%4+1}"
def vintages():
    out=[]; q=START
    while qkey(q)<=qkey(END):out.append(q);q=qshift(q,1)
    return out

def parse_valet(raw,series):
    out={}
    for o in json.loads(raw).get("observations",[]):
        d=str(o.get("d","")); cell=o.get(series) or o.get(series.lower()) or o.get(series.upper())
        if "Q" not in d.upper() or not isinstance(cell,dict):continue
        try:out[d[:6].replace("-","").upper()]=float(cell["v"])
        except (KeyError,TypeError,ValueError):pass
    return out

def fetch_series(q,field,prefix):
    series=f"{prefix}{q}"
    for code in (series,series.lower()):
        try:
            raw=get(VAL.format(code)); values=parse_valet(raw,series)
            if values:return q,field,series,raw,values
        except Exception:pass
    return q,field,series,b"",{}

def sep_panel():
    gathered=defaultdict(dict); hashes={f:hashlib.sha256() for f in PREFIX}; counts=defaultdict(int); failures=[]
    jobs=[(q,f,p) for q in vintages() for f,p in PREFIX.items()]
    with ThreadPoolExecutor(max_workers=16) as pool:
        futures=[pool.submit(fetch_series,*j) for j in jobs]
        for future in as_completed(futures):
            q,f,s,raw,values=future.result()
            if not values:failures.append(f"{f}:{q}");continue
            gathered[q][f]=values;hashes[f].update(s.encode());hashes[f].update(raw);counts[f]+=1
    rows=[]; skipped=[]
    for q in vintages():
        fields=gathered[q]
        if any(f not in fields for f in PREFIX):skipped.append(q);continue
        try:
            gap=fields["output_gap"][q]; rate=fields["policy_rate"][q]; fx=fields["usdcad"][q]
            now=fields["core_index"][q]; prev=fields["core_index"][qshift(q,-4)]
        except KeyError:skipped.append(q);continue
        core=100*(now/prev-1) if now>0 and prev>0 else float("nan")
        if not(-15<=gap<=15 and 0<=rate<=20 and .5<=fx<=2.5 and -5<=core<=15):skipped.append(q);continue
        rows.append({"quarter":q,"output_gap":gap,"core_inflation":core,"policy_rate":rate,"usdcad":fx})
    if len(rows)<60:raise RuntimeError(f"Only {len(rows)} complete SEP vintages; skipped={skipped}; fetch_failures={failures[:20]}")
    labels={"output_gap":"SEP output gap SWP-LYGAP{vintage}","core_index":"SEP core CPI SWP-PCPIX{vintage}","policy_rate":"SEP implied policy rate SWP-R1N{vintage}","usdcad":"SEP U.S./CAD rate SWP-USCANPFX{vintage}"}
    manifest=[{"source_id":f"boc_sep_{f}","agency":"Bank of Canada","dataset":labels[f],"url":"https://www.bankofcanada.ca/rates/staff-economic-projections/","sha256":hashes[f].hexdigest(),"observations":counts[f],"transformation":"current-quarter real-time vintage; PCPIX converted to year-over-year inflation" if f=="core_index" else "current-quarter real-time vintage"} for f in PREFIX]
    return rows,manifest


def norm(x):return " ".join(str(x).strip().lower().replace("—","-").split())
def col(row,*tokens):
    for k in row:
        if all(t in norm(k) for t in tokens):return k
    return None

def zrows(url):
    raw=get(url)
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        name=max([n for n in z.namelist() if n.lower().endswith(".csv") and "metadata" not in n.lower()],key=lambda n:z.getinfo(n).file_size)
        text=z.read(name).decode("utf-8-sig")
    return list(csv.DictReader(io.StringIO(text))),raw

def qdate(s):
    y=int(s[:4]);m=int(s[5:7]);return f"{y}Q{(m-1)//3+1}"
def stat_gdp(rows):
    r=rows[0];ref=col(r,"ref_date") or col(r,"ref","date");geo=col(r,"geo");est=col(r,"estimate");price=col(r,"price");sa=col(r,"seasonal");val=col(r,"value")
    if not all((ref,geo,est,price,sa,val)):raise RuntimeError("Unexpected StatCan GDP schema")
    lev={}
    for r in rows:
        if norm(r[geo])!="canada" or "gross domestic product at market prices" not in norm(r[est]) or "chained" not in norm(r[price]) or "seasonally adjusted at annual rates" not in norm(r[sa]):continue
        try:lev[qdate(r[ref])]=float(r[val])
        except (TypeError,ValueError):pass
    return {q:400*math.log(v/lev[qshift(q,-1)]) for q,v in lev.items() if qshift(q,-1) in lev and v>0 and lev[qshift(q,-1)]>0}
def stat_unemployment(rows):
    r=rows[0];ref=col(r,"ref_date") or col(r,"ref","date");geo=col(r,"geo");ch=col(r,"labour","force","characteristic");age=col(r,"age");sex=col(r,"gender") or col(r,"sex");dtype=col(r,"data","type");val=col(r,"value")
    if not all((ref,geo,ch,age,sex,dtype,val)):raise RuntimeError("Unexpected StatCan labour schema")
    m=defaultdict(list)
    for r in rows:
        if norm(r[geo])!="canada" or norm(r[ch])!="unemployment rate" or "15 years and over" not in norm(r[age]) or not("total" in norm(r[sex]) or "both" in norm(r[sex])) or "seasonally adjusted" not in norm(r[dtype]):continue
        try:m[qdate(r[ref])].append(float(r[val]))
        except (TypeError,ValueError):pass
    return {q:statistics.fmean(v) for q,v in m.items() if v}


def solve(a,b):
    n=len(a);aug=[list(a[i])+[b[i]] for i in range(n)]
    for c in range(n):
        p=max(range(c,n),key=lambda r:abs(aug[r][c]));aug[c],aug[p]=aug[p],aug[c]
        if abs(aug[c][c])<1e-12:raise RuntimeError("Singular matrix")
        s=aug[c][c];aug[c]=[v/s for v in aug[c]]
        for r in range(n):
            if r==c:continue
            f=aug[r][c];aug[r]=[aug[r][j]-f*aug[c][j] for j in range(n+1)]
    return [aug[i][-1] for i in range(n)]
def inv(a):
    n=len(a);aug=[list(a[i])+[1. if i==j else 0. for j in range(n)] for i in range(n)]
    for c in range(n):
        p=max(range(c,n),key=lambda r:abs(aug[r][c]));aug[c],aug[p]=aug[p],aug[c]
        if abs(aug[c][c])<1e-12:raise RuntimeError("Singular covariance matrix")
        s=aug[c][c];aug[c]=[v/s for v in aug[c]]
        for r in range(n):
            if r==c:continue
            f=aug[r][c];aug[r]=[aug[r][j]-f*aug[c][j] for j in range(2*n)]
    return [r[n:] for r in aug]
def ols(x,y):
    k=len(x[0]);xtx=[[sum(r[i]*r[j] for r in x) for j in range(k)] for i in range(k)];xty=[sum(r[i]*v for r,v in zip(x,y)) for i in range(k)];b=solve(xtx,xty);res=[v-sum(bb*xx for bb,xx in zip(b,r)) for r,v in zip(x,y)];d=max(1,len(y)-k);s2=sum(e*e for e in res)/d;i=inv(xtx);se=[math.sqrt(max(0,s2*i[j][j])) for j in range(k)];sd=math.sqrt(sum(e*e for e in res)/max(1,len(res)-1));return b,se,res,sd

def policy_fit(rows):
    by={r["quarter"]:r for r in rows};obs=[]
    for r in rows:
        p=by.get(qshift(r["quarter"],-1))
        if p:obs.append((float(p["policy_rate"]),float(r["policy_rate"]),float(r["core_inflation"]),float(r["output_gap"])))
    best=None;center=None
    for stage in range(3):
        if stage==0:A=[.25+.1*i for i in range(23)];B=[.05*i for i in range(21)];S=[.125+.025*i for i in range(36)]
        else:
            a,b,s=center;da=.025 if stage==1 else .005;db=.0125 if stage==1 else .0025;ds=.0125 if stage==1 else .0025;A=[max(.01,a+da*i) for i in range(-4,5)];B=[max(0,b+db*i) for i in range(-4,5)];S=[max(.025,s+ds*i) for i in range(-4,5)]
        for a in A:
            for b in B:
                for s in S:
                    err=0.
                    for prev,actual,pi,gap in obs:
                        target=NEUTRAL+a*(pi-2)+b*gap;pred=max(0,min(8,prev+max(-s,min(s,target-prev))));err+=(actual-pred)**2
                    if best is None or err<best[0]:best=(err,a,b,s)
        center=best[1:]
    return best[1],best[2],best[3],math.sqrt(best[0]/len(obs)),len(obs)

def estimate(rows):
    rows=sorted(rows,key=lambda r:qkey(r["quarter"]));by={r["quarter"]:r for r in rows};xg=[];yg=[];qg=[];xp=[];yp=[];qp=[]
    for r in rows:
        p=by.get(qshift(r["quarter"],-1))
        if not p:continue
        xg.append([float(p["output_gap"]),-(float(r["policy_rate"])-NEUTRAL)]);yg.append(float(r["output_gap"]));qg.append(r["quarter"])
        xp.append([float(p["core_inflation"])-2,float(r["output_gap"]),float(r["usdcad"])-1.34]);yp.append(float(r["core_inflation"])-2);qp.append(r["quarter"])
    bg,seg,rg,sdg=ols(xg,yg);bp,sep,rp,sdp=ols(xp,yp);a,b,s,rmse,np=policy_fit(rows)
    g=dict(zip(qg,rg));p=dict(zip(qp,rp));common=sorted(set(g)&set(p),key=qkey);gv=[g[q] for q in common];pv=[p[q] for q in common];gm=statistics.fmean(gv);pm=statistics.fmean(pv);den=max(1,len(common)-1);cov=sum((x-gm)*(y-pm) for x,y in zip(gv,pv))/den;sg=math.sqrt(sum((x-gm)**2 for x in gv)/den);sp=math.sqrt(sum((y-pm)**2 for y in pv)/den);corr=cov/(sg*sp) if sg and sp else 0
    def R(name,v,se,lo,hi,n,method,ok,note):return {"parameter":name,"estimate":v,"standard_error":se,"lower_bound":lo,"upper_bound":hi,"observations":n,"sample_start":rows[0]["quarter"],"sample_end":rows[-1]["quarter"],"method":method,"direct_eligible":"true" if ok else "false","notes":note}
    E=[R("output_persistence",bg[0],seg[0],max(.05,bg[0]-1.96*seg[0]),min(.98,bg[0]+1.96*seg[0]),len(yg),"OLS production-form output-gap equation",.05<bg[0]<.98,"Real-time SEP vintages"),R("real_rate_demand_sensitivity",bg[1],seg[1],max(.001,bg[1]-1.96*seg[1]),max(.002,bg[1]+1.96*seg[1]),len(yg),"OLS production-form output-gap equation",bg[1]>0,"Coefficient on -(staff rate-neutral rate)"),R("output_shock_sd",sdg,0,max(.001,.7*sdg),1.3*sdg,len(yg),"residual SD of output equation",sdg>0,"Quarterly residual innovation"),R("inflation_persistence",bp[0],sep[0],max(.05,bp[0]-1.96*sep[0]),min(.98,bp[0]+1.96*sep[0]),len(yp),"OLS production-form centered inflation equation",.05<bp[0]<.98,"PCPIX year-over-year within each vintage"),R("phillips_curve_slope",bp[1],sep[1],bp[1]-1.96*sep[1],bp[1]+1.96*sep[1],len(yp),"OLS production-form centered inflation equation",bp[1]>0,"Contemporaneous SEP output gap"),R("fx_pass_through",bp[2],sep[2],bp[2]-1.96*sep[2],bp[2]+1.96*sep[2],len(yp),"OLS production-form centered inflation equation",bp[2]>0,"Omitted oil/import/supply terms remain in residual"),R("inflation_shock_sd",sdp,0,max(.001,.7*sdp),1.3*sdp,len(yp),"residual SD of inflation equation",sdp>0,"Quarterly residual innovation"),R("rate_inflation_response",a,0,max(.01,.65*a),1.35*a,np,"grid fit exact clipped production policy rule",a>0 and rmse<1,f"SEP implied-rate path RMSE={rmse:.4f} pp"),R("rate_output_response",b,0,max(.001,.65*b),max(.002,1.35*b),np,"grid fit exact clipped production policy rule",b>0 and rmse<1,f"SEP implied-rate path RMSE={rmse:.4f} pp"),R("max_quarterly_rate_step",s,0,max(.025,.75*s),1.25*s,np,"grid fit exact clipped production policy rule",s>0 and rmse<1,f"SEP implied-rate path RMSE={rmse:.4f} pp")]
    C=[{"row":"output_gap_residual","column":"output_gap_residual","covariance":sg*sg,"correlation":1.,"observations":len(common)},{"row":"output_gap_residual","column":"inflation_residual","covariance":cov,"correlation":corr,"observations":len(common)},{"row":"inflation_residual","column":"output_gap_residual","covariance":cov,"correlation":corr,"observations":len(common)},{"row":"inflation_residual","column":"inflation_residual","covariance":sp*sp,"correlation":1.,"observations":len(common)}]
    return E,C

def write(path,fields,rows):
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();[w.writerow({k:(f"{r.get(k):.10g}" if isinstance(r.get(k),float) else r.get(k,"")) for k in fields}) for r in rows]
def read(path):
    with path.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def refresh(root):
    panel,manifest=sep_panel();gr,graw=zrows(STAT_GDP);lr,lraw=zrows(STAT_LFS);g=stat_gdp(gr);u=stat_unemployment(lr)
    for r in panel:r["statcan_gdp_growth"]=g.get(r["quarter"],"");r["statcan_unemployment"]=u.get(r["quarter"],"")
    manifest+=[{"source_id":"statcan_gdp_quarterly","agency":"Statistics Canada","dataset":"36-10-0104-01","url":STAT_GDP,"sha256":hashlib.sha256(graw).hexdigest(),"observations":len(g),"transformation":"revised chained-dollar GDP quarterly growth diagnostic"},{"source_id":"statcan_unemployment_monthly","agency":"Statistics Canada","dataset":"14-10-0287-01","url":STAT_LFS,"sha256":hashlib.sha256(lraw).hexdigest(),"observations":len(u),"transformation":"revised quarterly-mean unemployment diagnostic"}]
    E,C=estimate(panel);d=root/"data"/"calibration";write(d/"quarterly_estimation_panel.csv",["quarter","output_gap","core_inflation","policy_rate","usdcad","statcan_gdp_growth","statcan_unemployment"],panel);write(d/"quarterly_estimation_manifest.csv",["source_id","agency","dataset","url","sha256","observations","transformation"],manifest);write(d/"quarterly_structural_estimates.csv",["parameter","estimate","standard_error","lower_bound","upper_bound","observations","sample_start","sample_end","method","direct_eligible","notes"],E);write(d/"quarterly_residual_covariance.csv",["row","column","covariance","correlation","observations"],C);print(f"panel_rows={len(panel)}");[print(f"ESTIMATE {e['parameter']}={e['estimate']:.8g} se={e['standard_error']:.8g} eligible={e['direct_eligible']}") for e in E];print(f"RESIDUAL_CORRELATION={C[1]['correlation']:.8g}")
def verify(root):
    d=root/"data"/"calibration";panel=read(d/"quarterly_estimation_panel.csv");E,C=estimate(panel);old={r["parameter"]:r for r in read(d/"quarterly_structural_estimates.csv")}
    for e in E:
        if e["parameter"] not in old or abs(float(old[e["parameter"]]["estimate"])-e["estimate"])>5e-8 or old[e["parameter"]]["direct_eligible"]!=e["direct_eligible"]:raise RuntimeError(f"Estimate drift {e['parameter']}")
    oc=read(d/"quarterly_residual_covariance.csv")
    if len(oc)!=4 or abs(float(oc[1]["correlation"])-C[1]["correlation"])>5e-8:raise RuntimeError("Residual covariance drift")
    if len(panel)<60:raise RuntimeError("Quarterly panel too short")
    print(f"verified_quarterly_panel rows={len(panel)} direct_eligible={sum(e['direct_eligible']=='true' for e in E)}")
def main():
    p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument("--refresh",action="store_true");g.add_argument("--verify",action="store_true");p.add_argument("--root",default=".");a=p.parse_args();root=Path(a.root).resolve();refresh(root) if a.refresh else verify(root)
if __name__=="__main__":
    try:main()
    except Exception as e:print(f"SEP calibration error: {e}",file=sys.stderr);raise
