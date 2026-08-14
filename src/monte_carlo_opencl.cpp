#include "monte_carlo_opencl.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cad::monte_carlo::opencl {
namespace {

using cl_char = std::int8_t;
using cl_uchar = std::uint8_t;
using cl_short = std::int16_t;
using cl_ushort = std::uint16_t;
using cl_int = std::int32_t;
using cl_uint = std::uint32_t;
using cl_long = std::int64_t;
using cl_ulong = std::uint64_t;
using cl_half = std::uint16_t;
using cl_bool = cl_uint;
using cl_bitfield = cl_ulong;
using cl_device_type = cl_bitfield;
using cl_platform_info = cl_uint;
using cl_device_info = cl_uint;
using cl_context_properties = std::intptr_t;
using cl_command_queue_properties = cl_bitfield;
using cl_mem_flags = cl_bitfield;
using cl_program_build_info = cl_uint;
using cl_platform_id = struct _cl_platform_id*;
using cl_device_id = struct _cl_device_id*;
using cl_context = struct _cl_context*;
using cl_command_queue = struct _cl_command_queue*;
using cl_mem = struct _cl_mem*;
using cl_program = struct _cl_program*;
using cl_kernel = struct _cl_kernel*;
using cl_event = struct _cl_event*;

constexpr cl_int CL_SUCCESS = 0;
constexpr cl_bool CL_TRUE = 1;
constexpr cl_device_type CL_DEVICE_TYPE_CPU = 1ull << 1;
constexpr cl_device_type CL_DEVICE_TYPE_GPU = 1ull << 2;
constexpr cl_mem_flags CL_MEM_READ_ONLY = 1ull << 2;
constexpr cl_mem_flags CL_MEM_WRITE_ONLY = 1ull << 1;
constexpr cl_device_info CL_DEVICE_NAME = 0x102B;
constexpr cl_device_info CL_DEVICE_EXTENSIONS = 0x1030;
constexpr cl_device_info CL_DEVICE_DOUBLE_FP_CONFIG = 0x1032;
constexpr cl_program_build_info CL_PROGRAM_BUILD_LOG = 0x1183;

constexpr std::size_t kScalarCount = 40;
constexpr std::size_t kPathSeriesCount = 14;
constexpr std::size_t kOutputStride = 8 * kQuarterCount + 11;

class DynamicLibrary {
 public:
  DynamicLibrary() = default;
  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  ~DynamicLibrary() { close(); }

  bool open(const char* name) {
    close();
#ifdef _WIN32
    handle_ = LoadLibraryA(name);
#else
    handle_ = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
#endif
    return handle_ != nullptr;
  }

  void* symbol(const char* name) const {
    if (!handle_) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(handle_, name));
#else
    return dlsym(handle_, name);
#endif
  }

 private:
  void close() {
    if (!handle_) return;
#ifdef _WIN32
    FreeLibrary(handle_);
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }
#ifdef _WIN32
  HMODULE handle_ = nullptr;
#else
  void* handle_ = nullptr;
#endif
};

template<class Function>
Function function(DynamicLibrary& library, const char* name) {
  return reinterpret_cast<Function>(library.symbol(name));
}

