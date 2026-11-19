/* bottlenose_c6 -- ESP32-C6 firmware for the Bottlenose WiFi Orca, the
 * companion of the FreeWili OG app in ../.. (apps/bottlenose).
 *
 * Joins Wi-Fi, gets the time over NTP, fetches one small URL over plain HTTP,
 * and reports all of it to the FreeWili as text lines. Every line goes out
 * on two ports:
 *   Serial0 -- UART0 on GPIO16 (TX) / GPIO17 (RX), 115200 8N1: the FreeWili's
 *              main CPU, breakout UART1. This is the link.
 *   Serial  -- the Bottlenose's USB-C port, for watching it from a PC.
 *
 * Arduino IDE settings (Tools menu), with the esp32 core 3.x installed
 * (developed against 3.3.13):
 *   Board: "ESP32C6 Dev Module"
 *   USB CDC On Boot: "Enabled"   (so Serial is USB and Serial0 is the link)
 * Copy secrets.example.h to secrets.h and set your network first.
 *
 * ---- Lines sent: "KEY value", one per line ----
 *   HELLO <fw version>               at boot and on STATUS
 *   WIFI connecting | up | down: <why>   on every change, and on STATUS
 *   SSID <network>                   on STATUS
 *   IP <address>                     when Wi-Fi comes up, and on STATUS
 *   RSSI <dBm>                       every 5 s while connected
 *   TIME <YYYY-MM-DD HH:MM:SS>       every second once NTP has synced
 *   HTTP <code> in <ms> ms           after each fetch (or HTTP <error>)
 *   NET <body>                       the fetched text: your public IP
 *   SCAN <n> networks, AP <dBm> ch<n> <ssid>   after a scan
 *   ERR <text>                       an unknown command
 * Everything is re-sent every 30 s, so a display that missed it catches up.
 *
 * ---- Commands accepted, from either port ----
 *   SCAN, FETCH, STATUS, HELP (case-insensitive) */
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h and put your Wi-Fi name and password in it."
#endif

#if !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled, so Serial is the USB port and Serial0 is the FreeWili link."
#endif

static const char *FW_VERSION = "1.2";
// POSIX TZ string. "UTC0" is UTC; for example Central Europe is
// "CET-1CEST,M3.5.0,M10.5.0/3" and US Eastern is "EST5EDT,M3.2.0,M11.1.0".
static const char *TZ_INFO = "UTC0";
// Small, plain-text and plain HTTP (no TLS): answers with your public IP.
static const char *WEB_URL = "http://api.ipify.org/";

static const int OG_RX_PIN = 17;   // <- FreeWili GPIO 8, UART1 TX
static const int OG_TX_PIN = 16;   // -> FreeWili GPIO 9, UART1 RX
static const uint32_t OG_BAUD = 115200;
#define OG Serial0

static const uint32_t TIME_PERIOD_MS   = 1000;
static const uint32_t RSSI_PERIOD_MS   = 5000;
static const uint32_t STATUS_PERIOD_MS = 30000;
// The core already retries "not found" and similar on its own; this catches
// the failures it gives up on (a wrong password after the first attempt).
static const uint32_t REJOIN_AFTER_MS  = 60000;
static const int      SCAN_SHOW        = 6;

static char     s_wifi[40];            // the last WIFI state sent
static bool     s_was_up;
static uint32_t s_join_started;
static bool     s_ntp_started;
static String   s_web;                 // last fetched body, "" if none yet
static String   s_cmd_og, s_cmd_usb;

// Set by the Wi-Fi event task, reported by loop(): emitting from the event
// task would interleave with loop()'s own lines on the UART.
static volatile uint8_t s_disc_reason;

static void emit(const char *fmt, ...) {
  char buf[121];  // the FreeWili's line limit is 120
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  OG.print(buf);
  OG.print('\n');
  Serial.print(buf);
  Serial.print('\n');
}

// Sends WIFI <text> when it differs from what was last sent.
static void wifi_state(const char *text) {
  if (strcmp(text, s_wifi) == 0) return;
  strncpy(s_wifi, text, sizeof s_wifi - 1);
  s_wifi[sizeof s_wifi - 1] = '\0';
  emit("WIFI %s", s_wifi);
}

