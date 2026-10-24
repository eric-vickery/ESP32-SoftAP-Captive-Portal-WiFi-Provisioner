#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_netif.h"

// Standard DNS port
#define DNS_PORT 53
#define DNS_MAX_LEN 256

// DNS-Header-Flags
#define DNS_FLAG_QR (1 << 7)
#define DNS_FLAG_RA (1 << 15)

#define DNS_ANSWER_LEN 16
#define DNS_HEADER_SIZE 12
#define DNS_FLAGS_OFFSET 2
#define DNS_ANSWER_COUNT_OFFSET 7
#define DNS_QUESTION_SUFFIX_SIZE 4 // QTYPE (2 bytes) + QCLASS (2 bytes)

// Answer section
#define DNS_COMPRESSION_POINTER 0xC0
#define DNS_COMPRESSION_OFFSET 0x0C
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

//  Time and length values (TTL & RDLENGTH)
#define DNS_ANSWER_TTL_S 120 // Time-To-Live in seconds
#define DNS_RDLENGTH_IPV4 4

// Static variables for the task
static const char *TAG = "DNS_SERVER";
static int sock_fd = -1;
static volatile bool dns_server_running = false;

/**
 * @brief Creates a DNS response that points to the AP's IP.
 * @param request The original DNS request packet.
 * @param response The buffer for the DNS response packet.
 * @param ap_ip The IP address of the access point.
 */
static void create_dns_response(uint8_t *request, uint8_t *response, const esp_ip4_addr_t *ap_ip) {
    // Find the end of the "Question" section
    uint8_t *p = request + DNS_HEADER_SIZE;
    while (*p != 0) {
        p += (*p + 1);
    }
    size_t question_len = (p - (request + DNS_HEADER_SIZE)) + 1; // Length of the domain name incl. null byte

    // Copy header and the complete question section (name + suffix)
    size_t request_len = DNS_HEADER_SIZE + question_len + DNS_QUESTION_SUFFIX_SIZE;
    memcpy(response, request, request_len); // Header + Question + QTYPE/QCLASS

    // Set the header flags for a response
    response[DNS_FLAGS_OFFSET] |= DNS_FLAG_QR; // Query Response
    response[DNS_FLAGS_OFFSET ] |= DNS_FLAG_RA; // Recursion Available

    // Set the answer count to 1
    response[DNS_ANSWER_COUNT_OFFSET] = 1;

    // Set the pointer to the end of the copied "Question" section in the response buffer
    p = response + request_len;

    // Add the "Answer" section
    *p++ = DNS_COMPRESSION_POINTER;
    *p++ = DNS_COMPRESSION_OFFSET;

    *p++ = 0x00; *p++ = DNS_TYPE_A;      // Type
    *p++ = 0x00; *p++ = DNS_CLASS_IN;    // Class

    // TTL (Time To Live) in big-endian (network byte order)
    *p++ = (DNS_ANSWER_TTL_S >> 24) & 0xFF;
    *p++ = (DNS_ANSWER_TTL_S >> 16) & 0xFF;
    *p++ = (DNS_ANSWER_TTL_S >> 8) & 0xFF;
    *p++ = DNS_ANSWER_TTL_S & 0xFF;

    // Data length (RDLENGTH)
    *p++ = 0x00; *p++ = DNS_RDLENGTH_IPV4;
    
    // Insert the AP's IP address as the answer
    memcpy(p, &ap_ip->addr, DNS_RDLENGTH_IPV4);
}

/**
 * @brief The FreeRTOS task that runs the DNS server.
 */
static void dns_server_task(void *pvParameters) {
    uint8_t request_buffer[DNS_MAX_LEN];
    uint8_t response_buffer[DNS_MAX_LEN];
    struct sockaddr client;
    socklen_t client_len = sizeof(client);

    // Get the IP information of the access point interface
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    // Create and configure the server address for the socket.
    // We use the specific `sockaddr_in` structure for IPv4.
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all IPs of the device
    server_addr.sin_port = htons(DNS_PORT);   // Listen on port 53

    // Create a UDP socket
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        vTaskDelete(NULL);
        return;
    }

    // Bind the socket to the configured address and port.
    // The bind() function expects a pointer to the generic `sockaddr` structure,
    // so we cast the pointer to our `sockaddr_in` structure.
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(sock_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server started on port %d", DNS_PORT);
    dns_server_running = true;

    // Main loop for receiving and answering DNS requests
    while (dns_server_running) {
        int len = recvfrom(sock_fd, request_buffer, sizeof(request_buffer), 0, (struct sockaddr *)&client, &client_len);
        if (len > 0) {
            // Ignore requests that are already responses
            if ((request_buffer[2] & DNS_FLAG_QR) == 0) {
                create_dns_response(request_buffer, response_buffer, &ip_info.ip);
                sendto(sock_fd, response_buffer, len + DNS_ANSWER_LEN, 0, (struct sockaddr *)&client, client_len);
            }
        }
    }

    // Clean up when the loop ends
    close(sock_fd);
    sock_fd = -1;
    vTaskDelete(NULL);
}

/**
 * @brief Starts the DNS server task.
 */
void start_dns_server() {
    if (!dns_server_running) {
        xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
    }
}

/**
 * @brief Stops the DNS server task.
 */
void stop_dns_server() {
    if (dns_server_running) {
        dns_server_running = false;
        // Shut down the socket to end the blocking recvfrom() call
        if (sock_fd != -1) {
            shutdown(sock_fd, SHUT_RDWR);
        }
    }
}