using GetPlatformIDs = cl_int (*)(cl_uint, cl_platform_id*, cl_uint*);
using GetDeviceIDs = cl_int (*)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
using GetDeviceInfo = cl_int (*)(cl_device_id, cl_device_info, std::size_t, void*, std::size_t*);
using ContextNotify = void (*)(const char*, const void*, std::size_t, void*);
using CreateContext = cl_context (*)(const cl_context_properties*, cl_uint, const cl_device_id*, ContextNotify, void*, cl_int*);
using ReleaseContext = cl_int (*)(cl_context);
using CreateCommandQueue = cl_command_queue (*)(cl_context, cl_device_id, cl_command_queue_properties, cl_int*);
using ReleaseCommandQueue = cl_int (*)(cl_command_queue);
using CreateProgramWithSource = cl_program (*)(cl_context, cl_uint, const char**, const std::size_t*, cl_int*);
using BuildProgram = cl_int (*)(cl_program, cl_uint, const cl_device_id*, const char*, void (*)(cl_program, void*), void*);
using GetProgramBuildInfo = cl_int (*)(cl_program, cl_device_id, cl_program_build_info, std::size_t, void*, std::size_t*);
using ReleaseProgram = cl_int (*)(cl_program);
using CreateKernel = cl_kernel (*)(cl_program, const char*, cl_int*);
using ReleaseKernel = cl_int (*)(cl_kernel);
using CreateBuffer = cl_mem (*)(cl_context, cl_mem_flags, std::size_t, void*, cl_int*);
using ReleaseMemObject = cl_int (*)(cl_mem);
using SetKernelArg = cl_int (*)(cl_kernel, cl_uint, std::size_t, const void*);
using EnqueueWriteBuffer = cl_int (*)(cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t, const void*, cl_uint, const cl_event*, cl_event*);
using EnqueueNDRangeKernel = cl_int (*)(cl_command_queue, cl_kernel, cl_uint, const std::size_t*, const std::size_t*, const std::size_t*, cl_uint, const cl_event*, cl_event*);
using EnqueueReadBuffer = cl_int (*)(cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t, void*, cl_uint, const cl_event*, cl_event*);
using Finish = cl_int (*)(cl_command_queue);

struct Functions {
  GetPlatformIDs get_platform_ids = nullptr;
  GetDeviceIDs get_device_ids = nullptr;
  GetDeviceInfo get_device_info = nullptr;
  CreateContext create_context = nullptr;
  ReleaseContext release_context = nullptr;
  CreateCommandQueue create_command_queue = nullptr;
  ReleaseCommandQueue release_command_queue = nullptr;
  CreateProgramWithSource create_program_with_source = nullptr;
  BuildProgram build_program = nullptr;
  GetProgramBuildInfo get_program_build_info = nullptr;
  ReleaseProgram release_program = nullptr;
  CreateKernel create_kernel = nullptr;
  ReleaseKernel release_kernel = nullptr;
  CreateBuffer create_buffer = nullptr;
  ReleaseMemObject release_mem_object = nullptr;
  SetKernelArg set_kernel_arg = nullptr;
  EnqueueWriteBuffer enqueue_write_buffer = nullptr;
  EnqueueNDRangeKernel enqueue_nd_range_kernel = nullptr;
  EnqueueReadBuffer enqueue_read_buffer = nullptr;
  Finish finish = nullptr;

  bool complete() const {
    return get_platform_ids && get_device_ids && get_device_info
        && create_context && release_context
        && create_command_queue && release_command_queue
        && create_program_with_source && build_program && get_program_build_info
        && release_program && create_kernel && release_kernel
        && create_buffer && release_mem_object && set_kernel_arg
        && enqueue_write_buffer && enqueue_nd_range_kernel
        && enqueue_read_buffer && finish;
  }
};

