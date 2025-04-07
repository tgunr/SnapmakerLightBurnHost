#pragma once

#ifdef MACOS
// macOS implementation of softcam functionality
// This is a stub implementation that will be expanded as needed

#include <iostream>
#include <string>
#include <ctime>

// Set to 1 to enable debug logging, 0 to disable
#define SOFTCAM_DEBUG 0

namespace softcam {
    // Initialize the virtual camera system
    inline bool initialize() {
        std::cout << "macOS softcam stub: initialize()" << std::endl;
        return true;
    }

    // Uninitialize the virtual camera system
    inline void uninitialize() {
        std::cout << "macOS softcam stub: uninitialize()" << std::endl;
    }

    // Track frame count for less verbose logging
    static int frame_count = 0;
    static time_t last_log_time = 0;

    // Set the image for the virtual camera
    inline bool set_frame(unsigned char* data, int width, int height, int stride) {
        // Only log if debugging is enabled
        #if SOFTCAM_DEBUG
            time_t now = time(nullptr);
            frame_count++;
            
            if (now > last_log_time) {
                std::cout << "macOS softcam stub: Sent " << frame_count << " frames to virtual camera " 
                          << width << "x" << height << " (last second)" << std::endl;
                frame_count = 0;
                last_log_time = now;
            }
        #endif
        
        return true;
    }
}
#endif
