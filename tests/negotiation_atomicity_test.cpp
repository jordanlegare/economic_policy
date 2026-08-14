#include "server_contracts.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

int main() {
  using namespace cad::server;

  // The public evaluation contract already accepts a 100% U.S. tariff shock.
  // The durable negotiation authority must accept the same endpoint and fail
  // atomically above it, while Canada's retaliatory ceiling remains 60%.
  {
    NegotiationState bounds;
    std::string error;
    const auto maximum = cad::request_json::parse_object(
        R"({"actor":"us","operationId":"us-max-100","usTariff":100,"usPriority":50})");
    assert(maximum.valid);
    assert(bounds.update(maximum, error));
    assert(bounds.revision() == 1);
    assert(bounds.json().find("\"usTariff\":100") != std::string::npos);

    const std::string at_maximum = bounds.json();
    const auto too_high = cad::request_json::parse_object(
        R"({"actor":"us","operationId":"us-over-100","usTariff":101,"usPriority":50})");
    assert(too_high.valid);
    error.clear();
    assert(!bounds.update(too_high, error));
    assert(!error.empty());
    assert(bounds.revision() == 1);
    assert(bounds.json() == at_maximum);

    const auto automatic_maximum = cad::request_json::parse_object(
        R"({"actor":"automatic","operationId":"automatic-us-max-100","usTariff":100,"retaliatoryTariff":60})");
    assert(automatic_maximum.valid);
    error.clear();
    assert(bounds.update(automatic_maximum, error));
    assert(bounds.revision() == 2);
    assert(bounds.json().find("\"usTariff\":100") != std::string::npos);
    assert(bounds.json().find("\"retaliatoryTariff\":60") != std::string::npos);

    const std::string after_automatic = bounds.json();
    const auto retaliation_too_high = cad::request_json::parse_object(
        R"({"actor":"automatic","operationId":"automatic-retaliation-over-60","usTariff":100,"retaliatoryTariff":61})");
    assert(retaliation_too_high.valid);
    error.clear();
    assert(!bounds.update(retaliation_too_high, error));
    assert(bounds.revision() == 2);
    assert(bounds.json() == after_automatic);
  }

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
