# rocm-counter-daemon
Simple C++ daemon to set up hardware device counters, wait for SIGUSR1, read the counters, write them, then exit.

Initially, we are targeting the following counters:

SQ_INSTS_VALU_ADD_F64_sum\
SQ_INSTS_VALU_MUL_F64_sum\
SQ_INSTS_VALU_FMA_F64_sum\
SQ_INSTS_VALU_MFMA_F64_sum\
SQ_INSTS_VALU_ADD_F32_sum\
SQ_INSTS_VALU_MUL_F32_sum\
SQ_INSTS_VALU_FMA_F32_sum\
SQ_INSTS_VALU_MFMA_F32_sum\
SQ_INSTS_VALU_ADD_F16_sum\
SQ_INSTS_VALU_MUL_F16_sum\
SQ_INSTS_VALU_FMA_F16_sum\
SQ_INSTS_VALU_MFMA_F16_sum\
SQ_INSTS_VALU_MFMA_BF16_sum\
TCC_EA_RDREQ_sum\
TCC_EA_RDREQ_32B_sum\

which are defined in the olcf_derived_metrics.yaml file (the TCC_EA_RDREQ metrics are defined in the stock rocprof files).

This repo has two Python scripts, rocm-counter-prologue and rocm-counter-epilogue, and a binary, rocm-counter-daemon.

The rocm-counter-prologue script is meant to be invoked within the Slurm job prologue script. The script will determine if it should launch the counter collection daemon or not.If the environment variable, NO_OLCF_HW_COUNTERS, is defined, it will do nothing and exit.

If the prologue script starts the daemon, the daemon will do the following:

1. Get its job ID from the SLURM_JOBID environment variable.
2. Create a string, dirname, with the output directory including the job ID.
3. Create a string, filename, for the output file using both the job ID and hostname.
4. Create the file using dirname + filename.
5. Set up a signal handler to catch SIGUSR1, SIGTERM, and SIGINT.
6. Daemonize itself
   - Call fork()
     - If the parent, exit (the python script completes and the job starts)
   - Call setsid()
   - Redirect STDOUT and STDERR to /dev/null
   - Sets its umask to 0
   - Changes its working directory to "/"
7. Blocks on sigwait

At this point, the counters should be started and the daemon is waiting on SIGUSR1 (as well as SIGTERM and SIGINT).

At the job end, the epilogue script will determine if there is a daemon running. If there is, it will send SIGUSR1.

Upon receipt of SIGUSR1, the daemon will unblock, read the counters, write the counters to the output file, cleanup, and exit.

