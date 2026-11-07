// Copy this file to secrets.h (which git ignores) and fill it in.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// The access token: the settings page and tools/push.py send it as the X-Token header for every
// change (settings, OTA updates, reboot). Use a long random string, e.g.
//   python -c "import secrets; print(secrets.token_urlsafe(24))"
#define OTA_TOKEN "change-me"
