#pragma once
/* Bits of main.c that other modules (the web UI) need. */
#include <stdbool.h>
#include "esp_http_server.h"

const char *app_fw_version(void);
const char *app_ip(void);
int app_rssi(void);
bool http_authorized(httpd_req_t *req);   /* checks X-Token; sends 403 and returns false if wrong */
const char *app_token(void);              /* the access token: only for a pairing approved on the device */
