#pragma once
// C ABI for the isolated NVIDIA/CUDA backend. Kept separate from the C++ core so the core builds
// without CUDA. Real device discovery, allocation, H2D, kernels, events/synchronization, and
// memory accounting live here; everything reports counters only if they are genuinely available.
#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 on success, else a CUDA error code.
int iobs_cuda_probe(const char** device_name, int* compute_capability, long long* total_mem_bytes);

// Free device memory currently available (bytes). Uses cudaMemGetInfo.
long long iobs_cuda_free_memory_bytes(void);

// Compute-heavy kernel (no global-memory traffic). Returns elapsed wall ms via CUDA events,
// and the host CPU time consumed (ns) so host/device parity can be verified.
int iobs_cuda_compute_alone(unsigned long long iters, unsigned long long blocks, double* elapsed_ms, double* host_cpu_ms);
int iobs_cuda_compute_concurrent(unsigned long long iters, unsigned long long blocks, double* elapsed_ms_a, double* elapsed_ms_b);

// Memory-bound kernel over a real device buffer. Reports elapsed ms and achieved bandwidth (GiB/s).
int iobs_cuda_memory_alone(unsigned long long bytes, double* elapsed_ms, double* bw_gib_s);
int iobs_cuda_memory_concurrent(unsigned long long bytes, double* elapsed_ms_a, double* elapsed_ms_b);

// H2D + D2H transfer of a real device buffer. Reports elapsed ms and throughput (GiB/s).
int iobs_cuda_transfer_alone(unsigned long long bytes, double* elapsed_ms, double* bw_gib_s);
int iobs_cuda_transfer_concurrent(unsigned long long bytes, double* elapsed_ms_a, double* elapsed_ms_b);

#ifdef __cplusplus
}
#endif
