#include "web_server.h"
#include "data_buffer.h"
#include "imu_manager.h"
#include "led_status.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_netif.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_timer.h"

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static httpd_handle_t ws_server = NULL;

// WebSocket connection tracking
typedef struct {
    int fd;
    bool active;
} ws_connection_t;

static ws_connection_t ws_connections[WEBSOCKET_MAX_CONNECTIONS];
static SemaphoreHandle_t ws_mutex = NULL;

// Forward declarations
static esp_err_t api_data_handler(httpd_req_t *req);
static esp_err_t api_stats_handler(httpd_req_t *req);
static esp_err_t api_download_handler(httpd_req_t *req);
static esp_err_t api_ip_handler(httpd_req_t *req);
static void ws_register_connection(int fd);
static esp_err_t file_handler(httpd_req_t *req);
static void ws_register_connection(int fd);
static void ws_unregister_connection(int fd);
static esp_err_t ws_send_to_all(const char *data, size_t len);
static void ws_broadcast_task(void *arg);
static esp_err_t root_handler(httpd_req_t *req);
// API IP endpoint - returns current IP address as JSON
static esp_err_t api_ip_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API IP request received");
    esp_netif_ip_info_t ip_info;
    char ip_str[32] = {0};
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
            esp_ip4_addr1(&ip_info.ip), esp_ip4_addr2(&ip_info.ip), esp_ip4_addr3(&ip_info.ip), esp_ip4_addr4(&ip_info.ip));
        ESP_LOGI(TAG, "Returning IP: %s", ip_str);
    } else {
        strcpy(ip_str, "0.0.0.0");
        ESP_LOGW(TAG, "Failed to get IP, returning 0.0.0.0");
    }
    char json[64];
    snprintf(json, sizeof(json), "{\"ip\":\"%s\"}", ip_str);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// API Data endpoint - returns latest sensor data
static esp_err_t api_data_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Data request");
    
    imu_data_t data;
    esp_err_t ret = data_buffer_get_latest(&data);
    
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "No data available", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Convert to JSON
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "timestamp_us", data.timestamp_us);
    
    // Magnetometer
    if (data.magnetometer.valid) {
        cJSON *mag = cJSON_CreateObject();
        cJSON_AddNumberToObject(mag, "x_mg", data.magnetometer.x_mg);
        cJSON_AddNumberToObject(mag, "y_mg", data.magnetometer.y_mg);
        cJSON_AddNumberToObject(mag, "z_mg", data.magnetometer.z_mg);
        cJSON_AddNumberToObject(mag, "temperature_c", data.magnetometer.temperature_c);
        cJSON_AddItemToObject(json, "magnetometer", mag);
    }
    
    // Accelerometer
    if (data.accelerometer.valid) {
        cJSON *accel = cJSON_CreateObject();
        cJSON_AddNumberToObject(accel, "x_g", data.accelerometer.x_g);
        cJSON_AddNumberToObject(accel, "y_g", data.accelerometer.y_g);
        cJSON_AddNumberToObject(accel, "z_g", data.accelerometer.z_g);
        cJSON_AddItemToObject(json, "accelerometer", accel);
    }
    
    // IMU 6-axis
    if (data.imu_6axis.valid) {
        cJSON *imu = cJSON_CreateObject();
        cJSON *accel = cJSON_CreateObject();
        cJSON *gyro = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(accel, "x_g", data.imu_6axis.accel_x_g);
        cJSON_AddNumberToObject(accel, "y_g", data.imu_6axis.accel_y_g);
        cJSON_AddNumberToObject(accel, "z_g", data.imu_6axis.accel_z_g);
        cJSON_AddNumberToObject(gyro, "x_dps", data.imu_6axis.gyro_x_dps);
        cJSON_AddNumberToObject(gyro, "y_dps", data.imu_6axis.gyro_y_dps);
        cJSON_AddNumberToObject(gyro, "z_dps", data.imu_6axis.gyro_z_dps);
        cJSON_AddNumberToObject(imu, "temperature_c", data.imu_6axis.temperature_c);
        
        cJSON_AddItemToObject(imu, "accelerometer", accel);
        cJSON_AddItemToObject(imu, "gyroscope", gyro);
        cJSON_AddItemToObject(json, "imu_6axis", imu);
    }
    
    // Inclinometer
    if (data.inclinometer.valid) {
        cJSON *incl = cJSON_CreateObject();
        cJSON *angles = cJSON_CreateObject();
        cJSON *accel = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(angles, "x_deg", data.inclinometer.angle_x_deg);
        cJSON_AddNumberToObject(angles, "y_deg", data.inclinometer.angle_y_deg);
        cJSON_AddNumberToObject(angles, "z_deg", data.inclinometer.angle_z_deg);
        cJSON_AddNumberToObject(accel, "x_g", data.inclinometer.accel_x_g);
        cJSON_AddNumberToObject(accel, "y_g", data.inclinometer.accel_y_g);
        cJSON_AddNumberToObject(accel, "z_g", data.inclinometer.accel_z_g);
        cJSON_AddNumberToObject(incl, "temperature_c", data.inclinometer.temperature_c);
        
        cJSON_AddItemToObject(incl, "angles", angles);
        cJSON_AddItemToObject(incl, "accelerometer", accel);
        cJSON_AddItemToObject(json, "inclinometer", incl);
    }
    
    char *json_string = cJSON_Print(json);
    if (json_string != NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "JSON generation failed", HTTPD_RESP_USE_STRLEN);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// API Stats endpoint - returns buffer statistics
