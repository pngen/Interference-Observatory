// Interference Observatory CUDA backend — real RTX 5090 (sm_120), CUDA 12.9.
// This translation unit is built only when CUDAToolkit is found. All entry points are C-ABI so
// the C++ core can link the isolated CUDA backend without exposing CUDA headers publicly.
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include "observatory/cuda_api.h"

static cudaStream_t make_stream() {
  cudaStream_t s = nullptr;
  cudaStreamCreate(&s);
  return s;
}

// ---------------------------------------------------------------------------

extern "C" {

int iobs_cuda_probe(const char** device_name, int* compute_capability, long long* total_mem_bytes) {
  int deviceId = 0;
  cudaDeviceProp prop;
  cudaError_t e = cudaGetDeviceProperties(&prop, deviceId);
  if (e != cudaSuccess) return static_cast<int>(e);
  if (device_name) *device_name = prop.name;
  if (compute_capability) *compute_capability = prop.major * 10 + prop.minor;
  if (total_mem_bytes) *total_mem_bytes = static_cast<long long>(prop.totalGlobalMem);
  return 0;
}

long long iobs_cuda_free_memory_bytes(void) {
  std::size_t freeBytes = 0, totalBytes = 0;
  cudaMemGetInfo(&freeBytes, &totalBytes);
  return static_cast<long long>(freeBytes);
}

__global__ void compute_heavy_kernel(unsigned long long iters, float* out) {
  float a = static_cast<float>(threadIdx.x) + static_cast<float>(blockIdx.x) * 0.5f;
  const float b = 1.0000001f;
  for (unsigned long long i = 0; i < iters; ++i) {
    a = fmaf(a, b, 1.0000001f);
  }
  if (a == 123456.0f) out[blockIdx.x] = a;
}

__global__ void memory_heavy_kernel(float* buf, unsigned long long n) {
  const unsigned long long idx = static_cast<unsigned long long>(blockIdx.x) * blockDim.x + threadIdx.x;
  const unsigned long long stride = static_cast<unsigned long long>(gridDim.x) * blockDim.x;
  float s = 0.0f;
  for (unsigned long long i = idx; i < n; i += stride) s += buf[i];
  if (s == 123456.0f) buf[0] = s;
}

static float* alloc_buffer(unsigned long long bytes, bool& ok) {
  float* buf = nullptr;
  if (cudaMalloc(&buf, bytes) != cudaSuccess) { ok = false; return nullptr; }
  ok = true;
  return buf;
}

static void sync_current() { cudaDeviceSynchronize(); }

static double launch_compute(unsigned long long iters, unsigned long long blocks, cudaStream_t stream) {
  float* sink = nullptr;
  bool ok = false;
  sink = alloc_buffer(64, ok);
  cudaEvent_t start = nullptr, stop = nullptr;
  cudaEventCreate(&start); cudaEventCreate(&stop);
  // Warmup launch so the timed run is not cold (cold/warm mismatch must not confound comparison).
  compute_heavy_kernel<<<blocks, 256, 0, stream>>>(iters, sink);
  cudaDeviceSynchronize();
  cudaEventRecord(start, stream);
  compute_heavy_kernel<<<blocks, 256, 0, stream>>>(iters, sink);
  cudaEventRecord(stop, stream);
  cudaEventSynchronize(stop);
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, start, stop);
  cudaEventDestroy(start); cudaEventDestroy(stop);
  if (sink) cudaFree(sink);
  return static_cast<double>(ms);
}

static double launch_memory(unsigned long long bytes, cudaStream_t stream, double* bw_out) {
  float* buf = nullptr;
  bool ok = false;
  buf = alloc_buffer(bytes, ok);
  const unsigned long long n = bytes / sizeof(float);
  cudaEvent_t start = nullptr, stop = nullptr;
  cudaEventCreate(&start); cudaEventCreate(&stop);
  // Warmup pass.
  memory_heavy_kernel<<<(unsigned int)((n + 255) / 256), 256, 0, stream>>>(buf, n);
  cudaDeviceSynchronize();
  cudaEventRecord(start, stream);
  memory_heavy_kernel<<<(unsigned int)((n + 255) / 256), 256, 0, stream>>>(buf, n);
  cudaEventRecord(stop, stream);
  cudaEventSynchronize(stop);
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, start, stop);
  cudaEventDestroy(start); cudaEventDestroy(stop);
  const double sec = static_cast<double>(ms) / 1000.0;
  if (bw_out) *bw_out = (sec > 0.0) ? (static_cast<double>(n) * sizeof(float)) / (sec * 1024.0 * 1024.0 * 1024.0) : 0.0;
  if (buf) cudaFree(buf);
  return static_cast<double>(ms);
}

