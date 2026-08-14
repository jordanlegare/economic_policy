#include "server_contracts.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

int main() {
  using namespace cad::server;

  const std::string path = "negotiation-state-test.events";
  std::remove(path.c_str());
  const std::string accepted_json =
      R"({"actor":"canada","operationId":"neg-op-1","retaliatoryTariff":12,"canadaPriority":60,"riskAversion":45})";

  std::string durable_state;
  {
    NegotiationState state(path);
    std::string error;
    const auto accepted = cad::request_json::parse_object(accepted_json);
    assert(accepted.valid);
    assert(state.update(accepted, error));
    assert(state.revision() == 1);
    assert(!state.last_update_replayed());

    const std::string after_accept = state.json();
    error.clear();
    assert(state.update(accepted, error));
    assert(state.last_update_replayed());
    assert(state.revision() == 1);
    assert(state.json() == after_accept);

    const auto conflicting_replay = cad::request_json::parse_object(
        R"({"actor":"canada","operationId":"neg-op-1","retaliatoryTariff":13,"canadaPriority":61,"riskAversion":45})");
    assert(conflicting_replay.valid);
    error.clear();
    assert(!state.update(conflicting_replay, error));
    assert(!error.empty());
    assert(state.revision() == 1);
    assert(state.json() == after_accept);

    const std::string before = state.json();
    const auto rejected = cad::request_json::parse_object(
        R"({"actor":"canada","operationId":"neg-op-bad","riskAversion":91,"retaliatoryTariff":999,"canadaPriority":20})");
    assert(rejected.valid);
    error.clear();
    assert(!state.update(rejected, error));
    assert(!error.empty());
    assert(state.json() == before);
    assert(state.revision() == 1);

    const auto late_sector_rejection = cad::request_json::parse_object(
        R"({"actor":"automatic","operationId":"neg-op-sector","usTariff":15,"retaliatoryTariff":8,"riskAversion":20,"canadaSector0":35,"canadaSector19":101})");
    assert(late_sector_rejection.valid);
    error.clear();
    assert(!state.update(late_sector_rejection, error));
    assert(state.json() == before);
    assert(state.revision() == 1);
    durable_state = state.json();
  }

  {
    NegotiationState restored(path);
    assert(restored.revision() == 1);
    assert(restored.recovery_warning_count() == 0);
    assert(restored.json() == durable_state);

    std::string error;
    const auto accepted = cad::request_json::parse_object(accepted_json);
    assert(restored.update(accepted, error));
    assert(restored.last_update_replayed());
    assert(restored.revision() == 1);
    assert(restored.json() == durable_state);
  }

  {
    std::ofstream out(path, std::ios::app | std::ios::binary);
    assert(out.good());
    out << "{\"actor\":\"canada\",\"operationId\":";
  }
  {
    NegotiationState recovered(path);
    assert(recovered.revision() == 1);
    assert(recovered.recovery_warning_count() == 1);
    const auto json = recovered.json();
    assert(json.find("\"retaliatoryTariff\":12") != std::string::npos);
    assert(json.find("\"canadaPriority\":60") != std::string::npos);
    assert(json.find("\"riskAversion\":45") != std::string::npos);
  }

  std::remove(path.c_str());
  std::cout << "negotiation updates are transactional and replay safe\n";
  return 0;
}
