#include <rocprofiler-sdk/buffer.h>
#include <rocprofiler-sdk/context.h>
#include <rocprofiler-sdk/fwd.h>
#include <rocprofiler-sdk/registration.h>
#include <rocprofiler-sdk/rocprofiler.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>


#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

// Global file stream for writing to the output file
static std::ofstream output_file;

#define ROCPROFILER_CALL(result, msg)                                                              \
  {                                                                                                \
    rocprofiler_status_t CHECKSTATUS = result;                                                     \
    if (CHECKSTATUS != ROCPROFILER_STATUS_SUCCESS) {                                               \
      std::string status_msg = rocprofiler_get_status_string(CHECKSTATUS);                         \
      std::cerr << "[" #result "][" << __FILE__ << ":" << __LINE__ << "] " << msg                  \
                << " failed with error code " << CHECKSTATUS << ": " << status_msg << std::endl;   \
      std::stringstream errmsg{};                                                                  \
      errmsg << "[" #result "][" << __FILE__ << ":" << __LINE__ << "] " << msg " failure ("        \
             << status_msg << ")";                                                                 \
      throw std::runtime_error(errmsg.str());                                                      \
    }                                                                                              \
  }

// Daemonize the process
void daemonize() {
	pid_t pid = fork();
	if (pid < 0) {
		std::cerr << "Fork failed: " << std::strerror(errno) << std::endl;
		exit(1);
	}
	if (pid > 0) {
		exit(0); // Parent exits
	}

	// TODO - set up ROCm context and start counters

	// The following is needed to prevent the Slurm prolog script from killing the daemon

	// Create new session
	if (setsid() < 0) {
		std::cerr << "Failed to create new session: " << std::strerror(errno) << std::endl;
		exit(1);
	}

#ifdef NDEBUG
	// Redirect standard files to /dev/null
	int fd = open("/dev/null", O_RDWR);
	if (fd != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > 2) close(fd);
	} else {
		std::cerr << "Failed to open /dev/null: " << std::strerror(errno) << std::endl;
		exit(1);
	}

	// Set umask
	umask(0);

	// Change working directory to root
	if (chdir("/") < 0) {
		std::cerr << "Failed to change directory to /: " << std::strerror(errno) << std::endl;
		exit(1);
	}
#endif
}

int main() {
	// 1. Check for NO_OLCF_ROCPROF environment variable
	if (std::getenv("NO_OLCF_ROCPROF") != nullptr) {
		std::cerr << "NO_OLCF_ROCPROF set, exiting" << std::endl;
		return 0;
	}

	std::cout << "Daemon starting" << std::endl;

	// 2. Retrieve SLURM_JOBID environment variable
	const char* slurm_jobid = std::getenv("SLURM_JOBID");
	if (slurm_jobid == nullptr) {
		std::cerr << "SLURM_JOBID not set, exiting" << std::endl;
		return 1;
	}

	//std::string dirname = "/lustre/orion/stf008/world-shared/frontier-counters/" + std::string(slurm_jobid) + "/";
	std::string dirname = "counters/" + std::string(slurm_jobid);
	mkdir(dirname.c_str(), 0755);

	// 3. Create string with SLURM_JOBID and hostname
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != 0) {
		std::cerr << "Failed to get hostname: " << std::strerror(errno) << std::endl;
		return 1;
	}
	std::string filename = std::string(slurm_jobid) + "-" + std::string(hostname);

	// 4. Create file with read-write permissions
	output_file.open(dirname + "/" + filename, std::ios::out);
	if (!output_file.is_open()) {
		std::cerr << "Failed to open file " << dirname << "/" << filename << ": " << std::strerror(errno) << std::endl;
		return 1;
	}

#if 0
	// Set read-write permissions for owner (rw-------)
	if (chmod(("/var/log/" + filename).c_str(), S_IRUSR | S_IWUSR) != 0) {
		std::cerr << "Failed to set permissions on /var/log/" << filename << ": " << std::strerror(errno) << std::endl;
		output_file.close();
		return 1;
	}
#endif

	// Block waiting for SIGUSR1, SIGTERM, or SIGINT
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT);
	sigprocmask(SIG_BLOCK, &set, nullptr);

	// Daemonize the process
	daemonize();

	// Get our PID and write to a file in tmp
	pid_t pid = getpid();
	std::string pid_filename = std::string("/tmp/rocm-counter-daemon-pid-") + std::string(slurm_jobid);

	std::ofstream pid_file;
	pid_file.open(pid_filename, std::ios::out);
	if (!pid_file.is_open()) {
		std::cerr << "Failed to open file " << pid_filename << ": " << std::strerror(errno) << std::endl;
		return 1;
	}

	pid_file << std::to_string(pid)<< std::endl;
	pid_file.close();

	// Wait for signal
	int signum;
	if (sigwait(&set, &signum) != 0) {
		std::cerr << "Failed to wait for signal: " << std::strerror(errno) << std::endl;
		output_file.close();
		return 1;
	}

	if (signum == SIGUSR1) {
		// TODO
		// Read counters and write to output_file
		output_file << "Hello World!" << std::endl;
		output_file.flush();
	}
	output_file.close();

	// Signal handler will handle writing and exiting
	return 0;
}