static esp_err_t api_stats_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Stats request");
    
    buffer_stats_t stats;
    esp_err_t ret = data_buffer_get_stats(&stats);
    
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to get stats", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "total_samples", stats.total_samples);
    cJSON_AddNumberToObject(json, "dropped_samples", stats.dropped_samples);
    cJSON_AddNumberToObject(json, "buffer_overflows", stats.buffer_overflows);
    cJSON_AddNumberToObject(json, "last_timestamp_us", stats.last_timestamp_us);
    cJSON_AddNumberToObject(json, "avg_processing_time_us", stats.avg_processing_time_us);
    cJSON_AddNumberToObject(json, "buffer_count", data_buffer_get_count());
    cJSON_AddBoolToObject(json, "buffer_full", data_buffer_is_full());
    cJSON_AddBoolToObject(json, "buffer_empty", data_buffer_is_empty());
    
    char *json_string = cJSON_Print(json);
    if (json_string != NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "JSON generation failed", HTTPD_RESP_USE_STRLEN);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// API Config endpoint - handles configuration changes
static esp_err_t api_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Config request");
    
    if (req->method == HTTP_POST) {
        size_t total_len = req->content_len;
        if (total_len == 0 || total_len > 2048) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Invalid content length", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        char *content = malloc(total_len + 1);
        if (!content) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Out of memory", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        size_t received = 0;
        while (received < total_len) {
            int ret = httpd_req_recv(req, content + received, total_len - received);
            if (ret <= 0) {
                free(content);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Failed to read body", HTTPD_RESP_USE_STRLEN);
                return ESP_FAIL;
            }
            received += ret;
        }
        content[received] = '\0';
        
        cJSON *json = cJSON_Parse(content);
        free(content);
        if (json == NULL) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        
        // Process configuration
        cJSON *sampling_rate = cJSON_GetObjectItem(json, "sampling_rate");
        if (cJSON_IsNumber(sampling_rate)) {
            imu_manager_set_sampling_rate(sampling_rate->valueint);
        }
        
        cJSON *watermark = cJSON_GetObjectItem(json, "fifo_watermark");
        if (cJSON_IsNumber(watermark)) {
            imu_manager_set_fifo_watermark(watermark->valueint);
        }

        cJSON *sensors = cJSON_GetObjectItem(json, "sensors");
        if (cJSON_IsObject(sensors)) {
            struct {
                const char *key;
                uint8_t id;
            } sensor_map[] = {
                {"magnetometer", SENSOR_MAGNETOMETER},
                {"accelerometer", SENSOR_ACCELEROMETER},
                {"imu_6axis", SENSOR_IMU_6AXIS},
                {"inclinometer", SENSOR_INCLINOMETER},
            };
            for (size_t i = 0; i < sizeof(sensor_map)/sizeof(sensor_map[0]); ++i) {
                cJSON *item = cJSON_GetObjectItem(sensors, sensor_map[i].key);
                if (cJSON_IsBool(item)) {
                    imu_manager_enable_sensor(sensor_map[i].id, cJSON_IsTrue(item));
                }
            }
        }
        
        cJSON_Delete(json);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    } else {
        // Handle GET request - return current configuration
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "sampling_rate", imu_manager_get_sampling_rate());
        cJSON_AddNumberToObject(json, "fifo_watermark", imu_manager_get_fifo_watermark());
        uint8_t enabled = imu_manager_get_enabled_sensors();
        cJSON *sensors = cJSON_CreateObject();
        cJSON_AddBoolToObject(sensors, "magnetometer", (enabled & SENSOR_MAGNETOMETER) != 0);
        cJSON_AddBoolToObject(sensors, "accelerometer", (enabled & SENSOR_ACCELEROMETER) != 0);
        cJSON_AddBoolToObject(sensors, "imu_6axis", (enabled & SENSOR_IMU_6AXIS) != 0);
        cJSON_AddBoolToObject(sensors, "inclinometer", (enabled & SENSOR_INCLINOMETER) != 0);
        cJSON_AddItemToObject(json, "sensors", sensors);
        
        char *json_string = cJSON_Print(json);
        if (json_string != NULL) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, json_string, strlen(json_string));
            free(json_string);
        }
        
        cJSON_Delete(json);
    }
    
    return ESP_OK;
}

