#include "request_json.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using namespace cad::request_json;

  const auto valid = parse_object(
      R"({"usTariff":50,"comparisonOnly":true,"actor":"canada","note":"ok\nline"})");
  assert(valid.valid);
  assert(valid.number("usTariff").has_value());
  assert(*valid.number("usTariff") == 50.0);
  assert(valid.boolean("comparisonOnly").has_value());
  assert(*valid.boolean("comparisonOnly"));
  assert(valid.string("actor").has_value());
  assert(*valid.string("actor") == "canada");

  assert(!parse_object(R"({"usTariff":10,"usTariff":20})").valid);
  assert(!parse_object(R"({"usTariff":})").valid);
  assert(!parse_object(R"({"usTariff":1e9999})").valid);
  assert(!parse_object(R"({"usTariff":01})").valid);
  assert(!parse_object(R"({"x":[1,2]})").valid);
  assert(!parse_object(R"({"x":1} trailing)").valid);

  auto wrong_type = parse_object(R"({"riskAversion":"high"})");
  assert(wrong_type.valid);
  double target = 50.0;
  std::string error;
  assert(!number_in_range(wrong_type, "riskAversion", 0.0, 100.0, target, error));
  assert(target == 50.0);

  auto out_of_range = parse_object(R"({"riskAversion":101})");
  assert(out_of_range.valid);
  error.clear();
  assert(!number_in_range(out_of_range, "riskAversion", 0.0, 100.0, target, error));

  auto absent = parse_object(R"({})");
  assert(absent.valid);
  target = 42.0;
  error.clear();
  assert(number_in_range(absent, "riskAversion", 0.0, 100.0, target, error));
  assert(target == 42.0);

  std::cout << "request JSON tests passed\n";
  return 0;
}
