// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "quantized_fastgrnn.h"

int q_fastgrnn_lr(INT_T* const hiddenState, ITER_T hiddenDims,
                  const INT_T* const input, ITER_T inputDims, ITER_T steps,
                  const void* params, void* buffers, const void *scales,
                  int backward, int normalize) {
  const Q_FastGRNN_LR_Params* tparams = (const Q_FastGRNN_LR_Params*)params;
  Q_FastGRNN_LR_Buffers* tbuffers = (Q_FastGRNN_LR_Buffers*)buffers;
  const Q_FastGRNN_LR_Scales* tscales = (const Q_FastGRNN_LR_Scales*)scales;

  if (tbuffers->preComp1 == 0) return ERR_PRECOMP_NOT_INIT;
  if (tbuffers->preComp2 == 0) return ERR_PRECOMP_NOT_INIT;
  if (tbuffers->preComp3 == 0) return ERR_PRECOMP_NOT_INIT;
  if (tbuffers->tempLRW == 0) return ERR_TEMPLRW_NOT_INIT;
  if (tbuffers->tempLRU == 0) return ERR_TEMPLRU_NOT_INIT;
  if (tbuffers->normFeatures == 0) return ERR_NORMFEATURES_NOT_INIT;

  // #steps iterations of the RNN cell starting from hiddenState
  for (ITER_T t = 0; t < steps; t++) {
    // Normalize the features
    ITER_T offset = backward ? steps - 1 - t : t;
    if (normalize) {
      // This diverges from the original implementation because of
      // impracticality of scaled addition beyond 0, 1, and -1 multipliers
      v_q_sub(input + offset * inputDims, tparams->mean + offset * inputDims,
              inputDims, tbuffers->normFeatures, tscales->input,
              tscales->mean, tscales->meanSub);
      // Assuming the stdDev values are stored in inverse form
      v_q_hadamard(tparams->stdDev + offset * inputDims, tbuffers->normFeatures,
                   inputDims, tbuffers->normFeatures, tscales->stdDev,
                   tscales->normFeaturesHDStdDev);
    }
    else {
      for (ITER_T d = 0; d < inputDims; ++d)
        tbuffers->normFeatures[d] = input[offset * inputDims + d];
    }

    // Process the new input and previous hidden state
    m_q_mulvec(tparams->W1, tbuffers->normFeatures, tparams->wRank, inputDims,
               tbuffers->tempLRW, tscales->W1, tscales->normFeaturesMVW1, tscales->H1W1,
               tscales->H2W1);
    m_q_mulvec(tparams->W2, tbuffers->tempLRW, hiddenDims, tparams->wRank,
               tbuffers->preComp1, tscales->W2, tscales->tempLRW, tscales->H1W2,
               tscales->H2W2);
    m_q_mulvec(tparams->U1, hiddenState, tparams->uRank, hiddenDims,
               tbuffers->tempLRU, tscales->U1, tscales->hiddenStateMVU1, tscales->H1U1,
               tscales->H2U1);
    m_q_mulvec(tparams->U2, tbuffers->tempLRU, hiddenDims, tparams->uRank,
               tbuffers->preComp2, tscales->U2, tscales->tempLRU, tscales->H1U2,
               tscales->H2U2);
    v_q_add(tbuffers->preComp1, tbuffers->preComp2, hiddenDims,
            tbuffers->preComp1, tscales->mV2AddMV4, tscales->mV4AddMV2,
            tscales->mV2AddMV4Out);

    // Apply the gate to generate the new hidden state
    v_q_add(tbuffers->preComp1, tparams->Bg, hiddenDims, tbuffers->preComp2,
            tscales->pC1AddBg, tscales->Bg, tscales->pC1AddBgOut);
    v_q_sigmoid(tbuffers->preComp2, hiddenDims, tbuffers->preComp2, tscales->div,
                tscales->add, tscales->sigmoidLimit, tscales->sigmoidScaleIn,
                tscales->sigmoidScaleOut);
    v_q_add(tbuffers->preComp1, tparams->Bh, hiddenDims, tbuffers->preComp1,
            tscales->pC1AddBh, tscales->Bh, tscales->pC1AddBhOut);
    v_q_tanh(tbuffers->preComp1, hiddenDims, tbuffers->preComp1,
             tscales->tanhScaleIn, tscales->tanhScaleOut);
    v_q_hadamard(tbuffers->preComp2, hiddenState, hiddenDims, tbuffers->preComp3,
                 tscales->gateHDHiddenState, tscales->hiddenStateHDGate);
    v_q_scalar_sub(tscales->qOne, tbuffers->preComp2, hiddenDims,
                   tbuffers->preComp2, tscales->qOneScale, tscales->qOneSubGate,
                   tscales->qOneSubGateOut);
    v_q_scalar_mul(tparams->sigmoid_zeta, tbuffers->preComp2, hiddenDims,
                   tbuffers->preComp2, tscales->sigmoidZeta,
                   tscales->sigmoidZetaMulQOneSubGate);
    v_q_scalar_add(tparams->sigmoid_nu, tbuffers->preComp2, hiddenDims,
                   tbuffers->preComp2, tscales->sigmoidNu,
                   tscales->sigmoidNuAddQOneSubGate,
                   tscales->sigmoidNuAddQOneSubGateOut);
    v_q_hadamard(tbuffers->preComp2, tbuffers->preComp1, hiddenDims,
                 tbuffers->preComp1, tscales->sigmoidNuAddQOneSubGateHDUpdate,
                 tscales->updateHDSigmoidNuAddQOneSubGate);
    v_q_add(tbuffers->preComp3, tbuffers->preComp1, hiddenDims, hiddenState,
            tscales->pC3AddPC1, tscales->pC1AddPC3, tscales->hiddenStateOut);
  }
  return 0;
}

