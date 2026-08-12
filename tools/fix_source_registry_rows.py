#!/usr/bin/env python3
from pathlib import Path
p=Path('data/calibration/source_registry.csv')
s=p.read_text(encoding='utf-8')
old1='SOURCE,statcan_2018_tariff_export_study,Statistics Canada,"Canadian exports and imports of steel and aluminum products subject to tariffs: 2018 to 2019",2025-11-26,https://www.statcan.gc.ca/o1/en/plus/8721-canadian-exports-and-imports-steel-and-aluminum-products-subject-tariffs-2018-2019,,historical-treated-product-trade-validation'
new1='statcan_2018_tariff_export_study,Statistics Canada,"Canadian exports and imports of steel and aluminum products subject to tariffs: 2018 to 2019",historical_validation,one_time,public_html,https://www.statcan.gc.ca/o1/en/plus/8721-canadian-exports-and-imports-steel-and-aluminum-products-subject-tariffs-2018-2019,"Treated-product historical validation: tariffed Canadian steel/aluminum export values fell sharply during the 2018-2019 episode and the study reports U.S. importer price incidence. Used for sign/stress validation, not whole-manufacturing calibration."'
old2='SOURCE,finance_canada_2018_countermeasures,Department of Finance Canada,"Canada announces retaliatory tariffs in response to U.S. tariffs on Canadian steel and aluminum",2018-06-29,https://www.canada.ca/en/department-finance/news/2018/06/canada-announces-retaliatory-tariffs-in-response-to-unjustified-us-tariffs-on-canadian-steel-and-aluminum-products.html,,historical-legal-tariff-measure'
new2='finance_canada_2018_countermeasures,Department of Finance Canada,"Canada announces retaliatory tariffs in response to U.S. tariffs on Canadian steel and aluminum",official_legal,one_time,public_html,https://www.canada.ca/en/department-finance/news/2018/06/canada-announces-retaliatory-tariffs-in-response-to-unjustified-us-tariffs-on-canadian-steel-and-aluminum-products.html,"Historical legal measure: records the 2018 Canadian countermeasure scope/rates and C$16.6B trade value for the Section 232 episode."'
for old,new in ((old1,new1),(old2,new2)):
    if new in s: continue
    if s.count(old)!=1: raise SystemExit(f'expected exactly one malformed row: {old[:40]}')
    s=s.replace(old,new)
p.write_text(s,encoding='utf-8')
print('source registry rows normalized')
