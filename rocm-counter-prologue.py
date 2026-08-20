#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys

import re
import time

cluster = os.getenv("SLURM_CLUSTER_NAME", default=None)
if cluster is None:
    print("$SLURM_CLUSTER_NAME is unset", file=sys.stderr)
    sys.exit(1)

def expand_node_list(node_list):
    if not re.match(r"^[a-zA-Z0-9\[\-,\]]+$", node_list):
        print("Nodelist had suspicious characters, exiting", file=sys.stderr)
        sys.exit(1)
    result = subprocess.run(["/usr/bin/scontrol", "show", "hostnames", node_list], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, check=True)
    return result.stdout.splitlines()

def main():
    default_config_dir = "/etc/rocm-counter-daemon"

    parser = argparse.ArgumentParser(description="ROCm counter daemon prologue script")
    parser.add_argument(
        "--config-dir",
        dest="config_dir",
        default=default_config_dir,
        help=f"Directory containing the config-0/config-1/config-2/config-3 counter files (default: {default_config_dir})",
    )
    args = parser.parse_args()

    if not os.path.isdir(args.config_dir):
        print(f"config_dir '{args.config_dir}' is not a directory", file=sys.stderr)
        sys.exit(7)

    # Check for SPANK_GPU_COUNTERS environment variable
    counters_onoff = os.getenv("SPANK_GPU_COUNTERS")
    if counters_onoff is None:
            print("SPANK_GPU_COUNTERS is not set", file=sys.stderr)
    else:
        if int(counters_onoff) == 0:
            print("SPANK_GPU_COUNTERS=0, exiting", file=sys.stderr)
            sys.exit(0)

    # Should we start the daemon? Get the number of nodes and our node index.

    slurm_nnodes = os.getenv("SLURM_JOB_NUM_NODES")
    if slurm_nnodes is None:
        print("SLURM_JOB_NUM_NODES not set, exiting", file=sys.stderr)
        sys.exit(1)

    if cluster == "frontier":
        if int(slurm_nnodes) < 500:
            print("SLURM_JOB_NUM_NODES less than 500, exiting", file=sys.stderr)
            sys.exit(0) # not an error

    slurm_nodename = os.getenv("SLURMD_NODENAME")
    if slurm_nodename is None:
        print("SLURMD_NODENAME not set, exiting", file=sys.stderr)
        sys.exit(2)

    slurm_nodelist = os.getenv("SLURM_NODELIST")
    if slurm_nodelist is None:
        print("SLURM_NODELIST not set, exiting", file=sys.stderr)
        sys.exit(2)

    nodes = expand_node_list(slurm_nodelist)
    print("Node list vector:", nodes)

    try:
        index = nodes.index(slurm_nodename)
        print(f"Found {slurm_nodename} at index {index}")
    except ValueError:
        print(f"{slurm_nodename} not found in the node list", file=sys.stderr)
        sys.exit(2)

    if int(slurm_nnodes) < 48:
        base = 1
    else:
        base = 16

    start_daemon = int(int(index) % ((int(slurm_nnodes) / base)))
    if start_daemon > 3:
        print(f"start_daemon ({start_daemon}) > 3, exiting", file=sys.stderr)
        sys.exit(3)

    # The config files should be named "config-0", "config-1", "config-2", or "config-3"
    inputfile = os.path.join(args.config_dir, f"config-{start_daemon}")

    # Get SLURM_JOBID environment variable
    slurm_jobid = os.getenv("SLURM_JOBID")
    if slurm_jobid is None:
        print("SLURM_JOBID not set, exiting", file=sys.stderr)
        sys.exit(4)

    # Launch rocm-counter-daemon
    try:
        # for debugging, do not redirect stdout/stderr to DEVNULL
        #process = subprocess.Popen(["./rocm-counter-daemon", inputfile], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        process = subprocess.Popen(
                [
                    "/usr/bin/rocm-counter-daemon",
                    "--dirname=/lustre/orion/sysinfo/" + cluster,
                    inputfile
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True
        )
    except FileNotFoundError:
        print("rocm-counter-daemon binary not found", file=sys.stderr)
        sys.exit(5)
    except Exception as e:
        print(f"Failed to launch rocm-counter-daemon: {e}", file=sys.stderr)
        sys.exit(6)

        process.detach();

if __name__ == "__main__":
    main()
