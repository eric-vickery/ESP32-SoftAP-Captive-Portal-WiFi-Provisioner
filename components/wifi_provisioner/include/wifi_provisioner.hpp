/**
 * @file wifi_provisioner.hpp
 * @brief A robust, fully encapsulated class for WiFi provisioning on the ESP32.
 *
 * ## Concept: WiFi Provisioning
 *
 * IoT devices such as the ESP32 usually need a WiFi connection to be useful. Hard-coding the credentials
 * (SSID and password) in the code is inflexible and insecure. The process of telling a device
 * this information in a user-friendly way during initial setup is called
 * **provisioning**.
 *
 * This class implements the most common and user-friendly method for devices without a screen
 * or keyboard: the **Soft AP (Access Point) + Captive Portal** mode.
 *
 * ## How it works: The Captive Portal
 *
 * The heart of the class is a mechanism that automatically "forces" a user's smartphone onto a
 * configuration page after it connects to the ESP32. This is the same technology
 * used in hotels or airports.
 *
 * Imagine the ESP32 as a clever hotel concierge:
 *
 * 1.  **Connection & DHCP:** A guest (the smartphone) enters the hotel (connects to the WiFi network
 * created by the ESP32). The concierge (the ESP32's DHCP server) gives the guest a room key (IP address)
 * and says: "If you want to ask for directions (DNS request), ask only me."
 *
 * 2.  **Automatic connectivity check:** The smartphone immediately wants to know whether there is a way
 * out (to the internet). It automatically contacts a well-known address (e.g. "google.com").
 *
 * 3.  **DNS hijacking (the trick):** The smartphone asks the concierge (the ESP32's DNS server): "Where is the
 * exit to 'google.com'?" The concierge deliberately lies and always answers with the same
 * information: "The exit is right here at the reception" (it answers with its own
 * IP address `192.168.4.1`).
 *
 * 4.  **Web server response:** The smartphone goes to the reception (sends an HTTP request to `192.168.4.1`).
 * There, instead of directions, the concierge (the ESP32's web server) hands it the hotel's
 * registration form (the `index.html` configuration page).
 *
 * 5.  **Captive portal detection:** The smartphone's operating system sees through the ruse. It realizes: "This is
 * not the exit, this is a login or configuration page." It then opens this
 * page prominently in a special browser window for the user.
 *
 * ## Special features of the class
 *
 * Over the course of development, this class has been extended with several robust features:
 *
 * - **Full encapsulation:** The class takes care of all necessary system initializations
 * (NVS, WiFi, events, etc.) in a safe way that is executed only once. The `app_main` function
 * therefore stays extremely clean and simple.
 *
 * - **Blocking process without restarts:** The `start_provisioning()` process is "blocking".
 * This means the `app_main` function waits until the user has entered their data.
 * The connection is then established directly, without needing a disruptive device restart.
 *
 * - **Optional persistent storage:** The `start_provisioning()` method has a `persistent_storage` flag.
 * If it is `true`, the data is stored in NVS flash. If it is `false`, the data is only
 * kept temporarily in RAM and is lost after a restart. To avoid conflicts,
 * the ESP-IDF WiFi driver is explicitly prohibited from storing credentials automatically (`WIFI_STORAGE_RAM`).
 *
 * - **Automatic error handling:** If a user enters a wrong password, the device would
 * normally try to connect in an endless loop. This class counts the
 * failed attempts. After 5 unsuccessful attempts, it deletes the faulty credentials from NVS
 * and restarts, so the device automatically returns to configuration mode. It is therefore
 * impossible for the user to "accidentally brick" the device.
 *
 * ## Usage in the application (`main.cpp`)
 *
 * Using the class is extremely simple and requires no knowledge of the internal workings.
 *
 * ```cpp
 * #include "freertos/FreeRTOS.h"
 * #include "freertos/task.h"
 * #include "esp_log.h"
 * #include "wifi_provisioner.hpp" // The only header that is needed.
 *
 * static const char* TAG = "MAIN_APP";
 *
 * extern "C" void app_main(void) {
 * ESP_LOGI(TAG, "Application starting...");
 *
 * // Create the object. The constructor takes care of EVERYTHING.
 * WifiProvisioner provisioner;
 *
 * // Check whether the device is already configured.
 * if (provisioner.is_provisioned()) {
 * // If so, load the data from NVS into the object.
 * provisioner.get_credentials();
 * } else {
 * // If not, start the blocking provisioning process.
 * // 'true' means the entered data is stored persistently.
 * provisioner.start_provisioning("ESP32-Setup", true);
 * }
 * * // Now establish the connection, either with the old (loaded)
 * // or the new (freshly entered) credentials.
 * provisioner.connect_sta();
 *
 * ESP_LOGI(TAG, "Main application logic can now run.");
 * while(true) {
 * // Your actual application logic runs here...
 * vTaskDelay(pdMS_TO_TICKS(10000));
 * }
 * }
 * ```
 */

