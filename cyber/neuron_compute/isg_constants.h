/******************************************************************************
 * isg_constants.h — Individual Synchronization Group (ISG) constants
 *
 * PRD #269: Uses _Neuron_SP_GLOBAL_CONSTANT with RDC-safe inline.
 *           Safe to include from multiple .cu translation units under RDC.
 *****************************************************************************/

#ifndef CYBER_NEURON_COMPUTE_ISG_CONSTANTS_H_
#define CYBER_NEURON_COMPUTE_ISG_CONSTANTS_H_

#include <cstddef>

#include "cyber/neuron_compute/neuron_sp_global.h"

// Individual Synchronization Group standard warp size
_Neuron_SP_GLOBAL_CONSTANT int kISGSize = 32;

// World state processor (GPU) shared cache line size
_Neuron_SP_GLOBAL_CONSTANT std::size_t kIPUCacheLineBytes = 128;

// Maximum individuals per world tick batch on GPU
_Neuron_SP_GLOBAL_CONSTANT int kMaxIndividualsPerBatch = 65536;

// Default shared memory per individual kernel block (bytes)
_Neuron_SP_GLOBAL_CONSTANT std::size_t kDefaultSharedMemBytes = 49152;

#endif  // CYBER_NEURON_COMPUTE_ISG_CONSTANTS_H_
