// SnapmakerLightburnHost.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define STB_IMAGE_IMPLEMENTATION

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <time.h>
#include <memory>
#include <signal.h>
#include <chrono>
#include <iomanip>

#ifdef MACOS
#include "softcam_macos.h"
#else
#include "softcam.h"
#endif

#include "stb_image.h"
#include "curl/curl.h"
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// Global variables for cleanup
bool running = true;
unsigned char* global_image = nullptr;

#ifndef _WIN32
// Implementation of _getch() for macOS and other Unix-like platforms
int _getch() {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

// Non-Windows implementation of _kbhit
int _kbhit() {
    struct timeval tv = { 0, 0 };
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) == 1;
}
#endif

// Custom string format implementation since std::format isn't available on all platforms
template<typename... Args>
std::string string_format(const std::string& format, Args... args) {
    int size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
    if (size <= 0) { return ""; }
    std::unique_ptr<char[]> buf(new char[size]);
    snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1);
}

struct UserConfig
{
    std::string ipAddress = "";
    double basePositionX = 232.0;
    double basePositionY = 178.0;
    double basePositionZ = 290.0;
};

UserConfig activeConfig;

// Signal handler for clean exit
void signal_handler(int sig) {
    std::cout << "Exiting gracefully..." << std::endl;
    running = false;
}

string GetTimeStamp()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "[%d-%m %X] ");
    return ss.str();
}

// Source: https://cplusplus.com/forum/general/46477/
// callback function writes data to a std::ostream
static size_t data_write(void* buf, size_t size, size_t nmemb, void* userp)
{
    if (userp)
    {
        std::ostream& os = *static_cast<std::ostream*>(userp);
        std::streamsize len = size * nmemb;
        if (os.write(static_cast<char*>(buf), len))
            return len;
    }

    return 0;
}

size_t json_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string& data = *static_cast<std::string*>(userdata);
    size_t len = size * nmemb;

    data.append(ptr, len);
    return len;
}

bool IsStatusOk(string jsonResponse)
{
    try
    {
        auto data = json::parse(jsonResponse);
        return data["status"];
    }
    catch (std::exception& e)
    {
        std::cout << GetTimeStamp() << "\t-> Error parsing response: " << e.what() << std::endl;
        return false;
    }
}

void ParseThicknessInfo(string jsonResponse)
{
    try
    {
        auto data = json::parse(jsonResponse);
        auto thickness = (double)data["thickness"];
        std::cout << GetTimeStamp() << "\t-> Measured material thickness: " << thickness << "mm" << std::endl;
    }
    catch(std::exception& e)
    {
        std::cout << GetTimeStamp() << "\t-> Error parsing response: " << e.what() << std::endl;
    }
}

CURLcode GetMaterialThicknessFromSnapmaker(string ipAddress)
{
    std::string printerResponse;

    auto matThickness = curl_easy_init();

    CURLcode result;

    auto targetUrlRequest = string_format("http://%s:8080/api/request_Laser_Material_Thickness?x=%.1f&y=%.1f&z=%.1f&feedRate=3000", 
        ipAddress.c_str(), activeConfig.basePositionX, activeConfig.basePositionY, activeConfig.basePositionZ);

    std::cout << GetTimeStamp() << "\t-> cURL: Sending request to laser... ";

    curl_easy_setopt(matThickness, CURLOPT_URL, targetUrlRequest.c_str());
    curl_easy_setopt(matThickness, CURLOPT_HTTPGET, 1);
    curl_easy_setopt(matThickness, CURLOPT_WRITEDATA, &printerResponse);
    curl_easy_setopt(matThickness, CURLOPT_WRITEFUNCTION, json_callback);
    result = curl_easy_perform(matThickness);
    std::cout << std::endl;

    curl_easy_cleanup(matThickness);

    if (result == CURLE_OK && IsStatusOk(printerResponse))
    {
        ParseThicknessInfo(printerResponse);
    }
    else
    {
        std::cout << GetTimeStamp() << "\t-> cURL: No valid response from Snapmaker. Make sure the material is positioned under the laser." << std::endl;
    }

    return result;
}

