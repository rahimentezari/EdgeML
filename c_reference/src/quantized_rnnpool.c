// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <string.h>
#include "quantized_rnnpool.h"

int q_rnnpool_block(const INT_T* const patch, ITER_T inputDims, ITER_T patchDim,
                    ITER_T stride, q_rnn_t rnn1, ITER_T hiddenDims1,
                    const void* rnn1_params, void* rnn1_buffers,
                    const void* rnn1_scales, q_rnn_t rnn2,
                    ITER_T hiddenDims2, const void* rnn2_params,
                    void* rnn2_buffers, const void* rnn2_scales,
                    INT_T* const output, INT_T* const buffer) {
  // Clear the output
  memset(output, 0, sizeof(INT_T) * 4 * hiddenDims2);

  // Horizontal pass over each row with RNN1
  memset(buffer, 0, sizeof(INT_T) * hiddenDims1 * patchDim);
  for (ITER_T r = 0; r < patchDim; ++r) {
    rnn1(buffer + r * hiddenDims1, hiddenDims1, patch + stride * r * inputDims,
         inputDims, patchDim, rnn1_params, rnn1_buffers, rnn1_scales, 0, 0);
  }

  // Bi-directional vertical pass over the row summaries
  rnn2(output, hiddenDims2, buffer, hiddenDims1, patchDim, rnn2_params,
       rnn2_buffers, rnn2_scales, 0, 0);
  rnn2(output + hiddenDims2, hiddenDims2, buffer, hiddenDims1, patchDim,
       rnn2_params, rnn2_buffers, rnn2_scales, 1, 0);

  // Vertical pass over each column with RNN1
  memset(buffer, 0, sizeof(INT_T) * hiddenDims1 * patchDim);
  for (ITER_T c = 0; c < patchDim; ++c) {
    for (ITER_T r = 0; r < patchDim; ++r) {
      rnn1(buffer + c * hiddenDims1, hiddenDims1,
           patch + (stride * r + c) * inputDims, inputDims, 1, rnn1_params,
           rnn1_buffers, rnn1_scales, 0, 0);
    }
  }

  // Bi-directional horizontal pass over the columns summaries
  rnn2(output + 2 * hiddenDims2, hiddenDims2, buffer, hiddenDims1, patchDim,
       rnn2_params, rnn2_buffers, rnn2_scales, 0, 0);
  rnn2(output + 3 * hiddenDims2, hiddenDims2, buffer, hiddenDims1, patchDim,
       rnn2_params, rnn2_buffers, rnn2_scales, 1, 0);

  return 0;
}
