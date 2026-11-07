#pragma once
#include "esp_http_server.h"

/* Settings page (GET /), GET /api/status, GET/POST /api/settings. */
void web_register(httpd_handle_t server);
