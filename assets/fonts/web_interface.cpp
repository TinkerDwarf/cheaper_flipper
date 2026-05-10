#include <WebServer.h>
#include "web_interface.h"
#include "definitions.h"
#include "deauth.h"

WebServer server(80);
int num_networks;
void handle_rescan() {
//  num_networks = WiFi.scanNetworks();
}

void handle_root() {
  for (int i = 0; i < num_networks; i++) {
     String(i)+"." + WiFi.SSID(i) + "        " + String(WiFi.RSSI(i));
  }
}


void handle_deauth() {
  int wifi_number = server.arg("net_num").toInt();
  uint16_t reason = 2;
  if (wifi_number < num_networks) {
    start_deauth(wifi_number, DEAUTH_TYPE_SINGLE, reason);
  }
}

void handle_deauth_all() {
  uint16_t reason = 2;
  start_deauth(0, DEAUTH_TYPE_ALL, reason);
}

void handle_stop() {
  stop_deauth();
}

void start_web_interface() {
  server.on("/", handle_root);
  server.on("/deauth", handle_deauth);
  server.on("/deauth_all", handle_deauth_all);
  server.on("/rescan", handle_rescan);
  server.on("/stop", handle_stop);

  server.begin();
}

void web_interface_handle_client() {
  server.handleClient();
}

// The function implementation can stay where it is
String getEncryptionType(wifi_auth_mode_t encryptionType) {
  switch (encryptionType) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2_ENTERPRISE";
    default:
      return "UNKNOWN";
  }
}
