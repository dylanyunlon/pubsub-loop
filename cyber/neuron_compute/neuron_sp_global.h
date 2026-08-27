/******************************************************************************
 * neuron_sp_global.h — World runtime global constant macro
 *
 * PRD #269: Make _Neuron_SP_GLOBAL_CONSTANT inline when RDC is enabled.
 *
 * When __CUDACC_RDC__ is defined (Relocatable Device Code mode), constants
 * declared with this macro must be inline to satisfy the One Definition Rule
 * across multiple translation units (.cu files) that include the same header.
 *
 * Without this fix, multiple .cu files including the same world-constant
 * header produce "multiple definition" linker errors under RDC.
 *****************************************************************************/

#ifndef CYBER_NEURON_COMPUTE_NEURON_SP_GLOBAL_H_
#define CYBER_NEURON_COMPUTE_NEURON_SP_GLOBAL_H_

#if defined(__NEURIP_ARCH__)

  #if defined(__CUDA_ARCH__)
    // Device compilation path
    #ifdef __CUDACC_RDC__
      // RDC enabled: __device__ __constant__ inline ensures ODR compliance
      // when multiple TUs reference the same constant.
      #define _Neuron_SP_GLOBAL_CONSTANT __device__ __constant__ inline
    #else
      // Non-RDC: original behavior (single TU per constant is assumed)
      #define _Neuron_SP_GLOBAL_CONSTANT __device__ __constant__
    #endif
  #else
    // Host compilation path (for __host__ __device__ functions)
    #ifdef __CUDACC_RDC__
      #define _Neuron_SP_GLOBAL_CONSTANT inline constexpr
    #else
      #define _Neuron_SP_GLOBAL_CONSTANT constexpr
    #endif
  #endif

#else
  // Pure host build (no CUDA)
  #define _Neuron_SP_GLOBAL_CONSTANT inline constexpr
#endif

#endif  // CYBER_NEURON_COMPUTE_NEURON_SP_GLOBAL_H_
