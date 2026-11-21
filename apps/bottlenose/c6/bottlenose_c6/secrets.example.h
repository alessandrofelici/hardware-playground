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

// rootclock uploads (sketch 1.3+). Where finished games go: root-metrics'
// POST /api/results, and the INGEST_TOKEN set on that server. Leave
// RESULTS_URL out to turn uploads off (the board then says so).
//   Hosted on Vercel:  "https://<your-app>.vercel.app/api/results"
//   Docker on the LAN: "http://192.168.1.50:8080/api/results"
#define RESULTS_URL   "https://your-app.vercel.app/api/results"
#define RESULTS_TOKEN "the-same-value-as-INGEST_TOKEN-on-the-server"

// Only while testing a server whose certificate root_cas.h does not cover:
// skips the HTTPS certificate check. Do not leave it on.
// #define RESULTS_TLS_INSECURE 1
