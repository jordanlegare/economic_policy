#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new):
    p=Path(path); s=p.read_text(encoding='utf-8')
    if new in s:
        print('already patched', path); return
    if s.count(old)!=1:
        raise SystemExit(f'{path}: expected one marker, got {s.count(old)}')
    p.write_text(s.replace(old,new),encoding='utf-8')
    print('patched',path)

replace_once(
    'src/main.cpp',
    '    else if (first.rfind("GET /app.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/app.js") + "\\n" + read_file("web/evaluation-controller.js"));\n',
    '    else if (first.rfind("GET /app.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/app.js") + "\\n" + read_file("web/evaluation-controller.js"));\n    else if (first.rfind("GET /trade-incidence.js ", 0) == 0) respond(client, 200, "application/javascript", read_file("web/trade-incidence.js"));\n'
)

replace_once(
    '.github/workflows/ci.yml',
    '          node --check web/evaluation-controller.js\n          node --check web/diplomat.js\n',
    '          node --check web/evaluation-controller.js\n          node --check web/trade-incidence.js\n          node --check web/diplomat.js\n'
)

replace_once(
    '.github/workflows/ci.yml',
    '          python3 -m py_compile tools/refresh_calibration.py tools/estimate_trade_response.py tools/verify_trade_gate_certification.py tools/build_statcan_io_matrix.py\n',
    '          python3 -m py_compile tools/refresh_calibration.py tools/estimate_trade_response.py tools/verify_trade_gate_certification.py tools/build_statcan_io_matrix.py tools/build_bea_io_matrix.py tools/verify_structural_promotions.py\n          python3 tools/verify_structural_promotions.py\n'
)

registry=Path('data/calibration/source_registry.csv')
s=registry.read_text(encoding='utf-8')
rows=[]
if 'statcan_2018_tariff_export_study' not in s:
    rows.append('SOURCE,statcan_2018_tariff_export_study,Statistics Canada,"Canadian exports and imports of steel and aluminum products subject to tariffs: 2018 to 2019",2025-11-26,https://www.statcan.gc.ca/o1/en/plus/8721-canadian-exports-and-imports-steel-and-aluminum-products-subject-tariffs-2018-2019,,historical-treated-product-trade-validation')
if 'finance_canada_2018_countermeasures' not in s:
    rows.append('SOURCE,finance_canada_2018_countermeasures,Department of Finance Canada,"Canada announces retaliatory tariffs in response to U.S. tariffs on Canadian steel and aluminum",2018-06-29,https://www.canada.ca/en/department-finance/news/2018/06/canada-announces-retaliatory-tariffs-in-response-to-unjustified-us-tariffs-on-canadian-steel-and-aluminum-products.html,,historical-legal-tariff-measure')
if 'bea_input_output' not in s:
    rows.append('SOURCE,bea_input_output,U.S. Bureau of Economic Analysis,"Input-Output Accounts: annual supply-use and requirements tables",2026-07-22,https://www.bea.gov/data/industries/input-output-accounts-data,,official-us-production-network-refresh-source')
if rows:
    registry.write_text(s.rstrip()+"\n"+"\n".join(rows)+"\n",encoding='utf-8')
    print('appended calibration source provenance')
else:
    print('source provenance already present')

readme=Path('data/calibration/README.md')
s=readme.read_text(encoding='utf-8')
marker='## Trust rule\n'
insert='''## U.S. input-output refresh and structural promotion gate\n\n`tools/build_bea_io_matrix.py` is the fail-closed refresh path for the U.S. production-network artifact. It requires `BEA_API_KEY`, discovers an adequate BEA InputOutput direct-requirements/use-table pair, aggregates it to the exact 20 model sectors, and writes a CSV, generated C++ header and provenance record. Until those artifacts are reviewed and committed, the U.S. network remains explicitly non-empirical.\n\n`python3 tools/verify_structural_promotions.py` enforces the evidence boundary for macro/transmission parameters. Any production registry entry labelled `empirical_estimate`, `official_assessment` or `realized_residual_estimate` must have a matching `direct` evidence record from the same source. Reference-only estimates may inform sensitivity/calibrated anchors but cannot be silently promoted.\n\n'''
if insert not in s:
    if marker not in s: raise SystemExit('calibration README trust marker missing')
    readme.write_text(s.replace(marker,insert+marker),encoding='utf-8')
    print('patched calibration README')

doc=Path('docs/MODEL_ROBUSTNESS_V2.md')
s=doc.read_text(encoding='utf-8')
old='''2. Draw structural macro/transmission parameter sets from the documented structural registry.\n3. Inside every structural draw, rerun the complete 288-candidate generated policy-control search used by the production engine.\n4. Combine the newly selected custom controls with the 13 fixed expert strategies.\n5. Re-optimize each strategy's 20-sector negotiation package across the production Pareto-screened finalist schedules.\n6. Use 700 common-random-number paths for policy/finalist selection and 2,800 paths for verification.\n7. Re-rank the fully re-optimized decisions and report how often the reference control decision remains the winner.\n'''
new='''2. Draw structural macro/transmission parameter sets from the documented structural registry.\n3. For every structural draw, construct a production `PolicyEngine` with those parameters and rerun `evaluate()`; robustness no longer maintains a duplicate macro/sector simulator.\n4. The production rerun carries the complete 288-candidate generated policy-control search, 13 expert strategies, country-specific trade-network objects, sector Pareto search, bilateral growth constraints and stochastic verification.\n5. Use the same seeded production Monte Carlo/common-random-number semantics under every calibration and compare exact controls, strategy family and sector package with the reference production result.\n6. The linked-issue negotiation stage retains the complete 0.5-point epsilon-Pareto set and evaluates it with a bounded-memory two-pass common-random-number robust algorithm rather than truncating at 512 packages.\n7. Re-rank the fully re-optimized decisions and report how often the reference control decision remains the winner.\n'''
if old in s:
    s=s.replace(old,new)
oldnext='''21. Replace provisional structural envelopes with empirical estimates where defensible.\n22. Add typed sensitivity for internal component-loss weights, explicitly separating mandate-fixed from assumed welfare coefficients.\n23. Add later-tightening, hold/soft-landing and eventually tariff-specific historical episodes.\n24. Improve archival real-time vintage quality for reconstructed market-state inputs where open historical feeds become available.\n'''
newnext='''21. Replace provisional structural envelopes with empirical estimates where defensible; the structural-promotion CI gate now prevents reference-only evidence from being promoted accidentally.\n22. Replace the explicitly provisional U.S. production-network proxy with the versioned BEA 20-sector artifact generated by `tools/build_bea_io_matrix.py` once authenticated source extraction is available.\n23. Add typed sensitivity for internal component-loss weights, explicitly separating mandate-fixed from assumed welfare coefficients.\n24. Expand historical validation beyond the three macro-policy fixtures. The 2018 Section 232 steel/aluminum fixture now provides a separate treated-product trade-channel stress/falsification benchmark without pretending to be a complete 18-state macro vintage.\n25. Improve archival real-time vintage quality for reconstructed market-state inputs where open historical feeds become available.\n'''
if oldnext in s:
    s=s.replace(oldnext,newnext)
doc.write_text(s,encoding='utf-8')
print('patched robustness documentation')
