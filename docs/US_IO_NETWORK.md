# U.S. Input-Output Network

The production fallback for the U.S. network is the EPA USEEIO v2.5 detailed model `USEEIOv2.5-catbird-22` (DOI `10.23719/1532178`). It is a provisional U.S.-specific bridge while the repository's current-vintage BEA Input-Output artifact is pending. It replaces the former fallback that reused the Canadian Statistics Canada matrix.

EPA describes `catbird-22` as a detailed 2022 USEEIO model whose underlying U.S. input-output data represent 2017. The repository therefore keeps `us_trade_input_output_empirical()` false while this proxy is selected.

## Aggregation

`tools/build_useeio_us_proxy.py` reads the official workbook's domestic direct-requirements matrix `A_d`, commodity-output vector `q`, and `commodities_meta` crosswalk. `A_d` is upstream commodity by downstream commodity. The builder maps 402 detailed commodities into 20 model sectors, excluding four accounting/adjustment commodities: `S00300`, `S00401`, `S00402`, and `S00900`. This leaves 398 mapped commodities.

For each downstream model sector, positive domestic direct requirements are summed by upstream model sector and weighted by detailed downstream commodity output `q`. Negative accounting/redefinition entries are not propagated. The frozen runtime orientation is downstream by upstream.

The committed artifacts are:

- `data/calibration/useeio_us_proxy_matrix.csv`
- `include/generated/trade_io_us_proxy.hpp`
- `data/calibration/useeio_us_proxy_provenance.json`

The provenance file records the source workbook SHA-256, model, basis year, exclusions, mapping counts, aggregation method, and replacement boundary.

## BEA replacement boundary

`tools/build_bea_io_matrix.py` remains the final U.S. calibration path. The EPA proxy remains active unless both `include/generated/trade_io_us_bea.hpp` and a separately reviewed `include/generated/trade_io_us_bea_certified.hpp` marker are present. A generated but unreviewed BEA header alone cannot silently change production.

The certification marker should only be committed with a reviewed BEA artifact whose source vintage, table selection, mapping, provenance, diagnostics, and trade-network tests have been checked.

## Reproduction

Run the builder against the official `USEEIOv2.5-catbird-22.xlsx` workbook. Add `--verify` to compare the regenerated CSV, C++ header, and provenance against the committed frozen artifacts. Ordinary CI does not fetch the external workbook; it verifies the builder contract and frozen-artifact invariants offline.