// API Download endpoint - returns data in various formats
static esp_err_t api_download_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Download request");
    
    // Get format parameter
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char format[16];
            if (httpd_query_key_value(buf, "format", format, sizeof(format)) == ESP_OK) {
                if (strcmp(format, "csv") == 0) {
                    char *csv_buffer = NULL;
                    size_t csv_len = 0;
                    esp_err_t ret = data_buffer_export_csv_dynamic(&csv_buffer, &csv_len, 100);
                    if (ret == ESP_OK && csv_buffer != NULL) {
                        httpd_resp_set_type(req, "text/csv");
                        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=imu_data.csv");
                        httpd_resp_send(req, csv_buffer, csv_len);
                        free(csv_buffer);
                    } else {
                        if (csv_buffer) free(csv_buffer);
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        httpd_resp_send(req, "Failed to export CSV", HTTPD_RESP_USE_STRLEN);
                    }
                } else if (strcmp(format, "json") == 0) {
                    char *json_buffer = NULL;
                    size_t json_len = 0;
                    esp_err_t ret = data_buffer_export_json_dynamic(&json_buffer, &json_len, 100);
                    if (ret == ESP_OK && json_buffer != NULL) {
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=imu_data.json");
                        httpd_resp_send(req, json_buffer, json_len);
                        free(json_buffer);
                    } else {
                        if (json_buffer) free(json_buffer);
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        httpd_resp_send(req, "Failed to export JSON", HTTPD_RESP_USE_STRLEN);
                    }
                } else {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Unsupported format", HTTPD_RESP_USE_STRLEN);
                }
            } else {
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Missing format parameter", HTTPD_RESP_USE_STRLEN);
            }
        }
        free(buf);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing query parameters", HTTPD_RESP_USE_STRLEN);
    }
    
    return ESP_OK;
}

// WebSocket data handler
static esp_err_t ws_data_handler(httpd_req_t *req)
{
    // On initial GET upgrade, register the connection
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_register_connection(fd);
        ESP_LOGI(TAG, "WebSocket connected fd=%d", fd);
        return ESP_OK;
    }

    // For incoming messages (optional control), just consume and ack
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &ws_pkt, 0) == ESP_OK && ws_pkt.len) {
        uint8_t tmp_buf[64];
        ws_pkt.payload = tmp_buf;
        size_t to_read = ws_pkt.len < sizeof(tmp_buf) ? ws_pkt.len : sizeof(tmp_buf);
        httpd_ws_recv_frame(req, &ws_pkt, to_read);
        // No-op; could parse control messages here
    }
    return ESP_OK;
}

// WebSocket control handler
static esp_err_t ws_control_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket control connection request");
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

// File handler for serving static files
static esp_err_t file_handler(httpd_req_t *req)
{
    const char *filepath = req->uri + 1; // Skip leading '/'
    
    // Security check - prevent directory traversal
    if (strstr(filepath, "..") != NULL) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Access denied", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Default to index.html if no file specified
    if (strlen(filepath) == 0) {
        filepath = "index.html";
    }
    
    // Build full path
    char full_path[64];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filepath);
    
    // Open file
    FILE *file = fopen(full_path, "r");
    if (file == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "File not found", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Set content type based on file extension
    if (strstr(filepath, ".html") != NULL) {
        httpd_resp_set_type(req, "text/html");
    } else if (strstr(filepath, ".css") != NULL) {
        httpd_resp_set_type(req, "text/css");
    } else if (strstr(filepath, ".js") != NULL) {
        httpd_resp_set_type(req, "application/javascript");
    } else if (strstr(filepath, ".json") != NULL) {
        httpd_resp_set_type(req, "application/json");
    }
    
    // Send file content
    char buffer[512];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK) {
            break;
        }
    }
    
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0); // End chunked response
    return ESP_OK;
}