int q_fastgrnn(INT_T* const hiddenState, ITER_T hiddenDims,
               const INT_T* const input, ITER_T inputDims, ITER_T steps,
               const void* params, void* buffers, const void* scales,
               int backward, int normalize) {

  const Q_FastGRNN_Params* tparams = (const Q_FastGRNN_Params*)params;
  Q_FastGRNN_Buffers* tbuffers = (Q_FastGRNN_Buffers*)buffers;
  const Q_FastGRNN_Scales* tscales = (const Q_FastGRNN_Scales*)scales;

  if (tbuffers->preComp1 == 0) return ERR_PRECOMP_NOT_INIT;
  if (tbuffers->preComp2 == 0) return ERR_PRECOMP_NOT_INIT;
  if (tbuffers->preComp3 == 0) return ERR_PRECOMP_NOT_INIT;
  if (tbuffers->normFeatures == 0) return ERR_NORMFEATURES_NOT_INIT;

  for (ITER_T t = 0; t < steps; t++) {
    // Normalize the features
    ITER_T offset = backward ? steps - 1 - t : t;
    if (normalize) {
      // This diverges from the original implementation because of
      // impracticality of scaled addition beyond 0, 1, and -1 multipliers
      v_q_sub(input + offset * inputDims, tparams->mean + offset * inputDims,
              inputDims, tbuffers->normFeatures, tscales->input,
              tscales->mean, tscales->meanSub);
      // Assuming stdDev values are stored in inverse form
      v_q_hadamard(tparams->stdDev + offset * inputDims, tbuffers->normFeatures,
                   inputDims, tbuffers->normFeatures, tscales->stdDev,
                   tscales->normFeaturesHDStdDev);
    }
    else {
      for (ITER_T d = 0; d < inputDims; ++d)
        tbuffers->normFeatures[d] = input[offset * inputDims + d];
    }

    // Process the new input and previous hidden state
    m_q_mulvec(tparams->W, tbuffers->normFeatures, hiddenDims, inputDims,
               tbuffers->preComp1, tscales->W, tscales->normFeaturesMVW, tscales->H1W,
               tscales->H2W);
    m_q_mulvec(tparams->U, hiddenState, hiddenDims, hiddenDims,
               tbuffers->preComp2, tscales->U, tscales->hiddenStateMVU, tscales->H1U,
               tscales->H2U);
    v_q_add(tbuffers->preComp1, tbuffers->preComp2, hiddenDims,
            tbuffers->preComp1, tscales->mV1AddMV2, tscales->mV2AddMV1,
            tscales->mV1AddMV2Out);

    // Apply the gate to generate the new hidden state
    v_q_add(tbuffers->preComp1, tparams->Bg, hiddenDims, tbuffers->preComp2,
            tscales->pC1AddBg, tscales->Bg, tscales->pC1AddBgOut);
    v_q_sigmoid(tbuffers->preComp2, hiddenDims, tbuffers->preComp2, tscales->div,
                tscales->add, tscales->sigmoidLimit, tscales->sigmoidScaleIn,
                tscales->sigmoidScaleOut);
    v_q_add(tbuffers->preComp1, tparams->Bh, hiddenDims, tbuffers->preComp1,
            tscales->pC1AddBh, tscales->Bh, tscales->pC1AddBhOut);
    v_q_tanh(tbuffers->preComp1, hiddenDims, tbuffers->preComp1,
             tscales->tanhScaleIn, tscales->tanhScaleOut);
    v_q_hadamard(tbuffers->preComp2, hiddenState, hiddenDims, tbuffers->preComp3,
                 tscales->gateHDHiddenState, tscales->hiddenStateHDGate);
    v_q_scalar_sub(tscales->qOne, tbuffers->preComp2, hiddenDims,
                   tbuffers->preComp2, tscales->qOneScale, tscales->qOneSubGate,
                   tscales->qOneSubGateOut);
    v_q_scalar_mul(tparams->sigmoid_zeta, tbuffers->preComp2, hiddenDims,
                   tbuffers->preComp2, tscales->sigmoidZeta,
                   tscales->sigmoidZetaMulQOneSubGate);
    v_q_scalar_add(tparams->sigmoid_nu, tbuffers->preComp2, hiddenDims,
                   tbuffers->preComp2, tscales->sigmoidNu,
                   tscales->sigmoidNuAddQOneSubGate,
                   tscales->sigmoidNuAddQOneSubGateOut);
    v_q_hadamard(tbuffers->preComp2, tbuffers->preComp1, hiddenDims,
                 tbuffers->preComp1, tscales->sigmoidNuAddQOneSubGateHDUpdate,
                 tscales->updateHDSigmoidNuAddQOneSubGate);
    v_q_add(tbuffers->preComp3, tbuffers->preComp1, hiddenDims, hiddenState,
            tscales->pC3AddPC1, tscales->pC1AddPC3, tscales->hiddenStateOut);
  }
  return 0;
}
