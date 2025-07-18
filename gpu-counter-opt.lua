-- ===========================================================================
-- SPANK plugin to set GPU counters [on|off]
-- Only settable at the job level
-- Currently specific for AMD MI250x on HPE Bard Peak
-- ===========================================================================
-- Questions to Matt Ezell <ezellma@ornl.gov>

log = require("posix.syslog")

logname       = "gpu-counters"
counters_default = 1

requested_onoff = nil

gpu_counters_opt = {
    name    = "gpu-counters",
    usage   = "Set the GPU counter collection On [1] or Off [0]. Default is " .. counters_default,
    arginfo = "on_off",
    has_arg = 1,
    cb      = "gpu_counters_opt_handler",
    }

-- The lua plugin registers all options in this specially-named table
spank_options = { gpu_counters_opt, }

-- Validate a user-supplied power value
-- Returns a valid value and a rc (if the supplied value was valid or not)
function validate_counters_value(on_off)
    -- No value supplied is OK
    if on_off == nil then
        return counters_default, SPANK.SUCCESS
    end
    local parsed = tonumber(on_off)
    if parsed == nil then
        SPANK.log_error("%s: requested GPU counters value '%s' is not a valid integer", logname, on_off)
        return counters_default, SPANK.FAILURE
    elseif parsed < 0 then
        SPANK.log_error("%s: requested GPU counters value '%s' is less than 0", logname, parsed)
        return counters_default, SPANK.FAILURE
    elseif parsed > 1 then
	SPANK.log_error("%s: requested GPU counters value '%s' is greater than 1", logname, parsed)
        return power_max, SPANK.FAILURE
    else
        return parsed, SPANK.SUCCESS
    end
end

-- This is called as slurm parses the --gpu-counters option
function gpu_couners_opt_handler(val, optarg, isremote)
    -- Always succeed
    -- Delete this function if it is not required
    return 0
end

-- Called in prolog - sets the requested (or default) gpu power cap
function slurm_spank_job_prolog(spank)
    -- Nothing to do - simply set env variables for the python script
    return 0
end

-- Called at epilog time - resets back to defaults
function slurm_spank_job_epilog(spank)
    -- Nothing to do
    return 0
end

-- This gets called after the options have been parsed
-- This sets values that job_submit.lua will save into admin_comment
function slurm_spank_init_post_opt(spank)
    -- requested_onoff gets set by the option callback if the user set it.
    -- If the user didn't set it, store the default power cap.
    -- NOTE: it's possible that changing the default might cause the
    -- stored value (saved at submit time) to differ from the used value
    -- (determined at runtime).
    -- There doesn't seem to be a way for root to "set" a user option.
    if requested_onoff == nil then
        spank:job_control_setenv ("GPU_COUNTERS", counters_default, 1)
        spank:job_control_setenv ("GPU_COUNTERS_SRC", "default", 1)
    else
        spank:job_control_setenv ("GPU_COUNTERS", requested_onoff, 1)
        spank:job_control_setenv ("GPU_COUNTERS_SRC", "user", 1)
    end
end