// WebSocket connection management
static void ws_register_connection(int fd)
{
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (!ws_connections[i].active) {
                ws_connections[i].fd = fd;
                ws_connections[i].active = true;
                ESP_LOGI(TAG, "WebSocket connection registered: fd=%d at slot %d", fd, i);
                
                // Send IP address to client as a simple JSON message
                char ip_msg[64];
                esp_netif_ip_info_t ip_info;
                esp_netif_t *netif = esp_netif_get_default_netif();
                if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    snprintf(ip_msg, sizeof(ip_msg), "{\"ip\":\"%d.%d.%d.%d\"}",
                        esp_ip4_addr1(&ip_info.ip), esp_ip4_addr2(&ip_info.ip), esp_ip4_addr3(&ip_info.ip), esp_ip4_addr4(&ip_info.ip));
                    ESP_LOGI(TAG, "Sending IP to WebSocket client: %s", ip_msg);
                } else {
                    strcpy(ip_msg, "{\"ip\":\"0.0.0.0\"}");
                    ESP_LOGW(TAG, "Failed to get IP for WebSocket client");
                }
                httpd_ws_frame_t frame = {
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)ip_msg,
                    .len = strlen(ip_msg)
                };
                httpd_ws_send_frame_async(server, fd, &frame);
                break;
            }
        }
        
        // Count active connections
        int active_count = 0;
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (ws_connections[i].active) {
                active_count++;
            }
        }
        
        xSemaphoreGive(ws_mutex);
        
        // Switch LED to data transmission mode when first client connects
        if (active_count == 1) {
            led_status_set_state(LED_STATUS_DATA_IDLE);
            ESP_LOGI(TAG, "First WebSocket client connected - LED switched to data mode");
        }
    }
}

static void ws_unregister_connection(int fd)
{
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int active_count = 0;
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (ws_connections[i].active && ws_connections[i].fd == fd) {
                ws_connections[i].active = false;
                ESP_LOGI(TAG, "WebSocket connection unregistered: fd=%d", fd);
            } else if (ws_connections[i].active) {
                active_count++;
            }
        }
        xSemaphoreGive(ws_mutex);
        
        // Switch LED back to WiFi connected mode when last client disconnects
        if (active_count == 0) {
            led_status_set_state(LED_STATUS_WIFI_CONNECTED);
            ESP_LOGI(TAG, "All WebSocket clients disconnected - LED switched back to WiFi mode");
        }
    }
}

static esp_err_t ws_send_to_all(const char *data, size_t len)
{
    static uint32_t total_sends = 0;
    int active_connections = 0;
    
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = len
    };
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (ws_connections[i].active) {
                httpd_ws_send_frame_async(server, ws_connections[i].fd, &frame);
                active_connections++;
            }
        }
        xSemaphoreGive(ws_mutex);
        
        total_sends++;
        if (total_sends % 500 == 0) {
            ESP_LOGI(TAG, "WS broadcast: %lu total sends, %d active connections", total_sends, active_connections);
        }
    }
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    ESP_LOGI(TAG, "Starting web server...");
    
    // Create WebSocket mutex
    ws_mutex = xSemaphoreCreateMutex();
    if (ws_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WebSocket mutex");
        return ESP_FAIL;
    }
    
    // Initialize WebSocket connections
    memset(ws_connections, 0, sizeof(ws_connections));
    
    // HTTP server configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = WEB_SERVER_MAX_URI_HANDLERS;
    config.stack_size = WEB_SERVER_STACK_SIZE;
    
    // Start HTTP server
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started on port %d", WEB_SERVER_PORT);
        
        // Register URI handlers
        httpd_uri_t api_data_uri = {
            .uri = API_DATA_PATH,
            .method = HTTP_GET,
            .handler = api_data_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_data_uri);

        httpd_uri_t api_stats_uri = {
            .uri = API_STATS_PATH,
            .method = HTTP_GET,
            .handler = api_stats_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_stats_uri);

        httpd_uri_t api_config_uri = {
            .uri = API_CONFIG_PATH,
            .method = HTTP_GET,
            .handler = api_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_config_uri);

        httpd_uri_t api_config_post_uri = {
            .uri = API_CONFIG_PATH,
            .method = HTTP_POST,
            .handler = api_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_config_post_uri);

        httpd_uri_t api_download_uri = {
            .uri = API_DOWNLOAD_PATH,
            .method = HTTP_GET,
            .handler = api_download_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_download_uri);

        // Register /api/ip endpoint
        httpd_uri_t api_ip_uri = {
            .uri = API_IP_PATH,
            .method = HTTP_GET,
            .handler = api_ip_handler,
            .user_ctx = NULL
        };
        esp_err_t ip_reg_result = httpd_register_uri_handler(server, &api_ip_uri);
        ESP_LOGI(TAG, "Registered /api/ip endpoint: %s (result: %d)", API_IP_PATH, ip_reg_result);

        // WebSocket endpoint for realtime data
        httpd_uri_t ws_data_uri = {
            .uri = WS_DATA_PATH,
            .method = HTTP_GET,
            .handler = ws_data_handler,
            .user_ctx = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_data_uri);
        
        // Root handler serves an embedded dashboard when SPIFFS is not populated
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        // File handler for static content under /spiffs - MUST BE LAST
        httpd_uri_t file_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = file_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &file_uri);
        
        ESP_LOGI(TAG, "Web server started successfully");
        // Start broadcaster task (50 Hz)
        xTaskCreatePinnedToCore(ws_broadcast_task, "ws_broadcast", 4096, NULL, 4, NULL, 0);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
}

