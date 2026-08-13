#include "server_contracts.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using namespace cad::server;

  NegotiationState state;
  std::string error;
  const auto accepted = cad::request_json::parse_object(
      R"({"actor":"canada","retaliatoryTariff":12,"canadaPriority":60,"riskAversion":45})");
  assert(accepted.valid);
  assert(state.update(accepted, error));
  assert(state.revision() == 1);

  const std::string before = state.json();
  const auto rejected = cad::request_json::parse_object(
      R"({"actor":"canada","riskAversion":91,"retaliatoryTariff":999,"canadaPriority":20})");
  assert(rejected.valid);
  error.clear();
  assert(!state.update(rejected, error));
  assert(!error.empty());
  assert(state.json() == before);
  assert(state.revision() == 1);

  const auto late_sector_rejection = cad::request_json::parse_object(
      R"({"actor":"automatic","usTariff":15,"retaliatoryTariff":8,"riskAversion":20,"canadaSector0":35,"canadaSector19":101})");
  assert(late_sector_rejection.valid);
  error.clear();
  assert(!state.update(late_sector_rejection, error));
  assert(state.json() == before);
  assert(state.revision() == 1);

  std::cout << "negotiation updates are transactional\n";
  return 0;
}