const char* kernel_source() {
  return R"CLC(
#if defined(cl_khr_fp64)
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#elif defined(cl_amd_fp64)
#pragma OPENCL EXTENSION cl_amd_fp64 : enable
#endif
#pragma OPENCL FP_CONTRACT OFF

#define QCOUNT 12
#define INNOV 8
#define OUT_STRIDE 107

inline double cad_clamp(double x, double lo, double hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
inline double cad_max(double a, double b) { return a > b ? a : b; }
inline double cad_min(double a, double b) { return a < b ? a : b; }

__kernel void cad_monte_carlo(
    __global const double* s,
    __global const double* path,
    __global const double* innovation,
    __global double* output,
    const int draws) {
  const int d = (int)get_global_id(0);
  if (d >= draws) return;

  const double move_bp = s[0];
  const double productive_share = s[1];
  double rate = s[2];
  double inf = s[3];
  double gap = s[4];
  double unemployment = s[5];
  double debt = s[6];
  double housing = s[7];
  const double economy_us_growth = s[8];
  const double economy_gdp_growth = s[9];
  const double population_growth = s[10];
  const double credit_spread = s[11];
  const double fiscal_balance_gdp = s[12];
  const double wage_growth = s[13];
  double cost = s[14];
  const double global_growth = s[15];
  const double inflation_expectations = s[16];
  const double oil_price = s[17];
  const double border_friction = s[18];
  const double fx_pressure = s[19];
  const double neutral_rate = s[20];
  const double inflation_target = s[21];
  const double rate_inflation_response = s[22];
  const double rate_output_response = s[23];
  const double max_quarterly_rate_step = s[24];
  const double output_persistence = s[25];
  const double fiscal_demand_multiplier = s[26];
  const double real_rate_demand_sensitivity = s[27];
  const double global_growth_sensitivity = s[28];
  const double inflation_persistence = s[29];
  const double inflation_expectations_weight = s[30];
  const double phillips_curve_slope = s[31];
  const double import_price_pass_through = s[32];
  const double oil_inflation_sensitivity = s[33];
  const double output_shock_sd = s[34];
  const double inflation_shock_sd = s[35];
  const double growth_shock_sd = s[36];
  const double us_growth_shock_sd = s[37];
  const double export_shock_sd = s[38];
  const double us_export_shock_sd = s[39];

  double export_change = 0.0;
  double us_export_change = 0.0;
  int recession = 0;
  const int out_base = d * OUT_STRIDE;
  const int innov_base = d * QCOUNT * INNOV;

  for (int q = 0; q < QCOUNT; ++q) {
    const double fiscal = path[0 * QCOUNT + q];
    const double productive_investment = path[1 * QCOUNT + q];
    const double targeted_relief = path[2 * QCOUNT + q];
    const double diversification = path[3 * QCOUNT + q];
    const double deescalation = path[4 * QCOUNT + q];
    const double us_tariff = path[5 * QCOUNT + q];
    const double canada_tariff = path[6 * QCOUNT + q];
    const double trade_drag = path[7 * QCOUNT + q];
    const double us_supply_chain_drag = path[8 * QCOUNT + q];
    const double import_price = path[9 * QCOUNT + q];
    const double supply = path[10 * QCOUNT + q];
    const double relief_cost = path[11 * QCOUNT + q];
    const double canada_export_quantity_ratio = path[12 * QCOUNT + q];
    const double us_export_quantity_ratio = path[13 * QCOUNT + q];

    const int zi = innov_base + q * INNOV;
    const double export_z = innovation[zi + 0];
    const double us_export_z = innovation[zi + 1];
    const double output_z = innovation[zi + 2];
    const double inflation_z = innovation[zi + 3];
    const double growth_z = innovation[zi + 4];
    const double us_growth_z = innovation[zi + 5];
    const double unemployment_z = innovation[zi + 6];
    const double housing_z = innovation[zi + 7];

    const double rate_target = cad_clamp(neutral_rate
        + rate_inflation_response * (inf - inflation_target)
        + rate_output_response * gap, .25, 7.0);
    if (q == 0) {
      rate = cad_clamp(rate + move_bp / 100.0, 0.0, 8.0);
    } else {
      double policy_step = cad_clamp(rate_target - rate,
          -max_quarterly_rate_step, max_quarterly_rate_step);
      if (q == 1 && fabs(move_bp) > 1e-9) {
        const double followup = cad_min(.25, max_quarterly_rate_step);
        const int continue_easing = move_bp < 0.0
            && gap < -.25 && inf <= inflation_target + .35;
        const int continue_tightening = move_bp > 0.0
            && (inf >= inflation_target + .50 || gap > .50);
        if (continue_easing) policy_step = cad_min(policy_step, -followup);
        if (continue_tightening) policy_step = cad_max(policy_step, followup);
      }
      rate = cad_clamp(rate + policy_step, 0.0, 8.0);
    }

    const double demand = fiscal * (1.0 - productive_share)
        * fiscal_demand_multiplier
        - (rate - neutral_rate) * real_rate_demand_sensitivity;

    export_change = 100.0 * (canada_export_quantity_ratio - 1.0)
        + .35 * (economy_us_growth - 2.0)
        + 2.0 * diversification + export_z * export_shock_sd;
    us_export_change = 100.0 * (us_export_quantity_ratio - 1.0)
        + .30 * (economy_gdp_growth - 1.5)
        + 1.5 * deescalation + us_export_z * us_export_shock_sd;

    gap = output_persistence * gap + demand - trade_drag
        + global_growth_sensitivity * (global_growth - 2.7)
        + output_z * output_shock_sd;
    inf = inflation_persistence * inf
        + inflation_expectations_weight * inflation_expectations
        + phillips_curve_slope * gap + fx_pressure - supply
        + import_price_pass_through * import_price
        - oil_inflation_sensitivity * (oil_price - 75.0)
        + inflation_z * inflation_shock_sd;
    const double growth = cad_clamp(1.75 + gap - .18 * credit_spread
        + productive_investment * .24 + growth_z * growth_shock_sd,
        -3.0, 5.5);
    const double us_growth = cad_clamp(economy_us_growth
        + .16 * productive_investment + .28 * deescalation
        - .010 * us_tariff - .014 * canada_tariff
        - .04 * border_friction - .40 * us_supply_chain_drag
        + us_growth_z * us_growth_shock_sd, -3.0, 5.5);
    unemployment = cad_clamp(unemployment - .10 * (growth - 1.7)
        + unemployment_z * .035, 3.5, 11.0);
    housing = cad_clamp(.78 * housing - 1.15 * (rate - neutral_rate)
        + .08 * (population_growth - 1.2) + housing_z * .5,
        -15.0, 30.0);
    debt += (-fiscal_balance_gdp + fiscal * .8 + relief_cost * .55
        + .045 * (rate - neutral_rate) * debt - .18 * growth) / 4.0;
    cost = .56 * inf + .22 * cad_max(0.0, housing / 10.0)
        + .14 * cad_max(0.0, wage_growth - growth) + .08 * import_price;
    recession = recession || growth < 0.0;

    output[out_base + 0 * QCOUNT + q] = rate;
    output[out_base + 1 * QCOUNT + q] = inf;
    output[out_base + 2 * QCOUNT + q] = growth;
    output[out_base + 3 * QCOUNT + q] = us_growth;
    output[out_base + 4 * QCOUNT + q] = debt;
    output[out_base + 5 * QCOUNT + q] = cost;
    output[out_base + 6 * QCOUNT + q] = export_change;
    output[out_base + 7 * QCOUNT + q] = us_export_change;

    if (q == QCOUNT - 1) {
      output[out_base + 96] = inf;
      output[out_base + 97] = growth;
      output[out_base + 98] = us_growth;
      output[out_base + 99] = unemployment;
      output[out_base + 100] = debt;
      output[out_base + 101] = housing;
      output[out_base + 102] = cost;
      output[out_base + 103] = growth - cost + targeted_relief * .15;
      output[out_base + 104] = export_change;
      output[out_base + 105] = us_export_change;
      output[out_base + 106] = recession ? 1.0 : 0.0;
    }
  }
}
)CLC";
}

