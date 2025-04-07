cmake_minimum_required(VERSION 3.10)
project(SnapmakerLightburnHost VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/3rdParty/include
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Source files
set(SOURCE_FILES
    SnapmakerLightburnHost/SnapmakerLightburnHost.cpp
)

# Platform-specific configurations
if(APPLE)
    # macOS doesn't have DirectShow/softcam, so we'll create a macOS-specific implementation
    add_definitions(-DMACOS)
endif()

# Create executable
add_executable(SnapmakerLightburnHost ${SOURCE_FILES})

# Link libraries
target_link_libraries(SnapmakerLightburnHost
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

# Install rules
install(TARGETS SnapmakerLightburnHost
    RUNTIME DESTINATION bin
)
