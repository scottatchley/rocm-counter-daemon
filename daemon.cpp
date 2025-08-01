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


#include <filesystem>
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

using namespace std;

#define SLEEP_SECS	(1)

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

namespace {

class device_collector {
public:
	device_collector(rocprofiler_agent_id_t agent);

	// Sample the counter values for a set of counters,
	// returns the records in the out parameter.
	void sample_counters(const std::vector<std::string> &counters,
				 std::vector<rocprofiler_record_counter_t> &out);

	// Decode the counter name of a record
	const std::string &decode_record_name(const rocprofiler_record_counter_t &rec) const;

	// Get the dimensions of a record (what CU/SE/etc the counter is for). High
	// cost operation; should be cached if possible.
	static std::unordered_map<std::string, size_t>
	get_record_dimensions(const rocprofiler_record_counter_t &rec);

	// Get the available agents on the system
	static std::vector<rocprofiler_agent_v0_t> get_agents();

	void stop() const { rocprofiler_stop_context(ctx_); }

private:
	rocprofiler_agent_id_t agent_ = {};
	rocprofiler_context_id_t ctx_ = {};
	rocprofiler_profile_config_id_t profile_ = {.handle = 0};

	std::map<std::vector<std::string>, rocprofiler_profile_config_id_t> cached_profiles_;
	std::map<uint64_t, uint64_t> profile_sizes_;
	mutable std::map<uint64_t, std::string> id_to_name_;

	void set_profile(rocprofiler_context_id_t ctx, rocprofiler_agent_set_profile_callback_t cb) const;

	static size_t get_counter_size(rocprofiler_counter_id_t counter);

	static std::unordered_map<std::string, rocprofiler_counter_id_t>
	get_supported_counters(rocprofiler_agent_id_t agent);