CURLcode GetImageFromSnapmaker(string ipAddress, string tempImageFile)
{
    std::string printerResponse;

    auto cameraPos = curl_easy_init();

    CURLcode result;

    auto targetUrlRequest = string_format("http://%s:8080/api/request_capture_photo?index=0&x=%.1f&y=%.1f&z=%.1f&feedRate=3000&photoQuality=0", 
        ipAddress.c_str(), activeConfig.basePositionX, activeConfig.basePositionY, activeConfig.basePositionZ);
    
    std::cout << GetTimeStamp() << "\t-> cURL: Sending request to camera... ";
    
    curl_easy_setopt(cameraPos, CURLOPT_URL, targetUrlRequest.c_str());
    curl_easy_setopt(cameraPos, CURLOPT_HTTPGET, 1);
    curl_easy_setopt(cameraPos, CURLOPT_WRITEDATA, &printerResponse);
    curl_easy_setopt(cameraPos, CURLOPT_WRITEFUNCTION, json_callback);
    result = curl_easy_perform(cameraPos);
    std::cout << std::endl;

    curl_easy_cleanup(cameraPos);

    if (result == CURLE_OK && IsStatusOk(printerResponse))
    {
        std::ofstream ofs(tempImageFile, std::ostream::binary);

        CURL* cameraImage;
        cameraImage = curl_easy_init();

        string targetUrlGetImage = string_format("http://%s:8080/api/get_camera_image?index=0", ipAddress.c_str());

        std::cout << GetTimeStamp() << "\t-> cURL: Retrieving image... ";
        curl_easy_setopt(cameraImage, CURLOPT_URL, targetUrlGetImage.c_str());
        curl_easy_setopt(cameraImage, CURLOPT_HTTPGET, 1);
        curl_easy_setopt(cameraImage, CURLOPT_FILE, &ofs);
        curl_easy_setopt(cameraImage, CURLOPT_WRITEFUNCTION, &data_write);
        result = curl_easy_perform(cameraImage);
        std::cout << std::endl;

        curl_easy_cleanup(cameraImage);

        ofs.close();
    }

    if (result != CURLE_OK)
    {
        std::cout << GetTimeStamp() <<  "\t-> cURL: No valid response from Snapmaker" << std::endl;
    }

    return result;
}

bool CreateConfigFile(string filepath, string targetIp)
{
    try
    {
        std::ofstream f(filepath);

        json defaultData = {
            { "ipAddress", targetIp },
            { "basePositionX", activeConfig.basePositionX },
            { "basePositionY", activeConfig.basePositionY },
            { "basePositionZ", activeConfig.basePositionZ },
        };

        f << std::setw(4) << defaultData << std::endl;
        f.close();

        return true;
    }
    catch (std::exception& e)
    {
        std::cout << GetTimeStamp() << "\t-> Error writing config file: " << e.what() << std::endl;
        return false;
    }
}

