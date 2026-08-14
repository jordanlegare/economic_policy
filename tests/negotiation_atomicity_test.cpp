#include "server_contracts.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main() {
  using namespace cad::server;

  // The stateless evaluation request and durable negotiation authority share
  // one U.S. tariff ceiling. Exactly 200% is admissible; 201% fails closed.
  {
    cad::Economy economy;
    std::string error;
    const auto maximum = cad::request_json::parse_object(
        R"({"usTariff":200})");
    assert(maximum.valid);
    assert(parse_economy(maximum, economy, error));
    assert(economy.us_tariff_canada == 200.0);

    const auto too_high = cad::request_json::parse_object(
        R"({"usTariff":201})");
    assert(too_high.valid);
    error.clear();
    assert(!parse_economy(too_high, economy, error));
    assert(!error.empty());
  }

  // The durable live room uses the same 200% endpoint and remains transactional
  // above it, while Canada's retaliatory ceiling stays at 60%.
  {
    NegotiationState bounds;
    std::string error;
    const auto maximum = cad::request_json::parse_object(
        R"({"actor":"us","operationId":"us-max-200","usTariff":200,"usPriority":50})");
    assert(maximum.valid);
    assert(bounds.update(maximum, error));
    assert(bounds.revision() == 1);
    assert(bounds.json().find("\"usTariff\":200") != std::string::npos);

    const std::string at_maximum = bounds.json();
    const auto too_high = cad::request_json::parse_object(
        R"({"actor":"us","operationId":"us-over-200","usTariff":201,"usPriority":50})");
    assert(too_high.valid);
    error.clear();
    assert(!bounds.update(too_high, error));
    assert(!error.empty());
    assert(bounds.revision() == 1);
    assert(bounds.json() == at_maximum);

    const auto automatic_maximum = cad::request_json::parse_object(
        R"({"actor":"automatic","operationId":"automatic-us-max-200","usTariff":200,"retaliatoryTariff":60})");
    assert(automatic_maximum.valid);
    error.clear();
    assert(bounds.update(automatic_maximum, error));
    assert(bounds.revision() == 2);
    assert(bounds.json().find("\"usTariff\":200") != std::string::npos);
    assert(bounds.json().find("\"retaliatoryTariff\":60") != std::string::npos);

    const std::string after_automatic = bounds.json();
    const auto retaliation_too_high = cad::request_json::parse_object(
        R"({"actor":"automatic","operationId":"automatic-retaliation-over-60","usTariff":200,"retaliatoryTariff":61})");
    assert(retaliation_too_high.valid);
    error.clear();
    assert(!bounds.update(retaliation_too_high, error));
    assert(bounds.revision() == 2);
    assert(bounds.json() == after_automatic);
  }

  // A delegation-settings exchange is one bilateral transaction. Every scalar
  // and all 40 sector positions are required before the journal can advance.
  {
    auto exchange_json = [](const std::string& operation_id,
                            double canada_priority, double us_priority,
                            double canada_sector_19 = 61.0,
                            bool include_us_sector_19 = true) {
      std::ostringstream out;
      out << "{\"actor\":\"exchange\",\"operationId\":\"" << operation_id
          << "\",\"usTariff\":180,\"retaliatoryTariff\":55"
          << ",\"canadaPriority\":" << canada_priority
          << ",\"usPriority\":" << us_priority
          << ",\"riskAversion\":63,\"cooperationCeiling\":47";
      for (int i = 0; i < 20; ++i) {
        const double value = i == 19 ? canada_sector_19 : 80.0 - i;
        out << ",\"canadaSector" << i << "\":" << value;
      }
      for (int i = 0; i < 20; ++i) {
        if (i == 19 && !include_us_sector_19) continue;
        out << ",\"usSector" << i << "\":" << 20.0 + i;
      }
      out << '}';
      return out.str();
    };

    NegotiationState exchange;
    std::string error;
    const auto accepted = cad::request_json::parse_object(
        exchange_json("exchange-good", 64.0, 36.0));
    assert(accepted.valid);
    assert(exchange.update(accepted, error));
    assert(exchange.revision() == 1);
    const std::string imported = exchange.json();
    assert(imported.find("\"updatedBy\":\"imported delegation settings\"")
           != std::string::npos);
    assert(imported.find("\"usTariff\":180") != std::string::npos);
    assert(imported.find("\"retaliatoryTariff\":55") != std::string::npos);
    assert(imported.find("\"canadaPriority\":64") != std::string::npos);
    assert(imported.find("\"usPriority\":36") != std::string::npos);

    const auto bad_priority = cad::request_json::parse_object(
        exchange_json("exchange-priority-bad", 60.0, 30.0));
    assert(bad_priority.valid);
    error.clear();
    assert(!exchange.update(bad_priority, error));
    assert(error.find("sum to 100") != std::string::npos);
    assert(exchange.revision() == 1);
    assert(exchange.json() == imported);

    const auto incomplete = cad::request_json::parse_object(
        exchange_json("exchange-incomplete", 64.0, 36.0, 61.0, false));
    assert(incomplete.valid);
    error.clear();
    assert(!exchange.update(incomplete, error));
    assert(error.find("usSector19 is required") != std::string::npos);
    assert(exchange.revision() == 1);
    assert(exchange.json() == imported);

    const auto late_invalid_sector = cad::request_json::parse_object(
        exchange_json("exchange-sector-bad", 64.0, 36.0, 101.0));
    assert(late_invalid_sector.valid);
    error.clear();
    assert(!exchange.update(late_invalid_sector, error));
    assert(!error.empty());
    assert(exchange.revision() == 1);
    assert(exchange.json() == imported);
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
