#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cad::accelerator {

struct Status {
  bool gpu_present = false;
  std::string provider = "none";
  int device_count = 0;
  std::uint64_t total_vram_bytes = 0;
  std::string active_backend = "cpu-multicore";
  std::string recommended_workload = "batched Monte Carlo simulation";
  std::string policy =
      "VRAM is not used as a result cache; GPU compute is promoted only after a deterministic equivalence gate";
};

namespace detail {

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

inline Status detect_cuda() {
  Status status;
  DynamicLibrary library;
#ifdef _WIN32
  if (!library.open("nvcuda.dll")) return status;
#else
  if (!library.open("libcuda.so.1") && !library.open("libcuda.so")) return status;
#endif

  using Init = int (*)(unsigned int);
  using DeviceGetCount = int (*)(int*);
  using DeviceTotalMem = int (*)(std::size_t*, int);
  const auto init = function<Init>(library, "cuInit");
  const auto get_count = function<DeviceGetCount>(library, "cuDeviceGetCount");
  auto total_mem = function<DeviceTotalMem>(library, "cuDeviceTotalMem_v2");
  if (!total_mem) total_mem = function<DeviceTotalMem>(library, "cuDeviceTotalMem");
  if (!init || !get_count || init(0) != 0) return status;

  int count = 0;
  if (get_count(&count) != 0 || count <= 0) return status;
  status.gpu_present = true;
  status.provider = "cuda";
  status.device_count = count;
  if (total_mem) {
    for (int device = 0; device < count; ++device) {
      std::size_t bytes = 0;
      if (total_mem(&bytes, device) == 0)
        status.total_vram_bytes += static_cast<std::uint64_t>(bytes);
    }
  }
  return status;
}

inline Status detect_hip() {
  Status status;
  DynamicLibrary library;
#ifdef _WIN32
  if (!library.open("amdhip64.dll")) return status;
#else
  if (!library.open("libamdhip64.so")) return status;
#endif

  using GetDeviceCount = int (*)(int*);
  using SetDevice = int (*)(int);
  using MemGetInfo = int (*)(std::size_t*, std::size_t*);
  const auto get_count = function<GetDeviceCount>(library, "hipGetDeviceCount");
  const auto set_device = function<SetDevice>(library, "hipSetDevice");
  const auto mem_info = function<MemGetInfo>(library, "hipMemGetInfo");
  if (!get_count) return status;

  int count = 0;
  if (get_count(&count) != 0 || count <= 0) return status;
  status.gpu_present = true;
  status.provider = "hip";
  status.device_count = count;
  if (set_device && mem_info) {
    for (int device = 0; device < count; ++device) {
      if (set_device(device) != 0) continue;
      std::size_t free_bytes = 0;
      std::size_t total_bytes = 0;
      if (mem_info(&free_bytes, &total_bytes) == 0)
        status.total_vram_bytes += static_cast<std::uint64_t>(total_bytes);
    }
  }
  return status;
}

}  // namespace detail

inline Status detect() {
  Status status = detail::detect_cuda();
  if (status.gpu_present) return status;
  return detail::detect_hip();
}

}  // namespace cad::accelerator