// Our own leaving: join()'s disconnect, and the one the Arduino core makes
// before each of its own retries. Not a reason the network gave.
static bool is_own_leave(uint8_t r) {
  return r == WIFI_REASON_ASSOC_LEAVE || r == WIFI_REASON_STA_LEAVING;
}

// Why the driver dropped or refused the connection.
static const char *reason_text(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND:                       return "down: not found";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "down: security";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:            return "down: password?";
    case WIFI_REASON_BEACON_TIMEOUT:                    return "down: lost";
    default: {
      static char buf[24];
      snprintf(buf, sizeof buf, "down: reason %u", (unsigned)r);
      return buf;
    }
  }
}

static void join() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  s_join_started = millis();
  wifi_state("connecting");
}

static void emit_time() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return;  // not synced yet: still 1970
  struct tm tm;
  localtime_r(&now, &tm);
  char buf[24];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
  emit("TIME %s", buf);
}

static void emit_status() {
  const bool up = WiFi.status() == WL_CONNECTED;
  emit("HELLO %s", FW_VERSION);
  emit("WIFI %s", s_wifi[0] ? s_wifi : "connecting");
  emit("SSID %s", WIFI_SSID);
  if (up) {
    emit("IP %s", WiFi.localIP().toString().c_str());
    emit("RSSI %d", (int)WiFi.RSSI());
  }
  if (s_web.length() > 0) emit("NET %s", s_web.c_str());
  emit_time();
}

// A network name compared loosely: case, and the curly apostrophe phones put
// in names like "Sam’s iPhone" (UTF-8 E2 80 98/99), are the usual ways a typed
// name differs from the real one.
static String loose(const String &s) {
  String out;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    if (c == 0xE2 && i + 2 < s.length() && (uint8_t)s[i + 1] == 0x80 &&
        ((uint8_t)s[i + 2] == 0x98 || (uint8_t)s[i + 2] == 0x99)) {
      out += '\'';
      i += 2;
    } else {
      out += (char)tolower(c);
    }
  }
  out.trim();
  return out;
}

// The name byte for byte, with anything outside ASCII as \xNN, so a curly
// apostrophe shows up as one instead of as '?'.
static String escaped(const String &s) {
  String out;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = (uint8_t)s[i];
    if (c >= 0x20 && c <= 0x7E) {
      out += (char)c;
    } else {
      char hex[5];
      snprintf(hex, sizeof hex, "\\x%02x", c);
      out += hex;
    }
  }
  return out;
}

// Whether the configured network was in the scan. It may be too weak to make
// the strongest-first list, so look through all of them.
static void scan_report_mine(int n) {
  const String want = loose(WIFI_SSID);
  int mine = -1, close = -1;
  for (int i = 0; i < n; i++) {
    const String s = WiFi.SSID(i);
    if (s == WIFI_SSID) {
      if (mine < 0 || WiFi.RSSI(i) > WiFi.RSSI(mine)) mine = i;
    } else if (close < 0 && loose(s) == want) {
      close = i;
    }
  }
  if (mine >= 0) {
    emit("SCAN yours: %d dBm ch%d", (int)WiFi.RSSI(mine), (int)WiFi.channel(mine));
  } else if (close >= 0) {
    emit("SCAN yours: not seen; close: %s", escaped(WiFi.SSID(close)).c_str());
  } else {
    emit("SCAN yours: not seen (off, or 5 GHz only?)");
  }
}

static void do_scan() {
  emit("SCAN start");
  // A scan fails (-2) while a join attempt is in progress, so pause it.
  const bool joining = WiFi.status() != WL_CONNECTED;
  if (joining) WiFi.disconnect();
  const int n = WiFi.scanNetworks();  // blocks for a few seconds
  if (n < 0) {
    emit("SCAN failed (%d)", n);
  } else {
    emit("SCAN %d networks", n);
    // Strongest first. Printed in order, then each one marked as used.
    bool used[64] = {false};
    const int count = n < 64 ? n : 64;
    for (int shown = 0; shown < SCAN_SHOW && shown < count; shown++) {
      int best = -1;
      for (int i = 0; i < count; i++) {
        if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
      }
      used[best] = true;
      const String ssid = WiFi.SSID(best);
      emit("AP %d ch%d %s", (int)WiFi.RSSI(best), (int)WiFi.channel(best),
           ssid.length() > 0 ? ssid.c_str() : "(hidden)");
    }
    scan_report_mine(n);
    WiFi.scanDelete();
  }
  if (joining) join();
}