#pragma once
#include <string>
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h" 
#include "esp_sntp.h"

class WifiProvisioner {
public:
    WifiProvisioner();
    ~WifiProvisioner();

    /**
     * @brief Starts the blocking provisioning mode.
     * * Starts the AP, DNS and web server and waits until the user
     * has entered their data in the captive portal. The servers are stopped afterwards.
     *
     * @param ap_ssid The name of the WiFi network the ESP32 creates.
     * @param persistent_storage If true, the data is stored persistently in NVS.
     * If false, it is only kept temporarily in memory.
     * @param ap_password Optional password for the access point.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t start_provisioning(const std::string& ap_ssid, bool persistent_storage, const std::string& ap_password = "");

    /**
     * @brief Checks whether valid WiFi credentials are stored persistently in NVS.
     */
    bool is_provisioned();

    /**
     * @brief Loads persistently stored credentials from NVS into the class.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t get_credentials();

    /**
     * @brief Tries to connect using the credentials stored in the class.
     *
     * The hostname is taken from the provisioning data. If none was given, "ESP32" is used.
     * @return esp_err_t ESP_OK if initiation succeeded.
     */
    esp_err_t connect_sta();

    /**
     * @brief Configures and starts the SNTP client for time synchronization.
     * * @note Ideally called automatically as soon as a WiFi connection is established.
     */
    void synchronize_time();

    /**
     * @brief Checks whether the system time was successfully synchronized with an NTP server.
     * @return true if the time is synchronized, otherwise false.
     */
    bool is_time_synchronized() const;

private:
    void init_wifi_();

    esp_err_t start_ap_(const std::string& ssid, const std::string& password);
    void stop_ap_(); 

    esp_err_t start_web_server_();
    void stop_web_server_();

    static esp_err_t root_get_handler_(httpd_req_t *req);
    static esp_err_t scan_get_handler_(httpd_req_t *req);
    static esp_err_t save_post_handler_(httpd_req_t *req);
    static esp_err_t style_get_handler_(httpd_req_t *req);
    static esp_err_t captive_portal_handler_(httpd_req_t *req);

    esp_err_t load_credentials_from_nvs_(std::string& ssid, std::string& password, std::string& hostname, std::string& timezone);
    esp_err_t save_credentials_to_nvs_();

    // Static methods for C callbacks
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void time_sync_notification_cb(struct timeval *tv);

    // Member variables for storing the credentials
    std::string _ssid;
    std::string _password;
    std::string _hostname;
    std::string _timezone;

    // Whether there has already been a successful connection
    bool _has_been_connected = false;

    // This flag is set to 'true' by the SNTP callback.
    bool _is_time_synced = false;

    // Flag to track the SNTP initialization
    bool _sntp_initialized = false; 

    // Configuration flags
    bool _persistent_storage = false;
    
    // FreeRTOS objects for synchronization
    EventGroupHandle_t _provisioning_event_group;
    
    httpd_handle_t server_ = nullptr;
    bool wifi_initialized_ = false;

    // Static pointer so the C callback can access the instance
    static WifiProvisioner* s_instance;
};