class Runtime {
 public:
  ~Runtime() { release_all(); }

  Probe probe() {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_probe();
    return probe_;
  }

  bool run(const Input& input, const std::vector<Innovation>& innovations,
           BatchResult& output, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_probe();
    if (!probe_.device_present || !probe_.fp64_supported) {
      error = probe_.detail.empty() ? "no FP64 OpenCL device" : probe_.detail;
      return false;
    }
    if (!ensure_program(error)) return false;

    std::array<double, kScalarCount> scalars{};
    scalars[0] = input.move_bp;
    scalars[1] = input.productive_share;
    scalars[2] = input.policy_rate;
    scalars[3] = input.core_inflation;
    scalars[4] = input.output_gap;
    scalars[5] = input.unemployment;
    scalars[6] = input.federal_debt_gdp;
    scalars[7] = input.housing_gap;
    scalars[8] = input.us_growth;
    scalars[9] = input.gdp_growth;
    scalars[10] = input.population_growth;
    scalars[11] = input.credit_spread;
    scalars[12] = input.fiscal_balance_gdp;
    scalars[13] = input.wage_growth;
    scalars[14] = input.headline_inflation;
    scalars[15] = input.global_growth;
    scalars[16] = input.inflation_expectations;
    scalars[17] = input.oil_price;
    scalars[18] = input.border_friction;
    scalars[19] = input.fx_pressure;
    scalars[20] = input.parameters.neutral_rate;
    scalars[21] = input.parameters.inflation_target;
    scalars[22] = input.parameters.rate_inflation_response;
    scalars[23] = input.parameters.rate_output_response;
    scalars[24] = input.parameters.max_quarterly_rate_step;
    scalars[25] = input.parameters.output_persistence;
    scalars[26] = input.parameters.fiscal_demand_multiplier;
    scalars[27] = input.parameters.real_rate_demand_sensitivity;
    scalars[28] = input.parameters.global_growth_sensitivity;
    scalars[29] = input.parameters.inflation_persistence;
    scalars[30] = input.parameters.inflation_expectations_weight;
    scalars[31] = input.parameters.phillips_curve_slope;
    scalars[32] = input.parameters.import_price_pass_through;
    scalars[33] = input.parameters.oil_inflation_sensitivity;
    scalars[34] = input.parameters.output_shock_sd;
    scalars[35] = input.parameters.inflation_shock_sd;
    scalars[36] = input.parameters.growth_shock_sd;
    scalars[37] = input.parameters.us_growth_shock_sd;
    scalars[38] = input.parameters.export_shock_sd;
    scalars[39] = input.parameters.us_export_shock_sd;

    std::array<double, kPathSeriesCount * kQuarterCount> paths{};
    auto copy_path = [&](std::size_t series, const auto& values) {
      std::copy(values.begin(), values.end(),
                paths.begin() + static_cast<std::ptrdiff_t>(series * kQuarterCount));
    };
    copy_path(0, input.fiscal);
    copy_path(1, input.productive_investment);
    copy_path(2, input.targeted_relief);
    copy_path(3, input.diversification);
    copy_path(4, input.deescalation);
    copy_path(5, input.us_tariff);
    copy_path(6, input.canada_tariff);
    copy_path(7, input.trade_drag);
    copy_path(8, input.us_supply_chain_drag);
    copy_path(9, input.import_price);
    copy_path(10, input.supply);
    copy_path(11, input.relief_cost);
    copy_path(12, input.canada_export_quantity_ratio);
    copy_path(13, input.us_export_quantity_ratio);

    std::vector<double> flat_innovations;
    flat_innovations.reserve(innovations.size() * kInnovationsPerQuarter);
    for (const auto& z : innovations) {
      flat_innovations.push_back(z.export_z);
      flat_innovations.push_back(z.us_export_z);
      flat_innovations.push_back(z.output_z);
      flat_innovations.push_back(z.inflation_z);
      flat_innovations.push_back(z.growth_z);
      flat_innovations.push_back(z.us_growth_z);
      flat_innovations.push_back(z.unemployment_z);
      flat_innovations.push_back(z.housing_z);
    }
    std::vector<double> flat_output(
        static_cast<std::size_t>(input.draws) * kOutputStride, 0.0);

    cl_int rc = CL_SUCCESS;
    cl_mem scalar_buffer = functions_.create_buffer(
        context_, CL_MEM_READ_ONLY, sizeof(scalars), nullptr, &rc);
    if (!scalar_buffer || rc != CL_SUCCESS) {
      error = "clCreateBuffer(scalars) failed: " + std::to_string(rc);
      return false;
    }
    BufferGuard scalar_guard(functions_, scalar_buffer);
    cl_mem path_buffer = functions_.create_buffer(
        context_, CL_MEM_READ_ONLY, sizeof(paths), nullptr, &rc);
    if (!path_buffer || rc != CL_SUCCESS) {
      error = "clCreateBuffer(paths) failed: " + std::to_string(rc);
      return false;
    }
    BufferGuard path_guard(functions_, path_buffer);
    cl_mem innovation_buffer = functions_.create_buffer(
        context_, CL_MEM_READ_ONLY, flat_innovations.size() * sizeof(double), nullptr, &rc);
    if (!innovation_buffer || rc != CL_SUCCESS) {
      error = "clCreateBuffer(innovations) failed: " + std::to_string(rc);
      return false;
    }
    BufferGuard innovation_guard(functions_, innovation_buffer);
    cl_mem output_buffer = functions_.create_buffer(
        context_, CL_MEM_WRITE_ONLY, flat_output.size() * sizeof(double), nullptr, &rc);
    if (!output_buffer || rc != CL_SUCCESS) {
      error = "clCreateBuffer(output) failed: " + std::to_string(rc);
      return false;
    }
    BufferGuard output_guard(functions_, output_buffer);

    if (!write_buffer(scalar_buffer, scalars.data(), sizeof(scalars), error)
        || !write_buffer(path_buffer, paths.data(), sizeof(paths), error)
        || !write_buffer(innovation_buffer, flat_innovations.data(),
                         flat_innovations.size() * sizeof(double), error)) return false;

    if ((rc = functions_.set_kernel_arg(kernel_, 0, sizeof(cl_mem), &scalar_buffer)) != CL_SUCCESS
        || (rc = functions_.set_kernel_arg(kernel_, 1, sizeof(cl_mem), &path_buffer)) != CL_SUCCESS
        || (rc = functions_.set_kernel_arg(kernel_, 2, sizeof(cl_mem), &innovation_buffer)) != CL_SUCCESS
        || (rc = functions_.set_kernel_arg(kernel_, 3, sizeof(cl_mem), &output_buffer)) != CL_SUCCESS
        || (rc = functions_.set_kernel_arg(kernel_, 4, sizeof(int), &input.draws)) != CL_SUCCESS) {
      error = "clSetKernelArg failed: " + std::to_string(rc);
      return false;
    }

    const std::size_t global = static_cast<std::size_t>(input.draws);
    rc = functions_.enqueue_nd_range_kernel(
        queue_, kernel_, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
    if (rc != CL_SUCCESS) {
      error = "clEnqueueNDRangeKernel failed: " + std::to_string(rc);
      return false;
    }
    rc = functions_.enqueue_read_buffer(
        queue_, output_buffer, CL_TRUE, 0, flat_output.size() * sizeof(double),
        flat_output.data(), 0, nullptr, nullptr);
    if (rc != CL_SUCCESS) {
      error = "clEnqueueReadBuffer failed: " + std::to_string(rc);
      return false;
    }
    rc = functions_.finish(queue_);
    if (rc != CL_SUCCESS) {
      error = "clFinish failed: " + std::to_string(rc);
      return false;
    }

    output.backend = "opencl-gpu";
    output.draws.assign(static_cast<std::size_t>(input.draws), DrawResult{});
    for (int d = 0; d < input.draws; ++d) {
      const std::size_t base = static_cast<std::size_t>(d) * kOutputStride;
      auto& draw = output.draws[static_cast<std::size_t>(d)];
      for (std::size_t q = 0; q < kQuarterCount; ++q) {
        draw.rates[q] = flat_output[base + 0 * kQuarterCount + q];
        draw.inflation[q] = flat_output[base + 1 * kQuarterCount + q];
        draw.growth[q] = flat_output[base + 2 * kQuarterCount + q];
        draw.us_growth[q] = flat_output[base + 3 * kQuarterCount + q];
        draw.debt[q] = flat_output[base + 4 * kQuarterCount + q];
        draw.cost[q] = flat_output[base + 5 * kQuarterCount + q];
        draw.exports[q] = flat_output[base + 6 * kQuarterCount + q];
        draw.us_exports[q] = flat_output[base + 7 * kQuarterCount + q];
      }
      draw.terminal_inflation = flat_output[base + 96];
      draw.terminal_growth = flat_output[base + 97];
      draw.terminal_us_growth = flat_output[base + 98];
      draw.terminal_unemployment = flat_output[base + 99];
      draw.terminal_debt = flat_output[base + 100];
      draw.terminal_housing = flat_output[base + 101];
      draw.terminal_cost = flat_output[base + 102];
      draw.terminal_income = flat_output[base + 103];
      draw.terminal_exports = flat_output[base + 104];
      draw.terminal_us_exports = flat_output[base + 105];
      draw.recession = flat_output[base + 106] != 0.0;
    }
    return true;
  }

 private:
  struct BufferGuard {
    Functions& functions;
    cl_mem value = nullptr;
    BufferGuard(Functions& f, cl_mem v) : functions(f), value(v) {}
    ~BufferGuard() { if (value) functions.release_mem_object(value); }
  };

  void release_all() {
    if (kernel_ && functions_.release_kernel) functions_.release_kernel(kernel_);
    if (program_ && functions_.release_program) functions_.release_program(program_);
    if (queue_ && functions_.release_command_queue) functions_.release_command_queue(queue_);
    if (context_ && functions_.release_context) functions_.release_context(context_);
    kernel_ = nullptr;
    program_ = nullptr;
    queue_ = nullptr;
    context_ = nullptr;
  }

  bool load_library() {
#ifdef _WIN32
    if (!library_.open("OpenCL.dll")) return false;
#elif defined(__APPLE__)
    if (!library_.open("/System/Library/Frameworks/OpenCL.framework/OpenCL")) return false;
#else
    if (!library_.open("libOpenCL.so.1") && !library_.open("libOpenCL.so")) return false;
#endif
    functions_.get_platform_ids = function<GetPlatformIDs>(library_, "clGetPlatformIDs");
    functions_.get_device_ids = function<GetDeviceIDs>(library_, "clGetDeviceIDs");
    functions_.get_device_info = function<GetDeviceInfo>(library_, "clGetDeviceInfo");
    functions_.create_context = function<CreateContext>(library_, "clCreateContext");
    functions_.release_context = function<ReleaseContext>(library_, "clReleaseContext");
    functions_.create_command_queue = function<CreateCommandQueue>(library_, "clCreateCommandQueue");
    functions_.release_command_queue = function<ReleaseCommandQueue>(library_, "clReleaseCommandQueue");
    functions_.create_program_with_source = function<CreateProgramWithSource>(library_, "clCreateProgramWithSource");
    functions_.build_program = function<BuildProgram>(library_, "clBuildProgram");
    functions_.get_program_build_info = function<GetProgramBuildInfo>(library_, "clGetProgramBuildInfo");
    functions_.release_program = function<ReleaseProgram>(library_, "clReleaseProgram");
    functions_.create_kernel = function<CreateKernel>(library_, "clCreateKernel");
    functions_.release_kernel = function<ReleaseKernel>(library_, "clReleaseKernel");
    functions_.create_buffer = function<CreateBuffer>(library_, "clCreateBuffer");
    functions_.release_mem_object = function<ReleaseMemObject>(library_, "clReleaseMemObject");
    functions_.set_kernel_arg = function<SetKernelArg>(library_, "clSetKernelArg");
    functions_.enqueue_write_buffer = function<EnqueueWriteBuffer>(library_, "clEnqueueWriteBuffer");
    functions_.enqueue_nd_range_kernel = function<EnqueueNDRangeKernel>(library_, "clEnqueueNDRangeKernel");
    functions_.enqueue_read_buffer = function<EnqueueReadBuffer>(library_, "clEnqueueReadBuffer");
    functions_.finish = function<Finish>(library_, "clFinish");
    return functions_.complete();
  }

  std::string device_string(cl_device_id device, cl_device_info parameter) const {
    std::size_t bytes = 0;
    if (functions_.get_device_info(device, parameter, 0, nullptr, &bytes) != CL_SUCCESS
        || bytes == 0) return {};
    std::string value(bytes, '\0');
    if (functions_.get_device_info(device, parameter, bytes, value.data(), nullptr) != CL_SUCCESS)
      return {};
    while (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
  }

  bool has_fp64(cl_device_id device) const {
    cl_bitfield config = 0;
    if (functions_.get_device_info(device, CL_DEVICE_DOUBLE_FP_CONFIG,
            sizeof(config), &config, nullptr) == CL_SUCCESS && config != 0) return true;
    const auto extensions = device_string(device, CL_DEVICE_EXTENSIONS);
    return extensions.find("cl_khr_fp64") != std::string::npos
        || extensions.find("cl_amd_fp64") != std::string::npos;
  }

  bool select_device(cl_device_type type) {
    cl_uint platform_count = 0;
    if (functions_.get_platform_ids(0, nullptr, &platform_count) != CL_SUCCESS
        || platform_count == 0) return false;
    std::vector<cl_platform_id> platforms(platform_count);
    if (functions_.get_platform_ids(platform_count, platforms.data(), nullptr) != CL_SUCCESS)
      return false;
    for (const auto platform : platforms) {
      cl_uint device_count = 0;
      if (functions_.get_device_ids(platform, type, 0, nullptr, &device_count) != CL_SUCCESS
          || device_count == 0) continue;
      std::vector<cl_device_id> devices(device_count);
      if (functions_.get_device_ids(platform, type, device_count, devices.data(), nullptr)
          != CL_SUCCESS) continue;
      for (const auto device : devices) {
        if (!has_fp64(device)) continue;
        device_ = device;
        probe_.device_present = true;
        probe_.fp64_supported = true;
        probe_.device_name = device_string(device, CL_DEVICE_NAME);
        return true;
      }
      // Record that a GPU/allowed device was present even when it lacked FP64.
      probe_.device_present = true;
    }
    return false;
  }

  void ensure_probe() {
    if (probed_) return;
    probed_ = true;
    if (!load_library()) {
      probe_.detail = "OpenCL runtime library not available";
      return;
    }
    probe_.library_present = true;
    if (select_device(CL_DEVICE_TYPE_GPU)) {
      probe_.detail = "FP64 OpenCL GPU available";
      return;
    }
    const char* allow_cpu = std::getenv("CAD_OPENCL_ALLOW_CPU");
    if (allow_cpu && std::string(allow_cpu) == "1" && select_device(CL_DEVICE_TYPE_CPU)) {
      probe_.detail = "FP64 OpenCL CPU device selected for equivalence testing";
      return;
    }
    probe_.detail = probe_.device_present
        ? "OpenCL device found but FP64 is unavailable"
        : "No OpenCL GPU device available";
  }

  std::string build_log() const {
    if (!program_ || !device_) return {};
    std::size_t bytes = 0;
    if (functions_.get_program_build_info(program_, device_, CL_PROGRAM_BUILD_LOG,
            0, nullptr, &bytes) != CL_SUCCESS || bytes == 0) return {};
    std::string log(bytes, '\0');
    if (functions_.get_program_build_info(program_, device_, CL_PROGRAM_BUILD_LOG,
            bytes, log.data(), nullptr) != CL_SUCCESS) return {};
    while (!log.empty() && log.back() == '\0') log.pop_back();
    return log;
  }

  bool ensure_program(std::string& error) {
    if (kernel_) return true;
    cl_int rc = CL_SUCCESS;
    context_ = functions_.create_context(nullptr, 1, &device_, nullptr, nullptr, &rc);
    if (!context_ || rc != CL_SUCCESS) {
      error = "clCreateContext failed: " + std::to_string(rc);
      return false;
    }
    queue_ = functions_.create_command_queue(context_, device_, 0, &rc);
    if (!queue_ || rc != CL_SUCCESS) {
      error = "clCreateCommandQueue failed: " + std::to_string(rc);
      return false;
    }
    const char* source = kernel_source();
    const std::size_t source_size = std::strlen(source);
    program_ = functions_.create_program_with_source(
        context_, 1, &source, &source_size, &rc);
    if (!program_ || rc != CL_SUCCESS) {
      error = "clCreateProgramWithSource failed: " + std::to_string(rc);
      return false;
    }
    rc = functions_.build_program(program_, 1, &device_, nullptr, nullptr, nullptr);
    if (rc != CL_SUCCESS) {
      error = "clBuildProgram failed: " + std::to_string(rc);
      const auto log = build_log();
      if (!log.empty()) error += ": " + log;
      return false;
    }
    kernel_ = functions_.create_kernel(program_, "cad_monte_carlo", &rc);
    if (!kernel_ || rc != CL_SUCCESS) {
      error = "clCreateKernel failed: " + std::to_string(rc);
      return false;
    }
    return true;
  }

  bool write_buffer(cl_mem buffer, const void* data, std::size_t bytes,
                    std::string& error) {
    const cl_int rc = functions_.enqueue_write_buffer(
        queue_, buffer, CL_TRUE, 0, bytes, data, 0, nullptr, nullptr);
    if (rc == CL_SUCCESS) return true;
    error = "clEnqueueWriteBuffer failed: " + std::to_string(rc);
    return false;
  }

  std::mutex mutex_;
  DynamicLibrary library_;
  Functions functions_;
  Probe probe_;
  bool probed_ = false;
  cl_device_id device_ = nullptr;
  cl_context context_ = nullptr;
  cl_command_queue queue_ = nullptr;
  cl_program program_ = nullptr;
  cl_kernel kernel_ = nullptr;
};

Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace

Probe probe() {
  return runtime().probe();
}

bool run(const Input& input, const std::vector<Innovation>& innovations,
         BatchResult& output, std::string& error) {
  return runtime().run(input, innovations, output, error);
}

}  // namespace cad::monte_carlo::opencl
