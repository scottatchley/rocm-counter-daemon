# rocm-counter-daemon
Simple C++ daemon to set up hardware device counters, wait for SIGUSR1, read the counters, write them, then exit.

Initially, we are targeting the following counters:

SQ_INSTS_VALU_ADD_F64_sum
SQ_INSTS_VALU_MUL_F64_sum
SQ_INSTS_VALU_FMA_F64_sum
SQ_INSTS_VALU_MFMA_F64_sum
SQ_INSTS_VALU_ADD_F32_sum
SQ_INSTS_VALU_MUL_F32_sum
SQ_INSTS_VALU_FMA_F32_sum
SQ_INSTS_VALU_MFMA_F32_sum
SQ_INSTS_VALU_ADD_F16_sum
SQ_INSTS_VALU_MUL_F16_sum
SQ_INSTS_VALU_FMA_F16_sum
SQ_INSTS_VALU_MFMA_F16_sum
SQ_INSTS_VALU_MFMA_BF16_sum
TCC_EA_RDREQ_sum
TCC_EA_RDREQ_32B_sum

which are defined in the olcf_derived_metrics.yaml file.