static void do_fetch() {
  if (WiFi.status() != WL_CONNECTED) {
    emit("HTTP skipped: Wi-Fi not up");
    return;
  }
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(WEB_URL)) {
    emit("HTTP failed: bad URL");
    return;
  }
  const uint32_t t0 = millis();
  const int code = http.GET();
  const uint32_t ms = millis() - t0;
  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    body.trim();
    if (body.length() > 60) body = body.substring(0, 60);
    emit("HTTP 200 in %lu ms", (unsigned long)ms);
    s_web = body;
    emit("NET %s", s_web.c_str());
  } else if (code > 0) {
    emit("HTTP %d in %lu ms", code, (unsigned long)ms);
  } else {
    emit("HTTP failed: %s", HTTPClient::errorToString(code).c_str());
  }
  http.end();
}

static void handle_cmd(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;
  if (cmd == "SCAN")        do_scan();
  else if (cmd == "FETCH")  do_fetch();
  else if (cmd == "STATUS") emit_status();
  else if (cmd == "HELP")   emit("HELP commands: SCAN FETCH STATUS");
  else                      emit("ERR unknown command: %s", cmd.c_str());
}

static void poll_cmds(Stream &in, String &buf) {
  while (in.available() > 0) {
    const char c = (char)in.read();
    if (c == '\n' || c == '\r') {
      if (buf.length() > 0) handle_cmd(buf);
      buf = "";
    } else if (buf.length() < 80) {
      buf += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never stall when no PC is listening on USB
  OG.begin(OG_BAUD, SERIAL_8N1, OG_RX_PIN, OG_TX_PIN);
  // Bringing the UART up can put a junk byte on the line; end it on a line
  // of its own so it does not prefix HELLO.
  OG.print('\n');
  // ESP-IDF logs to UART0 too, which would interleave with our lines.
  esp_log_level_set("*", ESP_LOG_NONE);

  emit("HELLO %s", FW_VERSION);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    // Every retry starts with a leave of our own; keeping only the latest
    // event would let that overwrite the reason the retry was for.
    const uint8_t r = info.wifi_sta_disconnected.reason;
    if (!is_own_leave(r)) s_disc_reason = r;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Scanning needs no password, so it proves the radio before anything else.
  do_scan();
  emit("SSID %s", WIFI_SSID);
  join();
}

void loop() {
  static uint32_t next_time, next_rssi, next_status;
  const uint32_t now = millis();

  poll_cmds(OG, s_cmd_og);
  poll_cmds(Serial, s_cmd_usb);

  const uint8_t reason = s_disc_reason;
  if (reason != 0) {
    s_disc_reason = 0;
    wifi_state(reason_text(reason));
  }

  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !s_was_up) {
    wifi_state("up");
    emit("IP %s", WiFi.localIP().toString().c_str());
    emit("RSSI %d", (int)WiFi.RSSI());
    if (!s_ntp_started) {
      configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
      s_ntp_started = true;
    }
    do_fetch();
  }
  s_was_up = up;
  // Auto-reconnect does not retry every failure, so start over if a join
  // has been going nowhere for a while. (join() sends "connecting"; if the
  // network still is not there, the next reason follows within seconds.)
  if (!up && now - s_join_started >= REJOIN_AFTER_MS) join();

  if ((int32_t)(now - next_time) >= 0) {
    next_time = now + TIME_PERIOD_MS;
    emit_time();
  }
  if (up && (int32_t)(now - next_rssi) >= 0) {
    next_rssi = now + RSSI_PERIOD_MS;
    emit("RSSI %d", (int)WiFi.RSSI());
  }
  if ((int32_t)(now - next_status) >= 0) {
    next_status = now + STATUS_PERIOD_MS;
    emit_status();
  }
  delay(10);
}
