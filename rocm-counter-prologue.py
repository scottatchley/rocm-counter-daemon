#!/usr/bin/env python3

import os
import subprocess
import sys

import re
import time

prefix = "";

def expand_node_list(node_list):
    # Match either prefix[digits,digits] or prefixdigits
    # - Group 1: prefix (e.g., "borg")
    # - Group 2: bracketed numbers (e.g., "0008,0029" or "0008-0010,0029") or None
    # - Group 3: single number (e.g., "0009") or None
    match = re.match(r'^([a-zA-Z]+)(?:\[([\d,-]+)\]|(\d+))?$', node_list)
    if not match:
        raise ValueError(f"Invalid node list format: {node_list}")

    prefix = match.group(1)  # e.g., "borg"
    number_ranges = match.group(2)  # Bracketed numbers or None
    single_number = match.group(3)  # Single number or None

    if prefix == "borg":
        padding = 3
    elif prefix == "frontier":
        padding = 5
    else:
        padding = 4

    nodes = []

    if number_ranges:
        # Handle bracketed format (e.g., "borg[0008,0029]" or "borg[0008-0010,0029]")
        for part in number_ranges.split(','):
            if '-' in part:
                # Handle range (e.g., "0008-0010")
                start, end = part.split('-')
                start_num, end_num = int(start), int(end)
                for i in range(start_num, end_num + 1):
                    nodes.append(f"{prefix}{i:0{padding}d}")
            else:
                # Enforce padding for single numbers in brackets
                nodes.append(f"{prefix}{int(part):0{padding}d}")
    elif single_number:
        # Handle single node (e.g., "borg0009")
        nodes.append(f"{prefix}{int(single_number):0{padding}}")

    return nodes


def main():

    #sys.exit(0)

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

    if prefix == "frontier":
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
    if start_daemon > 2:
        print(f"start_daemon ({start_daemon}) > 2, exiting", file=sys.stderr)
        sys.exit(3)

    # The config files should be named "config-0", "config-1", or "config-2"
    inputfile = f"/lustre/orion/stf008/world-shared/frontier-counters-script/config-{start_daemon}"

    # Get SLURM_JOBID environment variable
    slurm_jobid = os.getenv("SLURM_JOBID")
    if slurm_jobid is None:
        print("SLURM_JOBID not set, exiting", file=sys.stderr)
        sys.exit(4)

    # Launch rocm-counter-daemon
    try:
        # for debugging, do not redirect stdout/stderr to DEVNULL
        #process = subprocess.Popen(["./rocm-counter-daemon", inputfile], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        process = subprocess.Popen(["/lustre/orion/proj-shared/stf008/rocm-counter-daemon/rocm-counter-daemon", inputfile], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    except FileNotFoundError:
        print("rocm-counter-daemon binary not found", file=sys.stderr)
        sys.exit(5)
    except Exception as e:
        print(f"Failed to launch rocm-counter-daemon: {e}", file=sys.stderr)
        sys.exit(6)

        process.detach();

if __name__ == "__main__":
    main()
