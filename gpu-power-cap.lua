-- ===========================================================================
-- SPANK plugin to set GPU power cpas
-- Only settable at the job level, not for each srun because GPUs can be
-- shared among steps.
-- Currently specific for AMD MI250x on HPE Bard Peak
-- ===========================================================================
-- Questions to Matt Ezell <ezellma@ornl.gov>

log = require("posix.syslog")

logname       = "gpu-power-cap"
power_min     = 100
power_default = 560
power_max     = 600

requested_power = nil

gpu_power_cap_opt = {
    name    = "gpu-power-cap",
    usage   = "Set the GPU power cap in watts. Default is " .. power_default,
    arginfo = "watts",
    has_arg = 1,
    cb      = "gpu_power_cap_opt_handler",
    }

-- The lua plugin registers all options in this specially-named table
spank_options = { gpu_power_cap_opt, }

-- Validate a user-supplied power value
-- Returns a valid value and a rc (if the supplied value was valid or not)
function validate_power_value(watts)
    -- No value supplied is OK
    if watts == nil then
        return power_default, SPANK.SUCCESS
    end
    local parsed = tonumber(watts)
    if parsed == nil then
        SPANK.log_error("%s: requested GPU power cap value '%s' is not a valid integer", logname, watts)
        return power_default, SPANK.FAILURE
    elseif parsed < power_min then
        SPANK.log_error("%s: requested GPU power cap value '%s' is below minumum '%s'", logname, parsed, power_min)
        return power_min, SPANK.FAILURE
    elseif parsed > power_max then
	SPANK.log_error("%s: requested GPU power cap value '%s' is above maximum '%s'", logname, parsed, power_max)
        return power_max, SPANK.FAILURE
    else
        return parsed, SPANK.SUCCESS
    end
end

-- This is called as slurm parses the --gpu-power-cap option
-- If validate_power_value returns failure, it will abort the job
function gpu_power_cap_opt_handler(val, optarg, isremote)
    if isremote then
        return SPANK.SUCCESS
    end
    requested_power, rc = validate_power_value(optarg)
    return rc
end

-- Determine the current power cap of a gpu
-- NOTE: not currently used
function read_gpu_power_cap(card)
    local val = 0
    local f = io.open("/sys/class/drm/card" .. card .. "/device/hwmon/hwmon" .. card .. "/power1_cap", "r")
    if f then
        val = tonumber(f:read()) / 1000000
	f:close()
    end
    return val
end

-- Set the power cap of a GPU
function set_gpu_power_cap(card, watts)
    local f = io.open("/sys/class/drm/card" .. card .. "/device/hwmon/hwmon" .. card .. "/power1_cap", "w")
    if f then
        f:write(tostring(watts * 1000000))
	f:close()
    end
end

-- Called in prolog - sets the requested (or default) gpu power cap
function slurm_spank_job_prolog(spank)
    local power, rc = validate_power_value(spank:getopt(spank_options[1]))
    --local power, rc = validate_power_value(spank:job_control_getenv("GPU_POWER_CAP"))
    if power == nil then
        power = power_default
    end
    local jobid = spank:get_item ("S_JOB_ID")
    log.syslog(log.LOG_INFO, string.format("%s: Setting job %s power cap to %s", logname, jobid, power))
    set_gpu_power_cap(0, power)
    set_gpu_power_cap(2, power)
    set_gpu_power_cap(4, power)
    set_gpu_power_cap(6, power)

    return 0
end

-- Called at epilog time - resets back to defaults
function slurm_spank_job_epilog(spank)
    set_gpu_power_cap(0, power_default)
    set_gpu_power_cap(2, power_default)
    set_gpu_power_cap(4, power_default)
    set_gpu_power_cap(6, power_default)
    return 0
end

-- This gets called after the options have been parsed
-- This sets values that job_submit.lua will save into admin_comment
function slurm_spank_init_post_opt(spank)
    -- requested_power gets set by the option callback if the user set it.
    -- If the user didn't set it, store the default power cap.
    -- NOTE: it's possible that changing the default might cause the
    -- stored value (saved at submit time) to differ from the used value
    -- (determined at runtime).
    -- There doesn't seem to be a way for root to "set" a user option.
    if requested_power == nil then
        spank:job_control_setenv ("GPU_POWER_CAP", power_default, 1)
        spank:job_control_setenv ("GPU_POWER_CAP_SRC", "default", 1)
    else
        spank:job_control_setenv ("GPU_POWER_CAP", requested_power, 1)
        spank:job_control_setenv ("GPU_POWER_CAP_SRC", "user", 1)
    end
end