	static std::vector<rocprofiler_record_dimension_info_t>
	get_counter_dimensions(rocprofiler_counter_id_t counter);
};

device_collector::device_collector(rocprofiler_agent_id_t agent) : agent_(agent) {
	auto client_thread = rocprofiler_callback_thread_t{};
	ROCPROFILER_CALL(rocprofiler_create_context(&ctx_), "context creation failed");

	ROCPROFILER_CALL(rocprofiler_configure_device_counting_service(
				ctx_, rocprofiler_buffer_id_t{.handle = 0}, agent,
				[](rocprofiler_context_id_t context_id, rocprofiler_agent_id_t,
				rocprofiler_agent_set_profile_callback_t set_config, void *user_data) {
					if (user_data) {
						auto *collector = static_cast<device_collector *>(user_data);
						collector->set_profile(context_id, set_config);
					}
				},
				this),
			"Could not setup buffered service");
}

const std::string &
device_collector::decode_record_name(const rocprofiler_record_counter_t &rec) const {
	if (id_to_name_.empty()) {
		auto name_to_id = device_collector::get_supported_counters(agent_);
		for (const auto &[name, id] : name_to_id) {
			id_to_name_.emplace(id.handle, name);
		}
	}

	rocprofiler_counter_id_t counter_id = {.handle = 0};
	rocprofiler_query_record_counter_id(rec.id, &counter_id);
	return id_to_name_.at(counter_id.handle);
}

std::unordered_map<std::string, size_t>
device_collector::get_record_dimensions(const rocprofiler_record_counter_t &rec) {
	std::unordered_map<std::string, size_t> out;
	rocprofiler_counter_id_t counter_id = {.handle = 0};
	rocprofiler_query_record_counter_id(rec.id, &counter_id);
	auto dims = get_counter_dimensions(counter_id);

	for (auto &dim : dims) {
		size_t pos = 0;
		rocprofiler_query_record_dimension_position(rec.id, dim.id, &pos);
		out.emplace(dim.name, pos);
	}
	return out;
}

void device_collector::sample_counters(const std::vector<std::string> &counters,
					std::vector<rocprofiler_record_counter_t> &out) {
	auto profile_cached = cached_profiles_.find(counters);
	if (profile_cached == cached_profiles_.end()) {
		size_t expected_size = 0;
		rocprofiler_profile_config_id_t profile = {};
		std::vector<rocprofiler_counter_id_t> gpu_counters;
		auto roc_counters = get_supported_counters(agent_);
		for (const auto &counter : counters) {
			auto it = roc_counters.find(counter);
			if (it == roc_counters.end()) {
				std::cerr << "Counter " << counter << " not found\n";
				continue;
			}
			gpu_counters.push_back(it->second);
			expected_size += get_counter_size(it->second);
		}
		ROCPROFILER_CALL(rocprofiler_create_profile_config(agent_, gpu_counters.data(),
									gpu_counters.size(), &profile),
				 "Could not create profile");
		cached_profiles_.emplace(counters, profile);
		profile_sizes_.emplace(profile.handle, expected_size);
		profile_cached = cached_profiles_.find(counters);
	}

	out.resize(profile_sizes_.at(profile_cached->second.handle));
	profile_ = profile_cached->second;
	rocprofiler_start_context(ctx_);
	size_t out_size = out.size();
	rocprofiler_sample_device_counting_service(ctx_, {}, ROCPROFILER_COUNTER_FLAG_NONE, out.data(),
							&out_size);
	out.resize(out_size);
}

std::vector<rocprofiler_agent_v0_t> device_collector::get_agents() {
	std::vector<rocprofiler_agent_v0_t> agents;
	rocprofiler_query_available_agents_cb_t iterate_cb = [](rocprofiler_agent_version_t agents_ver,
								const void **agents_arr,
								size_t num_agents, void *udata) {
		if (agents_ver != ROCPROFILER_AGENT_INFO_VERSION_0)
			throw std::runtime_error{"unexpected rocprofiler agent version"};
		auto *agents_v = static_cast<std::vector<rocprofiler_agent_v0_t> *>(udata);
		for (size_t i = 0; i < num_agents; ++i) {
			const auto *rocp_agent = static_cast<const rocprofiler_agent_v0_t *>(agents_arr[i]);
			if (rocp_agent->type == ROCPROFILER_AGENT_TYPE_GPU)
				agents_v->emplace_back(*rocp_agent);
		}
		return ROCPROFILER_STATUS_SUCCESS;
	};

	ROCPROFILER_CALL(rocprofiler_query_available_agents(
				ROCPROFILER_AGENT_INFO_VERSION_0, iterate_cb, sizeof(rocprofiler_agent_t),
				const_cast<void *>(static_cast<const void *>(&agents))),
			"query available agents");
	return agents;
}

void device_collector::set_profile(rocprofiler_context_id_t ctx,
					rocprofiler_agent_set_profile_callback_t cb) const {
	if (profile_.handle != 0) {
		cb(ctx, profile_);
	}
}

size_t device_collector::get_counter_size(rocprofiler_counter_id_t counter) {
	size_t size = 1;
	rocprofiler_iterate_counter_dimensions(
		counter,
		[](rocprofiler_counter_id_t, const rocprofiler_record_dimension_info_t *dim_info,
			 size_t num_dims, void *user_data) {
			size_t *s = static_cast<size_t *>(user_data);
			for (size_t i = 0; i < num_dims; i++) {
				*s *= dim_info[i].instance_size;
			}
			return ROCPROFILER_STATUS_SUCCESS;
		},
		static_cast<void *>(&size));
	return size;
}

std::unordered_map<std::string, rocprofiler_counter_id_t>
device_collector::get_supported_counters(rocprofiler_agent_id_t agent) {
	std::unordered_map<std::string, rocprofiler_counter_id_t> out;
	std::vector<rocprofiler_counter_id_t> gpu_counters;

	ROCPROFILER_CALL(rocprofiler_iterate_agent_supported_counters(
				agent,
				[](rocprofiler_agent_id_t, rocprofiler_counter_id_t *counters,
				size_t num_counters, void *user_data) {
					std::vector<rocprofiler_counter_id_t> *vec =
						static_cast<std::vector<rocprofiler_counter_id_t> *>(user_data);
					for (size_t i = 0; i < num_counters; i++) {
						vec->push_back(counters[i]);
					}
				return ROCPROFILER_STATUS_SUCCESS;
				},
				static_cast<void *>(&gpu_counters)),
			"Could not fetch supported counters");
	for (auto &counter : gpu_counters) {
		rocprofiler_counter_info_v0_t version;
		ROCPROFILER_CALL(rocprofiler_query_counter_info(counter, ROCPROFILER_COUNTER_INFO_VERSION_0,
					static_cast<void *>(&version)),
				"Could not query info for counter");
		out.emplace(version.name, counter);
	}
	return out;
}

std::vector<rocprofiler_record_dimension_info_t>
device_collector::get_counter_dimensions(rocprofiler_counter_id_t counter) {
	std::vector<rocprofiler_record_dimension_info_t> dims;
	rocprofiler_available_dimensions_cb_t cb = [](rocprofiler_counter_id_t,
							const rocprofiler_record_dimension_info_t *dim_info,
							size_t num_dims, void *user_data) {
		std::vector<rocprofiler_record_dimension_info_t> *vec =
				static_cast<std::vector<rocprofiler_record_dimension_info_t> *>(user_data);
		for (size_t i = 0; i < num_dims; i++) {
			vec->push_back(dim_info[i]);
		}
		return ROCPROFILER_STATUS_SUCCESS;
	};
	ROCPROFILER_CALL(rocprofiler_iterate_counter_dimensions(counter, cb, &dims),
				"Could not iterate counter dimensions");
	return dims;
}

std::atomic<bool> done(false);
std::vector<std::shared_ptr<device_collector>> collectors = {};

} // namespace

