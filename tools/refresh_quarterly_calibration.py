#!/usr/bin/env python3
"""Build and verify a frozen quarterly empirical calibration panel.

Refresh uses the Bank of Canada's Staff Economic Projections (SEP) real-time
vintages plus Statistics Canada revised GDP/labour data. Ordinary CI uses only
--verify and therefore has no network dependency.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import statistics
import sys
import urllib.request
import zipfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

VAL_ENDPOINT = "https://www.bankofcanada.ca/valet/observations/{series}/json"
STATCAN_GDP = "https://www150.statcan.gc.ca/n1/en/tbl/csv/36100104-eng.zip"
STATCAN_LFS = "https://www150.statcan.gc.ca/n1/en/tbl/csv/14100287-eng.zip"
PREFIXES = {
    "output_gap": "SWP-LYGAP",
    "core_index": "SWP-PCPIX",
    "policy_rate": "SWP-R1N",
    "usdcad": "SWP-USCANPFX",
}
SAMPLE_START = "2001Q1"
SAMPLE_END = "2019Q4"
NEUTRAL_RATE = 2.75


def request_bytes(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "CanadaPolicyStudio/2.0"})
    with urllib.request.urlopen(req, timeout=60) as response:
        return response.read()


def qkey(q: str):
    return int(q[:4]), int(q[-1])


def shift_quarter(q: str, delta: int) -> str:
    year, quarter = qkey(q)
    index = year * 4 + (quarter - 1) + delta
    return f"{index // 4}Q{index % 4 + 1}"


def vintages(start=SAMPLE_START, end=SAMPLE_END):
    out, q = [], start
    while qkey(q) <= qkey(end):
        out.append(q)
        q = shift_quarter(q, 1)
    return out


def parse_valet(raw: bytes, series: str):
    payload = json.loads(raw)
    out = {}
    for obs in payload.get("observations", []):
        date = str(obs.get("d", ""))
        cell = obs.get(series) or obs.get(series.lower()) or obs.get(series.upper())
        if not date or not isinstance(cell, dict):
            continue
        try:
            value = float(cell.get("v"))
        except (TypeError, ValueError):
            continue
        q = date[:6].replace("-", "").upper() if "Q" in date.upper() else None
        if q and len(q) == 6:
            out[q] = value
    return out


def fetch_sep(vintage: str, field: str, prefix: str):
    series = f"{prefix}{vintage}"
    urls = [VAL_ENDPOINT.format(series=series), VAL_ENDPOINT.format(series=series.lower())]
    last_error = None
    for url in urls:
        try:
            raw = request_bytes(url)
            values = parse_valet(raw, series)
            if values:
                return vintage, field, series, url, raw, values
        except Exception as exc:
            last_error = exc
    raise RuntimeError(f"Unable to fetch {series}: {last_error}")


def sep_panel():
    jobs = [(q, field, prefix) for q in vintages() for field, prefix in PREFIXES.items()]
    gathered = defaultdict(dict)
    hashers = {field: hashlib.sha256() for field in PREFIXES}
    counts = defaultdict(int)
    with ThreadPoolExecutor(max_workers=16) as pool:
        futures = [pool.submit(fetch_sep, *job) for job in jobs]
        for future in as_completed(futures):
            vintage, field, series, url, raw, values = future.result()
            gathered[vintage][field] = values
            hashers[field].update(series.encode())
            hashers[field].update(raw)
            counts[field] += 1

    rows = []
    for q in vintages():
        fields = gathered[q]
        missing = sorted(set(PREFIXES) - set(fields))
        if missing:
            raise RuntimeError(f"Missing SEP fields for {q}: {missing}")
        try:
            gap = fields["output_gap"][q]
            rate = fields["policy_rate"][q]
            fx = fields["usdcad"][q]
            pcix_now = fields["core_index"][q]
            pcix_prev = fields["core_index"][shift_quarter(q, -4)]
        except KeyError as exc:
            raise RuntimeError(f"SEP vintage {q} lacks current/history observation {exc}") from exc
        if pcix_now <= 0 or pcix_prev <= 0:
            raise RuntimeError(f"Invalid PCPIX level in {q}")
        core = 100.0 * (pcix_now / pcix_prev - 1.0)
        if not (-15.0 <= gap <= 15.0 and 0.0 <= rate <= 20.0 and .5 <= fx <= 2.5 and -5.0 <= core <= 15.0):
            raise RuntimeError(f"SEP sanity check failed in {q}: gap={gap} rate={rate} fx={fx} core={core}")
        rows.append({"quarter": q, "output_gap": gap, "core_inflation": core,
                     "policy_rate": rate, "usdcad": fx})
    manifest = []
    labels = {
        "output_gap": "Staff Economic Projections output gap (SWP-LYGAP{vintage})",
        "core_index": "Staff Economic Projections core CPI index (SWP-PCPIX{vintage})",
        "policy_rate": "Staff Economic Projections implied policy rate (SWP-R1N{vintage})",
        "usdcad": "Staff Economic Projections U.S./CAD nominal exchange rate (SWP-USCANPFX{vintage})",
    }
    for field in PREFIXES:
        manifest.append({"source_id": f"boc_sep_{field}", "agency": "Bank of Canada",
                         "dataset": labels[field], "url": "https://www.bankofcanada.ca/rates/staff-economic-projections/",
                         "sha256": hashers[field].hexdigest(), "observations": counts[field],
                         "transformation": "current-quarter staff vintage; PCPIX converted to year-over-year inflation" if field == "core_index" else "current-quarter staff vintage"})
    return rows, manifest


def norm(value):
    return " ".join(str(value).strip().lower().replace("—", "-").split())


def find_col(row, *tokens):
    for key in row:
        nk = norm(key)
        if all(t in nk for t in tokens):
            return key
    return None


def read_statcan_zip(url: str):
    raw = request_bytes(url)
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names = [n for n in archive.namelist() if n.lower().endswith(".csv") and "metadata" not in n.lower()]
        name = max(names, key=lambda n: archive.getinfo(n).file_size)
        text = archive.read(name).decode("utf-8-sig")
    return list(csv.DictReader(io.StringIO(text))), raw


def quarter_from_date(text: str):
    year = int(text[:4]); month = int(text[5:7])
    return f"{year}Q{(month - 1)//3 + 1}"


def statcan_gdp(rows):
    sample = rows[0]
    ref = find_col(sample, "ref_date") or find_col(sample, "ref", "date")
    geo = find_col(sample, "geo"); estimate = find_col(sample, "estimate")
    prices = find_col(sample, "price"); seasonal = find_col(sample, "seasonal")
    value = find_col(sample, "value")
    if not all([ref, geo, estimate, prices, seasonal, value]):
        raise RuntimeError("Unexpected Statistics Canada GDP schema")
    levels = {}
    for row in rows:
        if norm(row[geo]) != "canada": continue
        if "gross domestic product at market prices" not in norm(row[estimate]): continue
        if "chained" not in norm(row[prices]): continue
        if "seasonally adjusted at annual rates" not in norm(row[seasonal]): continue
        try: levels[quarter_from_date(row[ref])] = float(row[value])
        except (TypeError, ValueError): pass
    growth = {}
    for q, level in levels.items():
        prev = levels.get(shift_quarter(q, -1))
        if prev and prev > 0 and level > 0:
            growth[q] = 400.0 * math.log(level / prev)
    return growth


def statcan_unemployment(rows):
    sample = rows[0]
    ref = find_col(sample, "ref_date") or find_col(sample, "ref", "date")
    geo = find_col(sample, "geo"); characteristic = find_col(sample, "labour", "force", "characteristic")
    age = find_col(sample, "age"); gender = find_col(sample, "gender") or find_col(sample, "sex")
    dtype = find_col(sample, "data", "type"); value = find_col(sample, "value")
    if not all([ref, geo, characteristic, age, gender, dtype, value]):
        raise RuntimeError("Unexpected Statistics Canada labour schema")
    months = defaultdict(list)
    for row in rows:
        if norm(row[geo]) != "canada" or norm(row[characteristic]) != "unemployment rate": continue
        if "15 years and over" not in norm(row[age]): continue
        if not ("total" in norm(row[gender]) or "both" in norm(row[gender])): continue
        if "seasonally adjusted" not in norm(row[dtype]): continue
        try: months[quarter_from_date(row[ref])].append(float(row[value]))
        except (TypeError, ValueError): pass
    return {q: statistics.fmean(v) for q, v in months.items() if v}


def solve(a, b):
    n = len(a); aug = [list(a[i]) + [b[i]] for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(aug[r][c]))
        if abs(aug[p][c]) < 1e-12: raise RuntimeError("Singular normal equation")
        aug[c], aug[p] = aug[p], aug[c]
        s = aug[c][c]; aug[c] = [v/s for v in aug[c]]
        for r in range(n):
            if r == c: continue
            f = aug[r][c]; aug[r] = [aug[r][j] - f*aug[c][j] for j in range(n+1)]
    return [aug[i][-1] for i in range(n)]


def inverse(a):
    n = len(a); aug = [list(a[i]) + [1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(aug[r][c]))
        if abs(aug[p][c]) < 1e-12: raise RuntimeError("Singular covariance matrix")
        aug[c], aug[p] = aug[p], aug[c]
        s = aug[c][c]; aug[c] = [v/s for v in aug[c]]
        for r in range(n):
            if r == c: continue
            f = aug[r][c]; aug[r] = [aug[r][j] - f*aug[c][j] for j in range(2*n)]
    return [row[n:] for row in aug]


def ols(x, y):
    k = len(x[0])
    xtx = [[sum(row[i]*row[j] for row in x) for j in range(k)] for i in range(k)]
    xty = [sum(row[i]*yy for row, yy in zip(x, y)) for i in range(k)]
    beta = solve(xtx, xty)
    residuals = [yy-sum(b*v for b,v in zip(beta,row)) for row,yy in zip(x,y)]
    dof = max(1, len(y)-k); sigma2 = sum(r*r for r in residuals)/dof
    inv = inverse(xtx); se = [math.sqrt(max(0.0,sigma2*inv[i][i])) for i in range(k)]
    sd = math.sqrt(sum(r*r for r in residuals)/max(1,len(residuals)-1))
    return beta, se, residuals, sd


def policy_grid(rows):
    obs = []
    byq = {r["quarter"]:r for r in rows}
    for r in rows:
        prev = byq.get(shift_quarter(r["quarter"],-1))
        if prev: obs.append((float(prev["policy_rate"]),float(r["policy_rate"]),float(r["core_inflation"]),float(r["output_gap"])))
    best = None; center = None
    for stage in range(3):
        if stage == 0:
            alphas=[.25+.10*i for i in range(23)]; betas=[.0+.05*i for i in range(21)]; steps=[.125+.025*i for i in range(36)]
        else:
            a,b,s=center; da=.025 if stage==1 else .005; db=.0125 if stage==1 else .0025; ds=.0125 if stage==1 else .0025
            alphas=[max(.01,a+da*i) for i in range(-4,5)]; betas=[max(0.,b+db*i) for i in range(-4,5)]; steps=[max(.025,s+ds*i) for i in range(-4,5)]
        for alpha in alphas:
            for beta in betas:
                for step in steps:
                    sse=0.0
                    for prev,actual,inf,gap in obs:
                        target=NEUTRAL_RATE+alpha*(inf-2.0)+beta*gap
                        pred=max(0.,min(8.,prev+max(-step,min(step,target-prev))))
                        sse+=(actual-pred)**2
                    if best is None or sse<best[0]: best=(sse,alpha,beta,step)
        center=best[1:]
    return best[1],best[2],best[3],math.sqrt(best[0]/len(obs)),len(obs)


def estimate(rows):
    rows=sorted([r for r in rows if SAMPLE_START<=r["quarter"]<=SAMPLE_END],key=lambda r:qkey(r["quarter"]))
    if len(rows)<60: raise RuntimeError(f"Only {len(rows)} complete SEP observations")
    byq={r["quarter"]:r for r in rows}
    xg=[]; yg=[]; qg=[]
    for r in rows:
        prev=byq.get(shift_quarter(r["quarter"],-1))
        if prev:
            xg.append([float(prev["output_gap"]),-(float(r["policy_rate"])-NEUTRAL_RATE)]); yg.append(float(r["output_gap"])); qg.append(r["quarter"])
    bg,seg,rg,sdg=ols(xg,yg)
    xp=[]; yp=[]; qp=[]
    for r in rows:
        prev=byq.get(shift_quarter(r["quarter"],-1))
        if prev:
            xp.append([float(prev["core_inflation"])-2.,float(r["output_gap"]),float(r["usdcad"])-1.34]); yp.append(float(r["core_inflation"])-2.); qp.append(r["quarter"])
    bp,sep,rp,sdp=ols(xp,yp)
    alpha,beta,step,rule_rmse,npolicy=policy_grid(rows)
    rga=dict(zip(qg,rg)); rpa=dict(zip(qp,rp)); common=sorted(set(rga)&set(rpa),key=qkey)
    ag=[rga[q] for q in common]; ap=[rpa[q] for q in common]; mg=statistics.fmean(ag); mp=statistics.fmean(ap); den=max(1,len(common)-1)
    cov=sum((a-mg)*(b-mp) for a,b in zip(ag,ap))/den; sga=math.sqrt(sum((a-mg)**2 for a in ag)/den); spa=math.sqrt(sum((b-mp)**2 for b in ap)/den); corr=cov/(sga*spa) if sga and spa else 0.
    def rec(name,val,se,lo,hi,n,method,eligible,notes): return {"parameter":name,"estimate":val,"standard_error":se,"lower_bound":lo,"upper_bound":hi,"observations":n,"sample_start":SAMPLE_START,"sample_end":SAMPLE_END,"method":method,"direct_eligible":"true" if eligible else "false","notes":notes}
    estimates=[
        rec("output_persistence",bg[0],seg[0],max(.05,bg[0]-1.96*seg[0]),min(.98,bg[0]+1.96*seg[0]),len(yg),"OLS production-form output-gap equation",.05<bg[0]<.98,"Real-time staff output-gap vintages."),
        rec("real_rate_demand_sensitivity",bg[1],seg[1],max(.001,bg[1]-1.96*seg[1]),max(.002,bg[1]+1.96*seg[1]),len(yg),"OLS production-form output-gap equation",bg[1]>0,"Coefficient on -(staff policy rate-neutral rate)."),
        rec("output_shock_sd",sdg,0.,max(.001,.7*sdg),1.3*sdg,len(yg),"sample SD of output-equation residual",sdg>0,"Residual innovation scale."),
        rec("inflation_persistence",bp[0],sep[0],max(.05,bp[0]-1.96*sep[0]),min(.98,bp[0]+1.96*sep[0]),len(yp),"OLS production-form centered inflation equation",.05<bp[0]<.98,"Core CPI is year-over-year PCPIX computed within each real-time vintage."),
        rec("phillips_curve_slope",bp[1],sep[1],bp[1]-1.96*sep[1],bp[1]+1.96*sep[1],len(yp),"OLS production-form centered inflation equation",bp[1]>0,"Contemporaneous staff output-gap coefficient."),
        rec("fx_pass_through",bp[2],sep[2],bp[2]-1.96*sep[2],bp[2]+1.96*sep[2],len(yp),"OLS production-form centered inflation equation",bp[2]>0,"Coefficient on U.S./CAD minus 1.34; omitted oil/import/supply terms remain in residual."),
        rec("inflation_shock_sd",sdp,0.,max(.001,.7*sdp),1.3*sdp,len(yp),"sample SD of inflation-equation residual",sdp>0,"Residual innovation scale."),
        rec("rate_inflation_response",alpha,0.,max(.01,.65*alpha),1.35*alpha,npolicy,"grid fit of exact clipped production policy rule",alpha>0 and rule_rmse<1.0,f"Staff implied-rate rule; RMSE={rule_rmse:.4f} pp."),
        rec("rate_output_response",beta,0.,max(0.,.65*beta),max(.01,1.35*beta),npolicy,"grid fit of exact clipped production policy rule",beta>0 and rule_rmse<1.0,f"Staff implied-rate rule; RMSE={rule_rmse:.4f} pp."),
        rec("max_quarterly_rate_step",step,0.,max(.025,.75*step),1.25*step,npolicy,"grid fit of exact clipped production policy rule",step>0 and rule_rmse<1.0,f"Staff implied-rate rule; RMSE={rule_rmse:.4f} pp."),
    ]
    covariance=[
        {"row":"output_gap_residual","column":"output_gap_residual","covariance":sga*sga,"correlation":1.,"observations":len(common)},
        {"row":"output_gap_residual","column":"inflation_residual","covariance":cov,"correlation":corr,"observations":len(common)},
        {"row":"inflation_residual","column":"output_gap_residual","covariance":cov,"correlation":corr,"observations":len(common)},
        {"row":"inflation_residual","column":"inflation_residual","covariance":spa*spa,"correlation":1.,"observations":len(common)},
    ]
    return estimates,covariance


def write_csv(path,fields,rows):
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader()
        for row in rows: w.writerow({k:(f"{row.get(k):.10g}" if isinstance(row.get(k),float) else row.get(k,"")) for k in fields})


def refresh(root):
    panel,manifest=sep_panel()
    gdp_rows,gdp_raw=read_statcan_zip(STATCAN_GDP); lfs_rows,lfs_raw=read_statcan_zip(STATCAN_LFS)
    gdp=statcan_gdp(gdp_rows); unemployment=statcan_unemployment(lfs_rows)
    for row in panel:
        row["statcan_gdp_growth"]=gdp.get(row["quarter"],""); row["statcan_unemployment"]=unemployment.get(row["quarter"],"")
    manifest += [
        {"source_id":"statcan_gdp_quarterly","agency":"Statistics Canada","dataset":"36-10-0104-01","url":STATCAN_GDP,"sha256":hashlib.sha256(gdp_raw).hexdigest(),"observations":len(gdp),"transformation":"400*log(q/q-1), revised chained-dollar GDP diagnostic"},
        {"source_id":"statcan_unemployment_monthly","agency":"Statistics Canada","dataset":"14-10-0287-01","url":STATCAN_LFS,"sha256":hashlib.sha256(lfs_raw).hexdigest(),"observations":len(unemployment),"transformation":"quarterly mean, seasonally adjusted revised diagnostic"},
    ]
    estimates,covariance=estimate(panel); data=root/"data"/"calibration"
    write_csv(data/"quarterly_estimation_panel.csv",["quarter","output_gap","core_inflation","policy_rate","usdcad","statcan_gdp_growth","statcan_unemployment"],panel)
    write_csv(data/"quarterly_estimation_manifest.csv",["source_id","agency","dataset","url","sha256","observations","transformation"],manifest)
    write_csv(data/"quarterly_structural_estimates.csv",["parameter","estimate","standard_error","lower_bound","upper_bound","observations","sample_start","sample_end","method","direct_eligible","notes"],estimates)
    write_csv(data/"quarterly_residual_covariance.csv",["row","column","covariance","correlation","observations"],covariance)
    print(f"panel_rows={len(panel)}")
    for e in estimates: print(f"ESTIMATE {e['parameter']}={e['estimate']:.8g} se={e['standard_error']:.8g} eligible={e['direct_eligible']}")
    print(f"RESIDUAL_CORRELATION={covariance[1]['correlation']:.8g}")


def read_csv(path):
    with path.open(newline="",encoding="utf-8") as f: return list(csv.DictReader(f))


def verify(root):
    data=root/"data"/"calibration"; panel=read_csv(data/"quarterly_estimation_panel.csv"); expected,cov=estimate(panel); committed={r["parameter"]:r for r in read_csv(data/"quarterly_structural_estimates.csv")}
    for row in expected:
        old=committed.get(row["parameter"])
        if not old: raise RuntimeError(f"Missing committed estimate {row['parameter']}")
        if abs(float(old["estimate"])-row["estimate"])>5e-8: raise RuntimeError(f"Estimate drift {row['parameter']}")
        if old["direct_eligible"]!=row["direct_eligible"]: raise RuntimeError(f"Eligibility drift {row['parameter']}")
    oldcov=read_csv(data/"quarterly_residual_covariance.csv")
    if len(oldcov)!=4 or abs(float(oldcov[1]["correlation"])-cov[1]["correlation"])>5e-8: raise RuntimeError("Residual covariance drift")
    if len(panel)!=76: raise RuntimeError(f"Expected 76 quarterly vintages, got {len(panel)}")
    print(f"verified_quarterly_panel rows={len(panel)} direct_eligible={sum(e['direct_eligible']=='true' for e in expected)}")


def main():
    p=argparse.ArgumentParser(); g=p.add_mutually_exclusive_group(required=True); g.add_argument("--refresh",action="store_true"); g.add_argument("--verify",action="store_true"); p.add_argument("--root",default="."); a=p.parse_args(); root=Path(a.root).resolve(); refresh(root) if a.refresh else verify(root)


if __name__=="__main__":
    try: main()
    except Exception as exc:
        print(f"quarterly calibration error: {exc}",file=sys.stderr); raise