esp_err_t web_server_stop(void)
{
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
    }
    
    if (ws_mutex != NULL) {
        vSemaphoreDelete(ws_mutex);
        ws_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

esp_err_t web_server_broadcast_data(const char *data, size_t len)
{
    return ws_send_to_all(data, len);
}

esp_err_t web_server_set_sampling_rate(uint32_t rate_hz)
{
    return imu_manager_set_sampling_rate(rate_hz);
}

esp_err_t web_server_set_fifo_watermark(uint16_t watermark)
{
    return imu_manager_set_fifo_watermark(watermark);
}

esp_err_t web_server_enable_sensor(uint8_t sensor_id, bool enable)
{
    return imu_manager_enable_sensor(sensor_id, enable);
}

// Broadcast latest sample periodically as compact JSON
static void ws_broadcast_task(void *arg)
{
    (void)arg;
    char json[768];
    uint32_t send_count = 0;
    uint32_t no_data_count = 0;
    uint64_t rate_window_start_us = 0;
    uint32_t rate_window_msgs = 0;
    float last_msg_rate = 0.0f;
    
    ESP_LOGI(TAG, "WebSocket broadcast task started");
    
    for (;;) {
        imu_data_t d;
        if (data_buffer_get_latest(&d) == ESP_OK) {
            // LED ON - bắt đầu gửi dữ liệu
            led_status_data_pulse_start();
            
            uint64_t now_us = (d.timestamp_us != 0) ? d.timestamp_us : esp_timer_get_time();
            if (rate_window_start_us == 0 || now_us <= rate_window_start_us) {
                rate_window_start_us = now_us;
                rate_window_msgs = 0;
            }
            rate_window_msgs++;
            uint64_t elapsed_us = now_us - rate_window_start_us;
            float current_rate = 0.0f;
            if (elapsed_us > 0) {
                current_rate = (rate_window_msgs * 1000000.0f) / (float)elapsed_us;
            }
            if (elapsed_us >= 1000000ULL) {
                rate_window_msgs = 0;
                rate_window_start_us = now_us;
                last_msg_rate = current_rate;
            } else if (current_rate > 0.0f) {
                last_msg_rate = current_rate;
            }
            // Build compact JSON manually to reduce overhead
            int n = 0;
            n += snprintf(json + n, sizeof(json) - n, "{\"t\":%llu", (unsigned long long)d.timestamp_us);
            if (d.magnetometer.valid) {
                n += snprintf(json + n, sizeof(json) - n,
                              ",\"mag_iis2\":{\"name\":\"IIS2MDC Magnetometer\",\"unit\":\"mG\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"temperature\":%.2f}",
                              d.magnetometer.x_mg, d.magnetometer.y_mg, d.magnetometer.z_mg,
                              d.magnetometer.temperature_c);
            }
            if (d.accelerometer.valid) {
                const float ax_g = d.accelerometer.x_g;
                const float ay_g = d.accelerometer.y_g;
                const float az_g = d.accelerometer.z_g;
                const float g_to_ms2 = 9.80665f;
                n += snprintf(json + n, sizeof(json) - n,
                              ",\"acc_iis3_g\":{\"name\":\"IIS3DWB Accelerometer\",\"unit\":\"g\",\"x\":%.5f,\"y\":%.5f,\"z\":%.5f}",
                              ax_g, ay_g, az_g);
                n += snprintf(json + n, sizeof(json) - n,
                              ",\"acc_iis3_ms2\":{\"name\":\"IIS3DWB Accelerometer\",\"unit\":\"m/s^2\",\"x\":%.5f,\"y\":%.5f,\"z\":%.5f}",
                              ax_g * g_to_ms2, ay_g * g_to_ms2, az_g * g_to_ms2);
            }
            if (d.imu_6axis.valid) {
                n += snprintf(json + n, sizeof(json) - n,
                              ",\"gyr_icm\":{\"name\":\"ICM45686 Gyroscope\",\"unit\":\"deg/s\",\"x\":%.4f,\"y\":%.4f,\"z\":%.4f}",
                              d.imu_6axis.gyro_x_dps, d.imu_6axis.gyro_y_dps, d.imu_6axis.gyro_z_dps);
            }
            if (d.inclinometer.valid) {
                n += snprintf(json + n, sizeof(json) - n,
                              ",\"inc_scl\":{\"name\":\"SCL3300 Inclinometer\",\"unit\":\"deg\",\"angle_x\":%.2f,\"angle_y\":%.2f,\"angle_z\":%.2f,\"temperature\":%.2f}",
                              d.inclinometer.angle_x_deg, d.inclinometer.angle_y_deg, d.inclinometer.angle_z_deg,
                              d.inclinometer.temperature_c);
            }
            n += snprintf(json + n, sizeof(json) - n,
                          ",\"statistics\":{\"msg_per_second\":%.2f}}",
                          last_msg_rate);
            ws_send_to_all(json, n);
            send_count++;
            
            // LED OFF - gửi xong dữ liệu
            led_status_data_pulse_end();
            
            // Log every 100 sends
            if (send_count % 100 == 0) {
                ESP_LOGI(TAG, "Sent %lu WebSocket messages (no_data: %lu)", send_count, no_data_count);
            }
        } else {
            no_data_count++;
            if (no_data_count % 100 == 0) {
                ESP_LOGW(TAG, "No data available in buffer (count: %lu)", no_data_count);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // ~50 Hz
    }
}

// Simple embedded HTML dashboard (served at "/")
static esp_err_t root_handler(httpd_req_t *req)
{
    // Get IP address
    char ip_str[32] = "0.0.0.0";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
            esp_ip4_addr1(&ip_info.ip), esp_ip4_addr2(&ip_info.ip), 
            esp_ip4_addr3(&ip_info.ip), esp_ip4_addr4(&ip_info.ip));
    }
    
    ESP_LOGI(TAG, "Root handler - ESP32 IP: %s", ip_str);
    
    // Log thêm để xác nhận địa chỉ IP
    ESP_LOGW(TAG, "### INJECTING IP: '%s' INTO HTML ###", ip_str);
    
    // HTML - part 1 (before IP injection)
    static const char *html_part1 =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>ESP32-C6 IMU Monitor</title>"
        "<script src='https://cdn.jsdelivr.net/npm/chart.js@4'></script>"
        "<style>"
        "body{font-family:Arial,Helvetica,sans-serif;margin:20px;background:#f8fafc;color:#1e293b}"
        "h2{color:#0369a1;margin-bottom:12px}"
        ".status{padding:12px;border-left:4px solid #10b981;background:#fff;margin:12px 0;font-weight:600;box-shadow:0 1px 3px rgba(0,0,0,0.1);display:flex;justify-content:space-between;align-items:center;}"
        ".status-ip{font-size:14px;color:#64748b;margin-left:auto;text-align:right;font-weight:bold;min-width:100px;}"
        ".metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px;margin:20px 0}"
        ".metric{background:#fff;padding:12px;border-radius:6px;box-shadow:0 1px 3px rgba(0,0,0,0.08)}"
        ".metric .label{display:block;font-size:12px;text-transform:uppercase;color:#64748b;letter-spacing:0.05em}"
        ".metric .value{display:block;font-size:20px;font-weight:600;margin-top:6px;color:#0f172a;}"
        ".chart-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:18px;margin:18px 0}"
        ".chart-box{background:#fff;padding:12px;border-radius:6px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}"
        ".chart-box h3{margin:0 0 10px 0;color:#0284c7;font-size:15px}"
        "canvas{width:100%!important;height:220px!important;background:#f8fafc;border:1px solid #e2e8f0;border-radius:4px}"
        ".log{background:#fff;padding:12px;height:160px;overflow:auto;border:1px solid #e2e8f0;border-radius:6px;font-family:monospace;font-size:11px;color:#475569}"
        "</style></head><body>"
        "<h2>ESP32-C6 IMU Monitor (IP: ";
    
    // HTML - ip part (inject IP directly into HTML tag)
    static const char *html_ip_part = ")</h2>"
        "<div class='status' id='status'><span id='statusText'>Connecting...</span><span class='status-ip' id='statusIp'></span></div>"
        "<div class='metrics'>"
        "  <div class='metric'><span class='label'>Messages</span><span class='value' id='metricMsg'>0</span></div>"
        "  <div class='metric'><span class='label'>Active Sensors</span><span class='value' id='metricSensors'>0</span></div>"
        "  <div class='metric'><span class='label'>msg/s</span><span class='value' id='metricMsgRate'>0</span></div>"
        "</div>"
        "<div class='chart-grid' id='charts'></div>"
        "<h3 style='color:#0284c7;margin-top:24px;'>Event Log</h3>"
        "<div class='log' id='log'></div>"
        "<script>"
        "const ESP32_IP='";
    
    // HTML - part 2 (after IP injection)
    static const char *html_part2 =
        "';"
        "(()=>{"
        "  const statusEl=document.getElementById('status');"
        "  const statusText=document.getElementById('statusText');"
        "  const statusIp=document.getElementById('statusIp');"
        "  statusIp.textContent=ESP32_IP;"
        "  statusIp.style.display='inline-block';"
        "  console.log('ESP32 IP set to:', ESP32_IP);"
        "  document.title = 'ESP32-C6 - ' + ESP32_IP;"
        "  const logEl=document.getElementById('log');"
        "  const chartsContainer=document.getElementById('charts');"
        "  const metrics={"
        "    msg:document.getElementById('metricMsg'),"
        "    sensors:document.getElementById('metricSensors'),"
        "    msgRate:document.getElementById('metricMsgRate')"
        "  };"
        "  const chartCfg={type:'line',options:{responsive:true,maintainAspectRatio:false,animation:false,scales:{x:{display:false},y:{beginAtZero:false,grid:{color:'rgba(226,232,240,0.9)'}}}}};"
        "  const defaultColors=['#ef4444','#10b981','#3b82f6','#f97316'];"
        "  const chartDefs={"
        "    mag_iis2:{title:'IIS2MDC Magnetometer (mG)',labels:['X','Y','Z'],colors:['#dc2626','#16a34a','#1d4ed8']},"
        "    acc_iis3_g:{title:'IIS3DWB Accelerometer (g)',labels:['X','Y','Z'],colors:['#b91c1c','#047857','#7c3aed']},"
        "    acc_iis3_ms2:{title:'IIS3DWB Accelerometer (m/s²)',labels:['X','Y','Z'],colors:['#d97706','#22c55e','#2563eb']},"
        "    gyr_icm:{title:'ICM45686 Gyroscope (deg/s)',labels:['X','Y','Z'],colors:['#f59e0b','#8b5cf6','#0ea5e9']},"
        "    inc_scl:{title:'SCL3300 Inclinometer (deg)',labels:['Angle X','Angle Y','Angle Z'],colors:['#e67e22','#3b82f6','#1e293b']}"
        "  };"
        "  const charts={};"
        "  const maxPoints=100;"
        "  let msgCount=0;"
        "  function addLog(msg){const t=new Date().toLocaleTimeString();logEl.innerHTML='['+t+'] '+msg+'<br>'+logEl.innerHTML;const parts=logEl.innerHTML.split('<br>');if(parts.length>200){logEl.innerHTML=parts.slice(0,200).join('<br>');}}"
        "  function chartTitle(key,payload){if(!payload){return(chartDefs[key]&&chartDefs[key].title)||('Sensor '+key);}const base=payload.title||payload.name||(chartDefs[key]&&chartDefs[key].title)||('Sensor '+key);return payload.unit?base+' ('+payload.unit+')':base;}"
        "  function ensureChart(key,payload){if(charts[key]){const title=chartTitle(key,payload);if(title&&charts[key].titleEl.textContent!==title){charts[key].titleEl.textContent=title;}return charts[key];}"
        "    const def=chartDefs[key]||{};const labels=payload&&payload.labels?payload.labels:def.labels||['X','Y','Z'];const colors=(payload&&payload.colors)||def.colors||defaultColors;"
        "    const box=document.createElement('div');box.className='chart-box';const titleEl=document.createElement('h3');titleEl.textContent=chartTitle(key,payload);box.appendChild(titleEl);const canvas=document.createElement('canvas');box.appendChild(canvas);"
        "    chartsContainer.appendChild(box);"
        "    const datasets=labels.map((label,idx)=>({label,data:[],borderColor:colors[idx%colors.length],borderWidth:1.8,pointRadius:0}));"
        "    const chart=new Chart(canvas.getContext('2d'),{...chartCfg,data:{labels:[],datasets}});"
        "    charts[key]={chart,titleEl};"
        "    addLog('Created chart for '+titleEl.textContent);"
        "    metrics.sensors.textContent=Object.keys(charts).length;"
        "    return charts[key];"
        "  }"
        "  function pushValues(key,payload){const values=payload&&payload.values?payload.values:payload;if(!values||!values.length)return;const entry=ensureChart(key,payload);const chart=entry.chart;"
        "    chart.data.labels.push(msgCount);chart.data.datasets.forEach((ds,idx)=>ds.data.push(values[idx]));"
        "    if(chart.data.labels.length>maxPoints){chart.data.labels.shift();chart.data.datasets.forEach(ds=>ds.data.shift());}"
        "    chart.update('none');"
        "  }"
        "  addLog('ESP32 IP: '+ESP32_IP);"
        "  addLog('Waiting for sensor data...');"
        "  const wsUrl=(location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws/data';"
        "  addLog('Connecting to '+wsUrl);"
        "  const ws=new WebSocket(wsUrl);"
        "  ws.onopen=()=>{"
        "    addLog('WebSocket connected');"
        "    statusText.textContent='Connected';"
        "    statusEl.style.borderLeftColor='#10b981';"
        "    statusEl.style.background='#fff';"
        "  };"
        "  ws.onerror=()=>{addLog('WebSocket error');statusText.textContent='Error';statusEl.style.borderLeftColor='#ef4444';statusEl.style.background='#fef2f2';};"
        "  ws.onclose=()=>{addLog('WebSocket closed');statusText.textContent='Disconnected';statusEl.style.borderLeftColor='#f59e0b';statusEl.style.background='#fffbeb';};"
        "  ws.onmessage=(ev)=>{"
        "    try{"
        "      const payload=JSON.parse(ev.data);"
        "      if(payload.ip){statusIp.textContent=payload.ip;return;}"
        "      msgCount++;"
        "      metrics.msg.textContent=msgCount;"
        "      if(payload&&payload.statistics&&payload.statistics.msg_per_second!==undefined){metrics.msgRate.textContent=payload.statistics.msg_per_second.toFixed(1);}"
        "      if(payload.mag_iis2){pushValues('mag_iis2',{values:[payload.mag_iis2.x,payload.mag_iis2.y,payload.mag_iis2.z],name:payload.mag_iis2.name,unit:payload.mag_iis2.unit});}"
        "      if(payload.acc_iis3_g){pushValues('acc_iis3_g',{values:[payload.acc_iis3_g.x,payload.acc_iis3_g.y,payload.acc_iis3_g.z],name:payload.acc_iis3_g.name,unit:payload.acc_iis3_g.unit});}"
        "      if(payload.acc_iis3_ms2){pushValues('acc_iis3_ms2',{values:[payload.acc_iis3_ms2.x,payload.acc_iis3_ms2.y,payload.acc_iis3_ms2.z],name:payload.acc_iis3_ms2.name,unit:payload.acc_iis3_ms2.unit});}"
        "      if(payload.gyr_icm){pushValues('gyr_icm',{values:[payload.gyr_icm.x,payload.gyr_icm.y,payload.gyr_icm.z],name:payload.gyr_icm.name,unit:payload.gyr_icm.unit});}"
        "      if(payload.inc_scl){pushValues('inc_scl',{values:[payload.inc_scl.angle_x,payload.inc_scl.angle_y,payload.inc_scl.angle_z],name:payload.inc_scl.name,unit:payload.inc_scl.unit,labels:['Angle X','Angle Y','Angle Z']});}"
        "      if(msgCount%25===0){addLog('Received '+msgCount+' messages');}"
        "    }catch(err){addLog('Parse error: '+err.message);}"
        "  };"
        "})();"
        "</script>"
        "</body></html>";
    
    // Send HTML in chunks with IP injected twice (in title and as JavaScript)
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, html_part1, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, ip_str, HTTPD_RESP_USE_STRLEN);  // IP trong tiêu đề
    httpd_resp_send_chunk(req, html_ip_part, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, html_part2, HTTPD_RESP_USE_STRLEN); 
    httpd_resp_send_chunk(req, NULL, 0); // End of chunks
    
    return ESP_OK;
}
