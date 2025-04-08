-- Snapmaker Image Refresher for OBS
-- Automatically refreshes an image source when the file changes
-- Author: David Cabell (converted from Python script)
 
obs           = obslua

-- Default configuration (will be updated by user settings)
source_name   = "SM Image"
image_path = "/Users/davec/Nextcloud/Work/Snapmaker/SnapmakerLightBurnHost/build/latest.jpg"
check_interval = 500  -- milliseconds (check more frequently - 500ms)
last_modified_time = 0
last_file_error_reported = 0
log_file_path = "/Users/davec/Nextcloud/Work/Snapmaker/SnapmakerLightBurnHost/build/snapmaker_monitor.log"
startup_indicator_path = "/Users/davec/Nextcloud/Work/Snapmaker/SnapmakerLightBurnHost/build/snapmaker_running.txt"

-- Clear log at startup
local log_file = io.open(log_file_path, "w")
if log_file then
    log_file:write("Snapmaker OBS Monitor started at " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
    log_file:close()
end


-- Description displayed in the Scripts window
function script_description()
    print("\n==== SNAPMAKER IMAGE MONITOR LOADED ====")
    obs.script_log(obs.LOG_INFO, "SNAPMAKER MONITOR: Script loaded and running!")
    return "Monitors and refreshes a Snapmaker camera image when it changes.\n\nBy David Cabell"
end

-- Define the properties for the script settings UI
function script_properties()
    local props = obs.obs_properties_create() 
    
    -- Path to the image file
    obs.obs_properties_add_path(props, "image_path", "Image Path", obs.OBS_PATH_FILE, 
                               "Image Files (*.jpg *.jpeg *.png)", nil)
    
    -- Source name selector
    local p = obs.obs_properties_add_list(props, "source_name", "Image Source", 
                                        obs.OBS_COMBO_TYPE_EDITABLE, 
                                        obs.OBS_COMBO_FORMAT_STRING)
    
    -- Populate with available image sources
    local sources = obs.obs_enum_sources()
    if sources ~= nil then
        for _, source in ipairs(sources) do
            local source_id = obs.obs_source_get_unversioned_id(source)
            if source_id == "image_source" then
                local name = obs.obs_source_get_name(source)
                obs.obs_property_list_add_string(p, name, name)
            end
        end
        obs.source_list_release(sources)
    end
    
    -- Interval for checking image changes
    obs.obs_properties_add_int_slider(props, "check_interval", "Check Interval (ms)", 
                                     100, 5000, 100)
    
    -- Log file location
    obs.obs_properties_add_path(props, "log_file", "Log File (Optional)", 
                               obs.OBS_PATH_FILE_SAVE, "Text Files (*.txt *.log)", nil)
    
    return props
end

-- Log a message to both OBS log and optionally to a file
function log_message(message, level)
    local level = level or obs.LOG_INFO
    
    -- Log to OBS
    obs.script_log(level, message)
    print(message)
    
    -- Log to file if path is set
    if log_file_path ~= nil and log_file_path ~= "" then
        local file = io.open(log_file_path, "a")
        if file then
            local timestamp = os.date("%Y-%m-%d %H:%M:%S")
            file:write(string.format("[%s] %s\n", timestamp, message))
            file:close()
        end
    end
end

-- Called when settings are updated
function script_update(settings)
    image_path = obs.obs_data_get_string(settings, "image_path")
    source_name = obs.obs_data_get_string(settings, "source_name")
    check_interval = obs.obs_data_get_int(settings, "check_interval")
    log_file_path = obs.obs_data_get_string(settings, "log_file")
    
    -- Log settings update
    log_message("==== SNAPMAKER MONITOR: SETTINGS UPDATED ====")
    log_message("Image path: " .. tostring(image_path))
    log_message("Source name: " .. tostring(source_name))
    log_message("Check interval: " .. tostring(check_interval) .. "ms")
    log_message("Log file: " .. tostring(log_file_path))
    
    -- Initialize file tracking
    local success, result = pcall(function()
        local file = io.open(image_path, "rb")
        if file then
            -- Get the file size
            last_size = file:seek("end")
            file:seek("set")
            
            -- Generate a simple hash of file content (first 1024 bytes)
            local content = file:read(1024) or ""
            file:close()
            
            -- Simple string-based hash
            last_content_hash = string.format("%d", #content) -- Simple content length as hash
            last_checked_time = os.time()
            
            log_message("Initial file stats - Size: " .. last_size .. ", Time: " .. os.date("%c", last_checked_time))
            return true
        end
        return false
    end)
    
    if not success then
        log_message("Error initializing file tracking: " .. tostring(result), obs.LOG_ERROR)
    end
    
    -- Restart the timer
    obs.timer_remove(check_image_modified)
    if image_path ~= "" and source_name ~= "" then
        obs.timer_add(check_image_modified, check_interval)
        log_message("SNAPMAKER MONITOR: Actively monitoring " .. image_path .. " for changes", obs.LOG_WARNING)
    end
end

-- Function that refreshes the image source
function refresh_source()
    local source = obs.obs_get_source_by_name(source_name)
    if source ~= nil then
        local source_id = obs.obs_source_get_unversioned_id(source)
        if source_id == "image_source" then
            -- Get the source's settings
            local settings = obs.obs_source_get_settings(source)
            
            -- Store current path
            local current_path = obs.obs_data_get_string(settings, "file")
            
            -- Force OBS to reload the image by:
            -- 1. First setting to empty string
            obs.obs_data_set_string(settings, "file", "")
            obs.obs_source_update(source, settings)
            
            -- 2. Then setting back to image path
            obs.obs_data_set_string(settings, "file", image_path)
            obs.obs_source_update(source, settings)
            
            -- 3. Force a refresh of the cache
            obs.obs_data_set_bool(settings, "unload", false)
            obs.obs_source_update(source, settings)
            
            obs.obs_data_release(settings)
            
            local msg = "SNAPMAKER MONITOR: **** IMAGE REFRESHED **** at " .. os.date("%H:%M:%S")
            log_message(msg, obs.LOG_WARNING)
        else
            log_message("Source ID is not an image_source: " .. tostring(source_id), obs.LOG_WARNING)
        end
        
        obs.obs_source_release(source)
    else
        log_message("ERROR: Source not found: " .. tostring(source_name), obs.LOG_ERROR)
    end
end

-- Timer callback function
function check_image_modified()
    if image_path == "" then return end
    
    -- Use file system stats instead of opening the whole file
    local success, result = pcall(function()
        -- Check if the file exists
        local file_attributes = os.execute("test -f '" .. image_path .. "'")
        if file_attributes ~= 0 then
            -- File doesn't exist - report error once per minute
            local current_time = os.time()
            if current_time - last_file_error_reported > 60 then 
                log_message("ERROR: Image file not found: " .. image_path, obs.LOG_ERROR)
                last_file_error_reported = current_time
            end
            return false
        end
        
        -- Get file modification time with stat command
        local handle = io.popen("stat -f %m '" .. image_path .. "'")
        local mod_time_str = handle:read("*a")
        handle:close()
        
        local mod_time = tonumber(mod_time_str) or 0
        
        -- If modification time changed, refresh
        if mod_time > last_modified_time then
            -- Get file size for logging
            local size_handle = io.popen("stat -f %z '" .. image_path .. "'")
            local size_str = size_handle:read("*a")
            size_handle:close()
            
            log_message("Image modified - New timestamp: " .. os.date("%H:%M:%S", mod_time) .. 
                        ", Size: " .. (tonumber(size_str) or 0) .. " bytes", obs.LOG_INFO)
            
            -- Update tracking variable
            last_modified_time = mod_time
            
            -- Refresh the image in OBS
            refresh_source()
            return true
        end
        
        return false
    end)
    
    if not success then
        log_message("Error checking file: " .. tostring(result), obs.LOG_ERROR)
    end
end

-- Called when the script is unloaded
function script_unload()
    obs.timer_remove(check_image_modified)
    log_message("Snapmaker image monitor stopped", obs.LOG_INFO)
end
