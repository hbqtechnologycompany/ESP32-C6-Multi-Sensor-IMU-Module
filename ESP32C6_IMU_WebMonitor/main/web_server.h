#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>

// Web server configuration
#define WEB_SERVER_PORT 80
#define WEB_SERVER_MAX_URI_HANDLERS 20
#define WEB_SERVER_STACK_SIZE 8192

// WebSocket configuration
#define WEBSOCKET_MAX_CONNECTIONS 4
#define WEBSOCKET_BUFFER_SIZE 1024

// API endpoints
#define API_BASE_PATH "/api"
#define API_DATA_PATH "/api/data"
#define API_STATS_PATH "/api/stats"
#define API_CONFIG_PATH "/api/config"
#define API_DOWNLOAD_PATH "/api/download"
#define API_IP_PATH "/api/ip"

// WebSocket endpoints
#define WS_DATA_PATH "/ws/data"
#define WS_CONTROL_PATH "/ws/control"

// Web server API
esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);
esp_err_t web_server_broadcast_data(const char *data, size_t len);
esp_err_t web_server_broadcast_stats(const char *stats, size_t len);

// WebSocket API
esp_err_t websocket_send_data(httpd_handle_t hd, int fd, const char *data, size_t len);
esp_err_t websocket_send_json(httpd_handle_t hd, int fd, const char *json_data);

// Configuration API
esp_err_t web_server_set_sampling_rate(uint32_t rate_hz);
esp_err_t web_server_set_fifo_watermark(uint16_t watermark);
esp_err_t web_server_enable_sensor(uint8_t sensor_id, bool enable);

#endif // WEB_SERVER_H
