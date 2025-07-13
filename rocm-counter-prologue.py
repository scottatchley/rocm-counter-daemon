#!/usr/bin/env python3

import os
import subprocess
import sys

def main():
    # Check for NO_OLCF_HW_COUNTERS environment variable
    if os.getenv("NO_OLCF_HW_COUNTERS") is not None:
        print("NO_OLCF_HW_COUNTERS set, exiting", file=sys.stderr)
        sys.exit(0)

    # Should we start the daemon? Get the number of nodes from SLURM_NNODES and our node index from SLURM_NODEID.

    slurm_nnodes = os.getenv("SLURM_NNODES")
    if slurm_nnodes is None:
        print("SLURM_NNODES not set, exiting", file=sys.stderr)
        sys.exit(1)

    # if not a leadership job, exit
    if int(slurm_nnodes) < 1882:
        print("SLURM_NNODES less than 1882, exiting", file=sys.stderr)
        sys.exit(0) # not an error

    slurm_nodeid = os.getenv("SLURM_NODEID")
    if slurm_nodeid is None:
        print("SLURM_NODEID not set, exiting", file=sys.stderr)
        sys.exit(2)

    start_daemon = int(int(slurm_nodeid) % ((int(slurm_nnodes) / 16)))
    if start_daemon > 2:
        print(f"start_daemon ({start_daemon}) > 2, exiting", file=sys.stderr)
        sys.exit(3)

    #TODO use full Lustre path
    inputfile = f"config-{start_daemon}"

    # Get SLURM_JOBID environment variable
    slurm_jobid = os.getenv("SLURM_JOBID")
    if slurm_jobid is None:
        print("SLURM_JOBID not set, exiting", file=sys.stderr)
        sys.exit(4)

    # Launch rocm-counter-daemon
    try:
        # for debugging, do not redirect stdout/stderr to DEVNULL
        #process = subprocess.Popen(["./rocm-counter-daemon", inputfile], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        process = subprocess.Popen(["./rocm-counter-daemon", inputfile])
    except FileNotFoundError:
        print("rocm-counter-daemon binary not found", file=sys.stderr)
        sys.exit(5)
    except Exception as e:
        print(f"Failed to launch rocm-counter-daemon: {e}", file=sys.stderr)
        sys.exit(6)

    # Write PID to file in /tmp
    pid_filename = f"/tmp/rocm-counter-daemon-pid-{slurm_jobid}"
    try:
        with open(pid_filename, "w") as pid_file:
            pid_file.write(str(process.pid))
        # Set file permissions to rw------- (600)
        os.chmod(pid_filename, 0o600)
    except Exception as e:
        print(f"Failed to write PID to {pid_filename}: {e}", file=sys.stderr)
        process.terminate()
        sys.exit(7)

    print(f"Launched rocm-counter-daemon with PID {process.pid}, written to {pid_filename}")

if __name__ == "__main__":
    main()
