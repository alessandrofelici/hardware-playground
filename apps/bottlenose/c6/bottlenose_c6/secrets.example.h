// Copy this file to secrets.h in the same folder and fill in your network.
// secrets.h is git-ignored. The ESP32-C6 is 2.4 GHz only: a 5 GHz-only
// network will show up as "down: not found".
//
// The name must match byte for byte. Phones name their hotspot with a curly
// apostrophe ("Sam’s iPhone"), which is not the ' on a keyboard; write it
// as "Sam\xE2\x80\x99" "s iPhone". On Windows, `netsh wlan show networks`
// shows the exact name and the band.
#define WIFI_SSID "your-network-name"
#define WIFI_PASS "your-password"
