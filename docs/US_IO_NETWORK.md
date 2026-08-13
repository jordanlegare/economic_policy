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

The provenance file records the source workbook SHA-256, model, basis year, exclusions, mapping counts, aggregation method, and replacement boundary. The active EPA source is also registered in `data/calibration/source_registry.csv` as `epa_useeio_us_proxy`.

## BEA replacement boundary

`tools/build_bea_io_matrix.py` remains the final U.S. calibration path. The EPA proxy remains active unless both `include/generated/trade_io_us_bea.hpp` and a separately reviewed `include/generated/trade_io_us_bea_certified.hpp` marker are present. A generated but unreviewed BEA header alone cannot silently change production.

Certification is content-bound rather than presence-only. A BEA refresh generates four related artifacts:

- `data/calibration/bea_us_io_matrix.csv`
- `include/generated/trade_io_us_bea.hpp`
- `include/generated/trade_io_us_bea_contract.hpp`
- `data/calibration/bea_us_io_provenance.json`

The generated contract records the exact BEA header SHA-256 and a deterministic artifact fingerprint. The generated BEA header records the selected year, domestic-direct-requirements table ID, Use table ID, and CSV SHA-256. Its compile-time assertions require a certification marker to match all of those values plus the exact generated-header SHA-256 and artifact fingerprint.

`tools/verify_bea_io_certification.py` independently recomputes the committed CSV/header hashes and fingerprint offline. It succeeds in proxy mode when no BEA artifact is committed, accepts a complete but uncertified BEA artifact without activating it, and fails on partial artifacts, hash drift, table/vintage drift, or a mismatched certification marker.

The certification marker must be written only after independent review. Its contract is:

```cpp
#pragma once

#include <string_view>

namespace cad::generated {
inline constexpr int kCertifiedBeaUsIoYear = /* reviewed year */;
inline constexpr std::string_view kCertifiedBeaUsIoDirectRequirementsTableId = "/* reviewed table ID */";
inline constexpr std::string_view kCertifiedBeaUsIoUseTableId = "/* reviewed table ID */";
inline constexpr std::string_view kCertifiedBeaUsIoCsvSha256 = "/* reviewed CSV SHA-256 */";
inline constexpr std::string_view kCertifiedBeaUsIoHeaderSha256 = "/* reviewed generated-header SHA-256 */";
inline constexpr std::string_view kCertifiedBeaUsIoArtifactFingerprint = "/* reviewed fingerprint */";
}  // namespace cad::generated
```

The exact values are emitted in `bea_us_io_provenance.json` under `certification_contract`. Copying those values is not itself certification: the reviewer must first verify source vintage, table selection, mapping, row diagnostics, generated files, and trade-network tests.

## Reproduction

Run the USEEIO builder against the official `USEEIOv2.5-catbird-22.xlsx` workbook. Add `--verify` to compare the regenerated CSV, C++ header, and provenance against the committed frozen artifacts. Ordinary CI does not fetch the external workbook; it verifies the builder contract and frozen-artifact invariants offline.

For a BEA refresh, run `tools/build_bea_io_matrix.py` with `BEA_API_KEY`, review the generated matrix/provenance/contract, then run `python3 tools/verify_bea_io_certification.py`. Do not commit `trade_io_us_bea_certified.hpp` until the exact contract has been independently reviewed.
