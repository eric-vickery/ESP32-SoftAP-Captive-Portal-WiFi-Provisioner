#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h> 
#include "wifi_provisioner.hpp"

static const char* TAG = "MAIN_APP";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Application starting...");

    WifiProvisioner provisioner;

    // STEP 1: Decide where the data comes from
    if (provisioner.is_provisioned()) {
        // Option A: Data is in NVS. Load it into the class.
        provisioner.get_credentials();
    } else {
        // Option B: No data in NVS. Start the provisioning process.
        // This fills the member variables and stores the data persistently (because of `true`).
        provisioner.start_provisioning("ESP32-WiFi-Provisioning", true);
    }

    // STEP 2: Connect
    // `connect_sta` now uses the data loaded in step 1.
    provisioner.connect_sta();

    // Infinite loop for the main application
    ESP_LOGI(TAG, "Main application logic can now run. Waiting for WiFi events...");
    while(true) {
        // Ask the class directly whether the time is synchronized.
        if (provisioner.is_time_synchronized()) {
            time_t now;
            struct tm timeinfo;
            char strftime_buf[64];

            time(&now);
            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%A, %d. %B %Y %H:%M:%S", &timeinfo);
            
            ESP_LOGI(TAG, "Current local time: %s", strftime_buf);
        } else {
            ESP_LOGI(TAG, "Time is not yet synchronized with the NTP server.");
        }
        
        // Wait one second until the next output
        vTaskDelay(pdMS_TO_TICKS(1000));
    } 
}