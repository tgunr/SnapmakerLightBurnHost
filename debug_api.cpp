#include <iostream>
#include <string>
#include <fstream>
#include <curl/curl.h>
#include <unistd.h> // for sleep

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::string *response = (std::string*)userdata;
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t file_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::ofstream *outfile = (std::ofstream*)userdata;
    outfile->write(ptr, size * nmemb);
    return size * nmemb;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <snapmaker-ip>" << std::endl;
        return 1;
    }
    
    std::string ip = argv[1];
    std::string response;
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_ALL);
    
    // ============== FIRST REQUEST ==============
    CURL* curl = curl_easy_init();
    
    if (!curl) {
        std::cerr << "Failed to initialize curl" << std::endl;
        return 1;
    }
    
    // First request - request_capture_photo
    std::string url = "http://" + ip + ":8080/api/request_capture_photo?index=0&x=232.0&y=178.0&z=290.0&feedRate=3000&photoQuality=0";
    std::cout << "Step 1: Sending request: " << url << std::endl;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return 1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    std::cout << "HTTP response code: " << http_code << std::endl;
    std::cout << "Response body: " << response << std::endl;
    
    // Clean up first request
    curl_easy_cleanup(curl);
    
    // ============== SECOND REQUEST ==============
    // Check if we got a successful response (status:true)
    if (http_code == 200 && response.find("\"status\":true") != std::string::npos) {
        std::cout << "\nStep 2: Status OK, retrieving image..." << std::endl;
        
        // Small delay to ensure the image is ready
        std::cout << "Waiting 2 seconds for camera to process..." << std::endl;
        sleep(2);
        
        // Create a new curl handle for the second request
        CURL* curl2 = curl_easy_init();
        if (!curl2) {
            std::cerr << "Failed to initialize curl for second request" << std::endl;
            curl_global_cleanup();
            return 1;
        }
        
        // Open file for writing
        std::ofstream outfile("debug_latest.jpg", std::ios::binary);
        if (!outfile.is_open()) {
            std::cerr << "Failed to open output file" << std::endl;
            curl_easy_cleanup(curl2);
            curl_global_cleanup();
            return 1;
        }
        
        // Second request - get_camera_image
        std::string url2 = "http://" + ip + ":8080/api/get_camera_image?index=0";
        std::cout << "Sending request: " << url2 << std::endl;
        
        curl_easy_setopt(curl2, CURLOPT_URL, url2.c_str());
        curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, file_write_callback);
        curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &outfile);
        
        res = curl_easy_perform(curl2);
        
        if (res != CURLE_OK) {
            std::cerr << "Second request failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code);
            std::cout << "Image retrieval HTTP response code: " << http_code << std::endl;
            
            if (http_code == 200) {
                std::cout << "Success! Image saved as debug_latest.jpg" << std::endl;
            } else {
                std::cout << "Failed to retrieve image. HTTP code: " << http_code << std::endl;
            }
        }
        
        // Close file and clean up
        outfile.close();
        curl_easy_cleanup(curl2);
    } else {
        std::cout << "First request did not return status:true, not proceeding to image retrieval." << std::endl;
    }
    
    // Final cleanup
    curl_global_cleanup();
    
    return 0;
}
