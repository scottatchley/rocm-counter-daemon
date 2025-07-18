# rocm-counter-daemon
Simple C++ daemon to set up hardware device counters, wait for SIGUSR1, read the counters, write them, then exit.

Initially, we are targeting the following counters:

SQ_INSTS_VALU_ADD_F64\
SQ_INSTS_VALU_MUL_F64\
SQ_INSTS_VALU_FMA_F64\
SQ_INSTS_VALU_MFMA_F6m\
SQ_INSTS_VALU_ADD_F32\
SQ_INSTS_VALU_MUL_F32\
SQ_INSTS_VALU_FMA_F32\
SQ_INSTS_VALU_MFMA_F32\
SQ_INSTS_VALU_ADD_F16\
SQ_INSTS_VALU_MUL_F16\
SQ_INSTS_VALU_FMA_F16\
SQ_INSTS_VALU_MFMA_F16\
SQ_INSTS_VALU_MFMA_BF16\
TCC_EA_RDREQ_sum\
TCC_EA_RDREQ_32B_sum

which are defined in the olcf_derived_metrics.yaml file (the TCC_EA_RDREQ\*\_sum metrics are defined in the stock rocprof files).

This repo has two Python scripts, rocm-counter-prologue and rocm-counter-epilogue, and a binary, rocm-counter-daemon.

The rocm-counter-prologue script is meant to be invoked within the Slurm job prologue script. The script will determine if it should launch the counter collection daemon or not.If the environment variable, NO_OLCF_HW_COUNTERS, is defined, it will do nothing and exit.

If the prologue script starts the daemon, the daemon will do the following:

1. Get its job ID from the SLURM_JOBID environment variable.
2. Create a string, dirname, with the output directory including the job ID.
3. Create a string, filename, for the output file using both the job ID and hostname.
4. Create the file using dirname + filename.
5. Set up a signal handler to catch SIGUSR1, SIGTERM, and SIGINT.
7. Polls for new counters once per second in a loop waiting on "done" to be set to true
8. Once the signal is caught, set "done" to true
9. Exit the loop and write the counters to the pre-created file

At the job end, the epilogue script will determine if there is a daemon running. If there is, it will send SIGUSR1.

Currently, the daemon aggreagtes the counters into a single value and does not use the dreived counters in olcf\_derived\_metrics.yaml.
