#!/usr/bin/env python3

import os
import signal
import sys

def main():
    # Get SLURM_JOBID environment variable
    slurm_jobid = os.getenv("SLURM_JOBID")
    if slurm_jobid is None:
        print("SLURM_JOBID not set, exiting", file=sys.stderr)
        sys.exit(1)

    # Construct PID file path
    pid_filename = f"/tmp/rocm-counter-daemon-pid-{slurm_jobid}"

    # Read PID from file. "with" will close the file automatically after the read().
    try:
        with open(pid_filename, "r") as pid_file:
            pid_str = pid_file.read().strip()
            try:
                pid = int(pid_str)
            except ValueError:
                print(f"Invalid PID in {pid_filename}: {pid_str}", file=sys.stderr)
                sys.exit(2)
    except FileNotFoundError:
        print(f"PID file {pid_filename} not found", file=sys.stderr)
        sys.exit(3)
    except Exception as e:
        print(f"Failed to read PID file {pid_filename}: {e}", file=sys.stderr)
        sys.exit(4)

    # Send SIGUSR1 to the process
    try:
        os.kill(pid, signal.SIGUSR1)
        print(f"Sent SIGUSR1 to process with PID {pid}")
    except ProcessLookupError:
        print(f"No process found with PID {pid}", file=sys.stderr)
        sys.exit(5)
    except PermissionError:
        print(f"Permission denied when sending SIGUSR1 to PID {pid}", file=sys.stderr)
        sys.exit(6)
    except Exception as e:
        print(f"Failed to send SIGUSR1 to PID {pid}: {e}", file=sys.stderr)
        sys.exit(7)

    # Unlink the PID file
    try:
        os.unlink(pid_filename)
        print(f"Deleted PID file {pid_filename}")
    except FileNotFoundError:
        print(f"PID file {pid_filename} already deleted", file=sys.stderr)
    except Exception as e:
        print(f"Failed to delete PID file {pid_filename}: {e}", file=sys.stderr)
        sys.exit(8)

if __name__ == "__main__":
    main()
