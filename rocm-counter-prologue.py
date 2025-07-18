#!/usr/bin/env python3

import os
import subprocess
import sys

import re
import time

def expand_node_list(node_list):
    # Match the prefix and the bracketed numbers/ranges
    match = re.match(r'^([a-zA-Z]+)\[([\d,-]+)\]$', node_list)
    if not match:
        raise ValueError(f"Invalid node list format: {node_list}")

    prefix = match.group(1)  # e.g., "borg"
    number_ranges = match.group(2)  # e.g., "0008,0029" or "0008-0010,0029"

    nodes = []
    for part in number_ranges.split(','):
        if '-' in part:
            # Handle range (e.g., "0008-0010")
            start, end = part.split('-')
            start_num, end_num = int(start), int(end)
            # Enforce 4-digit padding for ranges
            padding = 5
            for i in range(start_num, end_num + 1):
                nodes.append(f"{prefix}{i:04d}")
        else:
            # Enforce 4-digit padding for single numbers
            nodes.append(f"{prefix}{int(part):04d}")

    return nodes


def main():
    # Check for NO_OLCF_HW_COUNTERS environment variable
    if os.getenv("NO_OLCF_HW_COUNTERS") is not None:
        print("NO_OLCF_HW_COUNTERS set, exiting", file=sys.stderr)
        sys.exit(0)

    # Should we start the daemon? Get the number of nodes and our node index.

    slurm_nnodes = os.getenv("SLURM_JOB_NUM_NODES")
    if slurm_nnodes is None:
        print("SLURM_JOB_NUM_NODES not set, exiting", file=sys.stderr)
        sys.exit(1)

    #if not a leadership job, exit
    if int(slurm_nnodes) < 1882:
        print("SLURM_JOB_NUM_NODES less than 1882, exiting", file=sys.stderr)
        sys.exit(0) # not an error

    slurm_nodename = os.getenv("SLURMD_NODENAME")
    if slurm_nodename is None:
        print("SLURMD_NODENAME not set, exiting", file=sys.stderr)
        sys.exit(2)

    slurm_nodelist = os.getenv("SLURM_NODELIST")
    if slurm_nodelist is None:
        print("SLURMD_NODELIST not set, exiting", file=sys.stderr)
        sys.exit(2)

    nodes = expand_node_list(slurm_nodelist)

    try:
        index = nodes.index(slurm_nodename)
        print(f"Found {slurm_nodename} at index {index}")
    except ValueError:
        print(f"{slurm_nodename} not found in the node list", file=sys.stderr)
        sys.exit(2)

    #slurm_nodeid = os.getenv("SLURM_NODEID")
    #if slurm_nodeid is None:
        #print("SLURM_NODEID not set, exiting", file=sys.stderr)
        #sys.exit(2)

    start_daemon = int(int(index) % ((int(slurm_nnodes) / 16)))
    if start_daemon > 2:
        print(f"start_daemon ({start_daemon}) > 2, exiting", file=sys.stderr)
        sys.exit(3)

    # The config files should be named "config-0", "config-1", or "config-3"
    #TODO use full Lustre path
    #inputfile = f"/lustre/orion/stf008/world-shared/frontier-counters-script/config-{start_daemon}"
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
        process = subprocess.Popen(["/lustre/orion/proj-shared/stf008/rocm-counter-daemon/rocm-counter-daemon", inputfile], start_new_session=True)
    except FileNotFoundError:
        print("rocm-counter-daemon binary not found", file=sys.stderr)
        sys.exit(5)
    except Exception as e:
        print(f"Failed to launch rocm-counter-daemon: {e}", file=sys.stderr)
        sys.exit(6)

        process.detach();

if __name__ == "__main__":
    main()
