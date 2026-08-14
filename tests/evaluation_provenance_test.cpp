#include "evaluation_provenance.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

int main() {
  using namespace cad;
  using namespace cad::server::evaluation_provenance;

  Economy first;
  first.us_tariff_canada = 37.0;
  first.canada_retaliatory_tariff = 11.0;
  first.us_sector_coverage[3] = 44.0;
  first.canada_sector_coverage[7] = 63.0;
  first.loss_weights.boc_inflation = 4.25;

  Economy same = first;
  const std::string first_hash = fingerprint(first);
  assert(first_hash.rfind("fnv1a64:", 0) == 0);
  assert(fingerprint(same) == first_hash);

  same.us_sector_coverage[3] = 45.0;
  assert(fingerprint(same) != first_hash);

  const std::string path = "evaluation-provenance-test.events";
  std::remove(path.c_str());
  assert(checkpoint_submission(path, first, first_hash, 9,
      "calibration-2026-08", "structural-v2"));

  std::ifstream in(path);
  assert(in.good());
  std::string line;
  assert(std::getline(in, line));
  assert(line.find("\"schemaVersion\":1") != std::string::npos);
  assert(line.find(first_hash) != std::string::npos);
  assert(line.find("\"negotiationRevision\":9") != std::string::npos);
  assert(line.find("calibration-2026-08") != std::string::npos);
  assert(line.find("structural-v2") != std::string::npos);
  assert(line.find("\"usTariff\":37") != std::string::npos);
  assert(line.find("\"retaliatoryTariff\":11") != std::string::npos);

  const std::string attached = attach_json(
      "{\"ok\":true}", first_hash, 9,
      "calibration-2026-08", "structural-v2");
  assert(attached.find("\"evaluationProvenance\":{") != std::string::npos);
  assert(attached.find(first_hash) != std::string::npos);
  assert(attached.find("\"negotiationRevision\":9") != std::string::npos);
  assert(attached.find("\"stale\":false") != std::string::npos);

  std::remove(path.c_str());
  return 0;
}
