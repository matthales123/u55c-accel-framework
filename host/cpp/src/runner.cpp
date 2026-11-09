#include "u55c_framework/runner.hpp"

#include "u55c_framework/buffer_manager.hpp"
#include "u55c_framework/profiler.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#if defined(U55C_ENABLE_XRT)
#include <xrt/experimental/xrt_xclbin.h>
#include <xrt/xrt_bo.h>
#include <xrt/xrt_device.h>
#include <xrt/xrt_kernel.h>
#endif

namespace u55c_framework {
namespace {

std::string utc_now_iso8601() {
  const std::time_t now = std::time(nullptr);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &now);
#else
  gmtime_r(&now, &tm_utc);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

double throughput_gbps(const std::size_t elements, const double total_ms) {
  if (total_ms <= 0.0) {
    return 0.0;
  }

  const double bytes_moved = static_cast<double>(elements) * sizeof(std::uint32_t) * 3.0;
  const double bits_moved = bytes_moved * 8.0;
  return bits_moved / (total_ms * 1.0e6);
}

void fill_inputs(std::uint32_t* a,
                 std::uint32_t* b,
                 std::vector<std::uint32_t>& expected,
                 const std::size_t elements,
                 const std::uint32_t seed,
                 const std::size_t iteration) {
  std::mt19937 rng(seed + static_cast<std::uint32_t>(iteration));
  std::uniform_int_distribution<std::uint32_t> dist(0u, 0x00FFFFFFu);

  for (std::size_t i = 0; i < elements; ++i) {
    const auto va = dist(rng);
    const auto vb = dist(rng);
    a[i] = va;
    b[i] = vb;
    expected[i] = va + vb;
  }
}

bool verify_outputs(const std::vector<std::uint32_t>& expected,
                    const std::uint32_t* out,
                    const std::size_t elements) {
  for (std::size_t i = 0; i < elements; ++i) {
    if (out[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

BenchmarkRow base_row(const BenchmarkConfig& cfg, const std::size_t iteration) {
  BenchmarkRow row;
  row.timestamp_utc = utc_now_iso8601();
  row.mode = run_mode_string(cfg.mode);
  row.kernel = cfg.kernel_name;
  row.device_index = cfg.device_index;
  row.elements = cfg.elements;
  row.iteration = iteration;
  row.verify_ok = false;
  return row;
}

std::vector<BenchmarkRow> run_dry_run(const BenchmarkConfig& cfg) {
  std::vector<BenchmarkRow> rows;
  rows.reserve(cfg.iterations);

  std::vector<std::uint32_t> host_a(cfg.elements);
  std::vector<std::uint32_t> host_b(cfg.elements);
  std::vector<std::uint32_t> expected(cfg.elements);
  std::vector<std::uint32_t> dev_a(cfg.elements);
  std::vector<std::uint32_t> dev_b(cfg.elements);
  std::vector<std::uint32_t> dev_out(cfg.elements);
  std::vector<std::uint32_t> host_out(cfg.elements);

  for (std::size_t iteration = 0; iteration < cfg.iterations; ++iteration) {
    auto row = base_row(cfg, iteration);
    fill_inputs(host_a.data(), host_b.data(), expected, cfg.elements, cfg.seed, iteration);

    const auto t_total_start = Clock::now();

    const auto t_h2d_start = Clock::now();
    std::copy(host_a.begin(), host_a.end(), dev_a.begin());
    std::copy(host_b.begin(), host_b.end(), dev_b.begin());
    const auto t_h2d_end = Clock::now();

    const auto t_kernel_start = Clock::now();
    for (std::size_t i = 0; i < cfg.elements; ++i) {
      dev_out[i] = dev_a[i] + dev_b[i];
    }
    const auto t_kernel_end = Clock::now();

    const auto t_d2h_start = Clock::now();
    std::copy(dev_out.begin(), dev_out.end(), host_out.begin());
    const auto t_d2h_end = Clock::now();

    const bool verify_ok = !cfg.verify || verify_outputs(expected, host_out.data(), cfg.elements);
    const auto t_total_end = Clock::now();

    row.h2d_ms = elapsed_ms(t_h2d_start, t_h2d_end);
    row.kernel_ms = elapsed_ms(t_kernel_start, t_kernel_end);
    row.d2h_ms = elapsed_ms(t_d2h_start, t_d2h_end);
    row.total_ms = elapsed_ms(t_total_start, t_total_end);
    row.verify_ok = verify_ok;
    row.throughput_gbps = throughput_gbps(cfg.elements, row.total_ms);
    rows.push_back(row);
  }

  return rows;
}

#if defined(U55C_ENABLE_XRT)
std::vector<BenchmarkRow> run_hardware(const BenchmarkConfig& cfg) {
  if (cfg.xclbin_path.empty()) {
    throw std::runtime_error("hardware mode requires --xclbin <path>");
  }

  if (cfg.elements > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::runtime_error("elements exceeds kernel arg width (uint32)");
  }

  std::vector<BenchmarkRow> rows;
  rows.reserve(cfg.iterations);

  const std::size_t bytes = cfg.elements * sizeof(std::uint32_t);

  xrt::device device(cfg.device_index);
  auto uuid = device.load_xclbin(cfg.xclbin_path);
  xrt::kernel kernel(device, uuid, cfg.kernel_name);
  BufferManager buffers(device, kernel, bytes);

  std::vector<std::uint32_t> expected(cfg.elements);

  for (std::size_t iteration = 0; iteration < cfg.iterations; ++iteration) {
    auto row = base_row(cfg, iteration);

    fill_inputs(buffers.input_a(), buffers.input_b(), expected, cfg.elements, cfg.seed, iteration);

    const auto t_total_start = Clock::now();

    const auto t_h2d_start = Clock::now();
    buffers.sync_inputs_to_device();
    const auto t_h2d_end = Clock::now();

    const auto t_kernel_start = Clock::now();
    auto run = kernel(
        buffers.input_a_bo(),
        buffers.input_b_bo(),
        buffers.output_bo(),
        static_cast<std::uint32_t>(cfg.elements));
    run.wait();
    const auto t_kernel_end = Clock::now();

    const auto t_d2h_start = Clock::now();
    buffers.sync_output_from_device();
    const auto t_d2h_end = Clock::now();

    const bool verify_ok = !cfg.verify || verify_outputs(expected, buffers.output(), cfg.elements);
    const auto t_total_end = Clock::now();

    row.h2d_ms = elapsed_ms(t_h2d_start, t_h2d_end);
    row.kernel_ms = elapsed_ms(t_kernel_start, t_kernel_end);
    row.d2h_ms = elapsed_ms(t_d2h_start, t_d2h_end);
    row.total_ms = elapsed_ms(t_total_start, t_total_end);
    row.verify_ok = verify_ok;
    row.throughput_gbps = throughput_gbps(cfg.elements, row.total_ms);
    rows.push_back(row);
  }

  return rows;
}
#endif

}  // namespace

const char* run_mode_string(const RunMode mode) {
  switch (mode) {
    case RunMode::DryRun:
      return "dry-run";
    case RunMode::Hardware:
      return "hw";
    default:
      return "unknown";
  }
}

std::vector<BenchmarkRow> run_benchmark(const BenchmarkConfig& cfg) {
  if (cfg.elements == 0) {
    throw std::runtime_error("elements must be > 0");
  }
  if (cfg.iterations == 0) {
    throw std::runtime_error("iterations must be > 0");
  }

  switch (cfg.mode) {
    case RunMode::DryRun:
      return run_dry_run(cfg);
    case RunMode::Hardware:
#if defined(U55C_ENABLE_XRT)
      return run_hardware(cfg);
#else
      throw std::runtime_error(
          "hardware mode requested, but this binary was built without XRT support");
#endif
    default:
      throw std::runtime_error("unsupported run mode");
  }
}

}  // namespace u55c_framework