int tool_init(rocprofiler_client_finalize_t fini_func, void *) {
	auto agents = device_collector::get_agents();
	if (agents.empty()) {
		std::cerr << "No agents found\n";
		return -1;
	}

	for (auto agent : agents) {
		collectors.push_back(std::make_shared<device_collector>(agent.id));
	}

	return 0;
}

void tool_fini(void *user_data) {
	for (auto c : collectors) {
		c->stop();
	}

	auto *output_stream = static_cast<std::ostream *>(user_data);
	*output_stream << std::flush;
	if (output_stream != &std::cout && output_stream != &std::cerr)
		delete output_stream;
}

extern "C" rocprofiler_tool_configure_result_t *rocprofiler_configure(uint32_t version,
									const char *runtime_version,
									uint32_t priority,
									rocprofiler_client_id_t *id) {
	id->name = "device-counters";

	uint32_t major = version / 10000;
	uint32_t minor = (version % 10000) / 100;
	uint32_t patch = version % 100;

	auto info = std::stringstream{};
	info << id->name << " (priority=" << priority << ") is using rocprofiler-sdk v" << major << "."
			 << minor << "." << patch << " (" << runtime_version << ")";

	std::cerr << info.str() << std::endl;

	std::ostream *output_stream = &std::cout;
	static auto cfg =
			rocprofiler_tool_configure_result_t{sizeof(rocprofiler_tool_configure_result_t), &tool_init,
								&tool_fini, static_cast<void *>(output_stream)};

	return &cfg;
}

void signal_handler(int signal) {
	//output_file << "Terminating collector - caught " << std::to_string(signal) << std::endl;
	if (signal == SIGTERM || signal == SIGINT || signal == SIGUSR1) {
		std::cerr << "Terminating collector\n";
		done.store(true);
	}
}

std::unordered_map<std::string, double>
process_records(const std::vector<rocprofiler_record_counter_t> &records,
		const std::shared_ptr<device_collector> &collector) {
	// Accumulate all records by name to display a single value.
	std::unordered_map<std::string, double> accumulated_values;
	for (const auto &record : records) {
		if (record.id) {
			auto name = collector->decode_record_name(record);
			accumulated_values[name] += record.counter_value;
		}
	}
	return accumulated_values;
}

void print_values(const std::unordered_map<std::string, double> &values) {
	// sort output by key
	std::vector<std::string> keys;
	for (const auto &pair : values) {
		keys.push_back(pair.first);
	}
	std::sort(keys.begin(), keys.end());

	std::cout << "- gpu:\n";
	for (const auto &key : keys) {
		std::cout << "	- " << key << ": " << values.at(key) << "\n";
	}
}

// Must be passed one argument, config-[01]
int main(int argc, char *argv[]) {
	// 1. Check for NO_OLCF_ROCPROF environment variable
	if (std::getenv("NO_OLCF_ROCPROF") != nullptr) {
		std::cerr << "NO_OLCF_ROCPROF set, exiting" << std::endl;
		return 0;
	}

	int num_devices = 0;
	auto status = hipGetDeviceCount(&num_devices);

	bool valid = true;
	// default sampling interval of 1 sec, can be overridden by command line arg
	int interval_seconds = SLEEP_SECS;

	std::cout << "Daemon starting" << std::endl;

	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <config-0|config-1|config-2>" << std::endl;
		return 1;
	}

	// Check if argument is "config-0" or "config-1"
	std::string config_name = argv[1];
	// get filename less basename and check
	filesystem::path p(config_name);
	if (p.filename() != "config-0" && p.filename() != "config-1" && p.filename() != "config-2") {
		std::cerr << "Invalid argument: must be 'config-0' or 'config-1' or 'config-2'" << std::endl;
		return 2;
	}

	// Open the config file
	std::ifstream config_file(config_name);
	if (!config_file.is_open()) {
		std::cerr << "Failed to open file: " << config_name << std::endl;
		return 3;
	}

	// Read counters into vector
	std::vector<std::string> counters;
	std::string line;
	while (std::getline(config_file, line)) {
		// Trim whitespace
		line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char c) { return !std::isspace(c); }));
		line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), line.end());
		// Skip empty lines
		if (!line.empty()) {
			counters.push_back(line);
		}
	}

	// Close the file
	config_file.close();