static double launch_transfer(unsigned long long bytes, cudaStream_t stream, double* bw_out) {
  float* dev = nullptr; bool ok = false;
  dev = alloc_buffer(bytes, ok);
  float* host_data = static_cast<float*>(std::malloc(bytes));
  cudaEvent_t start = nullptr, stop = nullptr;
  cudaEventCreate(&start); cudaEventCreate(&stop);
  // Warmup transfer.
  cudaMemcpyAsync(dev, host_data, bytes, cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(host_data, dev, bytes, cudaMemcpyDeviceToHost, stream);
  cudaDeviceSynchronize();
  cudaEventRecord(start, stream);
  cudaMemcpyAsync(dev, host_data, bytes, cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(host_data, dev, bytes, cudaMemcpyDeviceToHost, stream);
  cudaEventRecord(stop, stream);
  cudaEventSynchronize(stop);
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, start, stop);
  cudaEventDestroy(start); cudaEventDestroy(stop);
  const double sec = static_cast<double>(ms) / 1000.0;
  if (bw_out) *bw_out = (sec > 0.0) ? (2.0 * static_cast<double>(bytes)) / (sec * 1024.0 * 1024.0 * 1024.0) : 0.0;
  if (dev) cudaFree(dev);
  std::free(host_data);
  return static_cast<double>(ms);
}

int iobs_cuda_compute_alone(unsigned long long iters, unsigned long long blocks, double* elapsed_ms, double* host_cpu_ms) {
  cudaStream_t s = make_stream();
  double ms = launch_compute(iters, blocks, s);
  if (host_cpu_ms) *host_cpu_ms = 0.0;  // host time not separately measured here; see note
  cudaStreamDestroy(s);
  sync_current();
  if (elapsed_ms) *elapsed_ms = ms;
  return 0;
}

int iobs_cuda_compute_concurrent(unsigned long long iters, unsigned long long blocks, double* elapsed_ms_a, double* elapsed_ms_b) {
  cudaStream_t sa = make_stream(), sb = make_stream();
  double ma = launch_compute(iters, blocks, sa);
  double mb = launch_compute(iters, blocks, sb);
  cudaStreamDestroy(sa); cudaStreamDestroy(sb);
  sync_current();
  if (elapsed_ms_a) *elapsed_ms_a = ma;
  if (elapsed_ms_b) *elapsed_ms_b = mb;
  return 0;
}

int iobs_cuda_memory_alone(unsigned long long bytes, double* elapsed_ms, double* bw_gib_s) {
  cudaStream_t s = make_stream();
  double bw = 0.0;
  double ms = launch_memory(bytes, s, &bw);
  cudaStreamDestroy(s);
  sync_current();
  if (elapsed_ms) *elapsed_ms = ms;
  if (bw_gib_s) *bw_gib_s = bw;
  return 0;
}

int iobs_cuda_memory_concurrent(unsigned long long bytes, double* elapsed_ms_a, double* elapsed_ms_b) {
  cudaStream_t sa = make_stream(), sb = make_stream();
  double bwa = 0.0, bwb = 0.0;
  double ma = launch_memory(bytes, sa, &bwa);
  double mb = launch_memory(bytes, sb, &bwb);
  cudaStreamDestroy(sa); cudaStreamDestroy(sb);
  sync_current();
  if (elapsed_ms_a) *elapsed_ms_a = ma;
  if (elapsed_ms_b) *elapsed_ms_b = mb;
  return 0;
}

int iobs_cuda_transfer_alone(unsigned long long bytes, double* elapsed_ms, double* bw_gib_s) {
  cudaStream_t s = make_stream();
  double bw = 0.0;
  double ms = launch_transfer(bytes, s, &bw);
  cudaStreamDestroy(s);
  sync_current();
  if (elapsed_ms) *elapsed_ms = ms;
  if (bw_gib_s) *bw_gib_s = bw;
  return 0;
}

int iobs_cuda_transfer_concurrent(unsigned long long bytes, double* elapsed_ms_a, double* elapsed_ms_b) {
  cudaStream_t sa = make_stream(), sb = make_stream();
  double bwa = 0.0, bwb = 0.0;
  double ma = launch_transfer(bytes, sa, &bwa);
  double mb = launch_transfer(bytes, sb, &bwb);
  cudaStreamDestroy(sa); cudaStreamDestroy(sb);
  sync_current();
  if (elapsed_ms_a) *elapsed_ms_a = ma;
  if (elapsed_ms_b) *elapsed_ms_b = mb;
  return 0;
}

}  // extern "C"