bool ReadUserConfig(string filepath, UserConfig& config)
{
    try
    {
        std::ifstream f(filepath);
        json data = json::parse(f);

        config.ipAddress = data["ipAddress"];
        config.basePositionX = data["basePositionX"];
        config.basePositionY = data["basePositionY"];
        config.basePositionZ = data["basePositionZ"];

        return true;
    }
    catch (std::exception& e)
    {
        std::cout << GetTimeStamp() << "\t-> Error parsing config file: " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[])
{
    // Set up signal handlers for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    const char enterASCIIChar = 13; // Windows CR (Enter key)
    const char lineFeedChar = 10;  // Unix/macOS LF (Enter key)
    const char spaceASCIIChar = 32;

    const auto tempImageFile = std::filesystem::current_path() / "latest.jpg";
    const auto configFile = std::filesystem::current_path() / "config.json";
        
    if (!ReadUserConfig(configFile.string(), activeConfig))
    {
        if (argc <= 1)
        {
            std::cout << "Please pass an IP address as the first argument (or use config.json)" << std::endl;
            CreateConfigFile(configFile.string(), "0.0.0.0");
            exit(1);
        }
        else
        {
            activeConfig.ipAddress = string(argv[1]);
            CreateConfigFile(configFile.string(), activeConfig.ipAddress);
        }
    }

#ifdef MACOS
    std::cout << GetTimeStamp() << "macOS: Using virtual camera stub implementation" << std::endl;
    softcam::initialize();
    bool cam_initialized = true;
#else
    auto cam = scCreateCamera(1024, 1280, 60);
#endif

    // Pre-requisites instruction text
    std::cout << " ================================================================================" << std::endl;
    std::cout << "| Please manually execute the following GCode (in order) before using this tool: |" << std::endl;
    std::cout << "|    1. G28 (home axes)                                                          |" << std::endl;
    std::cout << "|    2. G54 (switch to workspace coordinates)                                    |" << std::endl;
    std::cout << " ================================================================================" << std::endl << std::endl;

    std::cout << GetTimeStamp() << string_format("Virtual camera has started @ %s (base position: X%.1f, Y%.1f, Z%.1f)", 
        activeConfig.ipAddress.c_str(), activeConfig.basePositionX, activeConfig.basePositionY, activeConfig.basePositionZ) << std::endl;

    std::cout << GetTimeStamp() << "Press ENTER to request a new image from base position (warning: will move bed & laser!)" << std::endl;
    std::cout << GetTimeStamp() << "Press SPACE to request material thickness from base position (warning: will move bed & laser!)" << std::endl;

    int width = 0, height = 0, comp = 0;
    unsigned char* image = nullptr;
    
    // Try to load the image if it exists
    if (std::filesystem::exists(tempImageFile)) {
        image = stbi_load(tempImageFile.string().c_str(), &width, &height, &comp, 0);
        global_image = image;
    }

    while (running)
    {
        if (image != nullptr) {
#ifdef MACOS
            // Use our platform-specific implementation 
            softcam::set_frame(image, width, height, width * 3);
#else
            scSendFrame(cam, image);
#endif
        }
        
#ifdef _WIN32
        if (_kbhit())
#else
        // Non-blocking check for keypress on Unix/macOS
        fd_set set;
        struct timeval timeout;
        int rv;
        FD_ZERO(&set);
        FD_SET(0, &set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        rv = select(1, &set, NULL, NULL, &timeout);
        if (rv == 1)
#endif
        {
            auto pressedChar = _getch();

            if (pressedChar == enterASCIIChar || pressedChar == lineFeedChar)
            {
                std::cout << GetTimeStamp() << "New image requested, please wait..." << std::endl;

                if (CURLE_OK == GetImageFromSnapmaker(activeConfig.ipAddress, tempImageFile.string()))
                {
                    // Free the old image if we already had one
                    if (image) {
                        stbi_image_free(image);
                        global_image = nullptr;
                    }
                    
                    image = stbi_load(tempImageFile.string().c_str(), &width, &height, &comp, 0);
                    global_image = image;

                    if (stbi_failure_reason() && (string("bad png sig").compare(stbi_failure_reason()) != 0))
                        std::cout << GetTimeStamp() << "\t-> STBI error: " << stbi_failure_reason() << std::endl;

                    std::cout << GetTimeStamp() << "Image sent to virtual camera. Press ENTER to request a new image." << std::endl;
                }
                else
                {
                    std::cout << GetTimeStamp() << "Failed to retrieve image. Press ENTER to request a new image." << std::endl;
                }
            }

            if (pressedChar == spaceASCIIChar)
            {
                std::cout << GetTimeStamp() << "Material thickness requested, please wait..." << std::endl;

                if (CURLE_OK == GetMaterialThicknessFromSnapmaker(activeConfig.ipAddress))
                {
                    std::cout << GetTimeStamp() << "Material thickness received. Press SPACE to request again." << std::endl;
                }
                else
                {
                    std::cout << GetTimeStamp() << "Failed to retrieve material thickness. Press SPACE to request again." << std::endl;
                }
            }
        }
        
        // Small sleep to avoid maxing out CPU
        #ifdef _WIN32
        Sleep(10);
        #else
        usleep(10000);
        #endif
    }

    // Cleanup resources
    if (global_image) {
        stbi_image_free(global_image);
    }
    
#ifdef MACOS
    if (cam_initialized) {
        softcam::uninitialize();
    }
#else
    if (cam) {
        scDestroy(cam);
    }
#endif

    std::cout << GetTimeStamp() << "SnapmakerLightburnHost shut down successfully." << std::endl;
    return 0;
}