#if 0
	// Print counters for verification
	std::cout << "Counters read from " << config_name << ":" << std::endl;
	for (const auto& counter : counters) {
		std::cout << counter << std::endl;
	}
#endif

	// 2. Retrieve SLURM_JOBID environment variable
	const char* slurm_jobid = std::getenv("SLURM_JOBID");
	if (slurm_jobid == nullptr) {
		std::cerr << "SLURM_JOBID not set, exiting" << std::endl;
		return 4;
	}

	std::string dirname = "/lustre/orion/stf008/world-shared/frontier-counters/" + std::string(slurm_jobid);
	//std::string dirname = "counters/" + std::string(slurm_jobid);
	int ret = mkdir(dirname.c_str(), 0755);
	if (ret != 0 && errno != EEXIST) {
		std::cerr << "mkdir(${dirname}) failed " << strerror(errno) << ", exiting" << std::endl;
		return 5;
	}

	// 3. Create string with SLURM_JOBID and hostname
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != 0) {
		std::cerr << "Failed to get hostname: " << std::strerror(errno) << std::endl;
		return 6;
	}
	std::string filename = std::string(slurm_jobid) + "-" + std::string(hostname);

	// 4. Create file with read-write permissions
	output_file.open(dirname + "/" + filename, std::ios::out);
	if (!output_file.is_open()) {
		std::cerr << "Failed to open file " << dirname << "/" << filename << ": " << std::strerror(errno) << std::endl;
		return 7;
	}

	std::cerr << "Opened file " << dirname << "/" << filename  << std::endl;
	//output_file << "Opened file " << dirname << "/" << filename  << std::endl;

	// Get our PID and write to a file in tmp
	pid_t pid = getpid();
	std::string pid_filename = std::string("/tmp/rocm-counter-daemon-pid-") + std::string(slurm_jobid);

	std::ofstream pid_file;
	pid_file.open(pid_filename, std::ios::out);
	if (!pid_file.is_open()) {
		std::cerr << "Failed to open file " << pid_filename << ": " << std::strerror(errno) << std::endl;
		//output_file << "Failed to open file " << pid_filename << ": " << std::strerror(errno) << std::endl;
		return 8;
	}

	pid_file << std::to_string(pid)<< std::endl;
	pid_file.close();

	//output_file << "Wrote pid file " << pid_filename << std::endl;

	std::vector<rocprofiler_record_counter_t> records;
	std::vector<double> grbm_counts;

	//output_file << "Starting collector " << std::endl;

	//std::cout << "start:\n";
	for (auto collector : collectors) {
		collector->sample_counters(counters, records);
		auto values = process_records(records, collector);
		//print_values(values);
		grbm_counts.push_back(values["GRBM_COUNT"]);
	}

	//output_file << "Entering loop " << std::endl;

	int seconds = 0;

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGUSR1, signal_handler);

	while (!done) {
		std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
		for (int i = 0; i < grbm_counts.size(); i++) {
			auto collector = collectors[i];
			collector->sample_counters(counters, records);
			auto values = process_records(records, collector);

			// Make sure GRBM_COUNT is always increasing. If it's not,
			// another profiling process (eg. rocprof) was likely invoked during the
			// interval and the numbers are no longer reliable.
			auto previous = grbm_counts[i];
			grbm_counts[i] = values["GRBM_COUNT"];
			if (grbm_counts[i] < previous) {
				std::cerr << "Invalid session: " << previous << " " << grbm_counts[i] << "\n";
				//output_file << "Invalid session: " << previous << " " << grbm_counts[i] << "\n";

				valid = false;
			}
			//print_values(values);
		}
		//output_file << "Seconds: " << std::to_string(seconds++) << std::endl;
	}

	//output_file << "Exiting loop" << std::endl;

	//std::cout << "end:\n";
	for (auto collector : collectors) {
		collector->sample_counters(counters, records);
		auto values = process_records(records, collector);
		if (valid) {
			print_values(values);
			for (const auto &pair : values) {
				output_file << pair.first << ": " << pair.second << std::endl;
			}
		} else {
			output_file << "Invalid session - ignoring counters" << std::endl;
		}
		output_file.flush();
		output_file.close();
	}
	std::cout << "valid: " << valid << std::endl;

	if(valid)
		return 0;
	else
		return -1;
}
