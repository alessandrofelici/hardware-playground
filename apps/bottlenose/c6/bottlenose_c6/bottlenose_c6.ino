/* bottlenose_c6 -- ESP32-C6 firmware for the Bottlenose WiFi Orca, the
 * companion of the FreeWili OG app in ../.. (apps/bottlenose).
 *
 * Joins Wi-Fi, gets the time over NTP, fetches one small URL over plain HTTP,
 * and reports all of it to the FreeWili as text lines. Since 1.3 it also
 * receives finished rootclock games from the FreeWili, saves each one to its
 * own flash (LittleFS) and uploads it to root-metrics (RESULTS_URL in
 * secrets.h), retrying until the server has it -- so a game survives no
 * Wi-Fi, a server outage or a power-off. Every line goes out on two ports:
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
 *   QUEUE <n>                        games saved here, not yet uploaded
 *   RESULT QUEUED <id> <why>         saved to flash (why: saved | net |
 *                                    http<code>); uploading / will retry
 *   RESULT OK <id> <http>            the server has it (201, or 200 when
 *                                    it already had it); deleted here
 *   RESULT ERR <id> <reason>         crc | len | seq: garbled, send again;
 *                                    nourl: no RESULTS_URL; fs: no flash;
 *                                    <http 4xx>: refused (401/403 = token)
 *   UPLOAD <text>                    upload progress, for people
 * Everything is re-sent every 30 s, so a display that missed it catches up.
 *
 * ---- Commands accepted, from either port ----
 *   SCAN, FETCH, STATUS, HELP, QUEUE (case-insensitive)
 *   WIFI ON | WIFI OFF   OFF switches the radio off once nothing is waiting
 *                        to upload; ON (the state at boot) joins again
 *   RESULT BEGIN <id> <bytes> <crc32 hex>   then RESULT DATA <seq> <text>
 *   lines, then RESULT END <id>: one game, sent by rootclock's main CPU.
 *   The CRC is CRC-32/IEEE of the JSON. See documents/04 in the repo. */
#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h and put your Wi-Fi name and password in it."
#endif

#include "root_cas.h"

#if !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled, so Serial is the USB port and Serial0 is the FreeWili link."
#endif

static const char *FW_VERSION = "1.3";
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

// ---- rootclock results ----
// A whole result (worst case ~4.7 KB, see documents/04) can arrive while an
// upload is blocking loop(), so the link's receive buffer holds one.
static const size_t   OG_RX_BUFFER     = 8192;
static const size_t   RESULT_MAX       = 6144;
static const char    *QUEUE_DIR        = "/q";
static const char    *BAD_DIR          = "/bad";
// Seconds between upload attempts while Wi-Fi is up: 5, 15, 60, then every
// 5 minutes. A new game or WIFI ON tries again at once.
static const uint32_t RETRY_S[]        = {5, 15, 60, 300};
static const uint32_t NTP_WAIT_MS      = 30000;  // HTTPS needs the time
static const uint32_t RX_STALE_MS      = 5000;   // a result cut off mid-way

static bool     s_fs_ok;
static bool     s_wifi_wanted = true;    // WIFI ON/OFF; joins at boot
static bool     s_radio_off;             // the radio is actually off
static uint32_t s_next_try;              // millis() of the next upload
static unsigned s_tries;
static bool     s_paused;                // 401/403: wrong token, stop trying
static uint32_t s_up_since;
static unsigned s_seq_counter;           // file-name order of saved results
static unsigned s_queue_n;               // saved results (cached count)
static char     s_body[RESULT_MAX + 1];  // one result, receive or upload

static struct {
  bool     active;
  char     id[33];
  size_t   bytes;
  uint32_t crc;
  size_t   len;
  unsigned next_seq;
  bool     bad;
  uint32_t last_ms;
} s_rx;

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
  emit_queue();
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
  if (s_radio_off) {
    emit("SCAN skipped: Wi-Fi is off (WIFI ON)");
    return;
  }
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

// ---- rootclock results: receive, save, upload ----

static uint32_t crc32_ieee(const uint8_t *p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
  }
  return crc ^ 0xFFFFFFFFu;
}

// Saved results are /q/<6-digit order>_<game id>.json, oldest first.
static unsigned queue_count(String *oldest = nullptr) {
  if (!s_fs_ok) return 0;
  File dir = LittleFS.open(QUEUE_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  unsigned n = 0;
  String best;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    if (!name.endsWith(".json")) continue;
    n++;
    if (best.length() == 0 || name < best) best = name;
  }
  if (oldest) *oldest = best;
  return n;
}

// Re-counts the saved results (only when they change) and reports it.
static void emit_queue() {
  s_queue_n = queue_count();
  emit("QUEUE %u", s_queue_n);
}

static void fs_init() {
  s_fs_ok = LittleFS.begin(true);  // formats the partition on first use
  if (!s_fs_ok) {
    emit("UPLOAD no flash filesystem: games cannot be saved");
    return;
  }
  if (!LittleFS.exists(QUEUE_DIR)) LittleFS.mkdir(QUEUE_DIR);
  if (!LittleFS.exists(BAD_DIR)) LittleFS.mkdir(BAD_DIR);
  File dir = LittleFS.open(QUEUE_DIR);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const unsigned order = (unsigned)atoi(f.name());
    if (order >= s_seq_counter) s_seq_counter = order + 1;
  }
}

static void radio_off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  s_radio_off = true;
  wifi_state("off");
}

static void radio_on() {
  if (!s_radio_off) return;
  s_radio_off = false;
  WiFi.mode(WIFI_STA);
  join();
}

static void result_line(const String &line) {
  // RESULT BEGIN <id> <bytes> <crc> | RESULT DATA <seq> <text> | RESULT END <id>
  const int a = line.indexOf(' ');
  const int b = line.indexOf(' ', a + 1);
  if (a < 0 || b < 0) return;
  const String verb = line.substring(a + 1, b);
  const String rest = line.substring(b + 1);
  s_rx.last_ms = millis();
  if (verb == "BEGIN") {
    char id[33] = {0};
    unsigned long bytes = 0, crc = 0;
    if (sscanf(rest.c_str(), "%32s %lu %lx", id, &bytes, &crc) != 3) return;
    memset(&s_rx, 0, sizeof s_rx);
    s_rx.last_ms = millis();
    strncpy(s_rx.id, id, sizeof s_rx.id - 1);
    s_rx.bytes = bytes;
    s_rx.crc = (uint32_t)crc;
    s_rx.active = true;
    if (bytes > RESULT_MAX) s_rx.bad = true;
  } else if (verb == "DATA" && s_rx.active) {
    const int sp = rest.indexOf(' ');
    if (sp < 0) { s_rx.bad = true; return; }
    if ((unsigned)rest.substring(0, sp).toInt() != s_rx.next_seq) s_rx.bad = true;
    s_rx.next_seq++;
    const String text = rest.substring(sp + 1);
    if (s_rx.len + text.length() > RESULT_MAX) { s_rx.bad = true; return; }
    memcpy(s_body + s_rx.len, text.c_str(), text.length());
    s_rx.len += text.length();
  } else if (verb == "END" && s_rx.active) {
    s_rx.active = false;
    const char *id = s_rx.id;
    if (s_rx.bad) { emit("RESULT ERR %s seq", id); return; }
    if (s_rx.len != s_rx.bytes) { emit("RESULT ERR %s len", id); return; }
    if (crc32_ieee((const uint8_t *)s_body, s_rx.len) != s_rx.crc) {
      emit("RESULT ERR %s crc", id);
      return;
    }
#ifndef RESULTS_URL
    emit("RESULT ERR %s nourl", id);
    return;
#endif
    if (!s_fs_ok) { emit("RESULT ERR %s fs", id); return; }
    char path[64];
    snprintf(path, sizeof path, "%s/%06u_%s.json", QUEUE_DIR, s_seq_counter++, id);
    File f = LittleFS.open(path, "w");
    const bool ok = f && f.write((const uint8_t *)s_body, s_rx.len) == s_rx.len;
    if (f) f.close();
    if (!ok) { LittleFS.remove(path); emit("RESULT ERR %s fs", id); return; }
    emit("RESULT QUEUED %s saved", id);
    emit_queue();
    s_next_try = millis();
    s_tries = 0;
    s_paused = false;
    radio_on();   // a game to send needs the radio, whatever WIFI OFF said
  }
}

// POST one saved result. Returns the HTTP status, or a negative HTTPClient
// error when there was no answer.
static int post_body(const char *body, size_t len) {
#ifdef RESULTS_URL
  const String url = RESULTS_URL;
  WiFiClient plain;
  WiFiClientSecure tls;
  const bool https = url.startsWith("https://");
  if (https) {
#ifdef RESULTS_TLS_INSECURE
    tls.setInsecure();
#else
    tls.setCACert(ROOT_CAS);
#endif
  }
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!(https ? http.begin(tls, url) : http.begin(plain, url))) return HTTPC_ERROR_CONNECTION_REFUSED;
  http.addHeader("Content-Type", "application/json");
#ifdef RESULTS_TOKEN
  http.addHeader("Authorization", String("Bearer ") + RESULTS_TOKEN);
#endif
  const int code = http.POST((uint8_t *)body, len);
  http.end();
  return code;
#else
  (void)body;
  (void)len;
  return HTTPC_ERROR_CONNECTION_REFUSED;
#endif
}

static void upload_tick(uint32_t now, bool up) {
#ifdef RESULTS_URL
  // s_body also holds a result being received: never upload in the middle.
  if (s_rx.active || s_queue_n == 0) return;
  if (!up || s_paused || !s_fs_ok || (int32_t)(now - s_next_try) < 0) return;
  // HTTPS checks certificate dates: give NTP a little time first.
  if (String(RESULTS_URL).startsWith("https://") && time(nullptr) < 1700000000 &&
      now - s_up_since < NTP_WAIT_MS) {
    return;
  }
  String name;
  if (queue_count(&name) == 0) return;
  const String path = String(QUEUE_DIR) + "/" + name;
  String id = name.substring(name.indexOf('_') + 1);
  id.remove(id.length() - 5);  // ".json"
  File f = LittleFS.open(path, "r");
  const size_t len = f ? f.read((uint8_t *)s_body, RESULT_MAX) : 0;
  if (f) f.close();
  if (len == 0) {  // unreadable: set it aside rather than loop on it
    LittleFS.rename(path, String(BAD_DIR) + "/" + name);
    emit_queue();
    return;
  }
  emit("UPLOAD %s try %u", id.c_str(), s_tries + 1);
  const uint32_t t0 = millis();
  const int code = post_body(s_body, len);
  const uint32_t ms = millis() - t0;
  if (code == 200 || code == 201) {
    LittleFS.remove(path);
    emit("RESULT OK %s %d", id.c_str(), code);
    s_tries = 0;
    s_next_try = millis();  // the next one, if any, at once
  } else if (code == 401 || code == 403) {
    emit("RESULT ERR %s %d", id.c_str(), code);
    emit("UPLOAD refused: RESULTS_TOKEN does not match the server's INGEST_TOKEN");
    s_paused = true;  // keep the games; a reflash with the right token sends them
  } else if (code >= 400 && code < 500 && code != 408 && code != 429) {
    LittleFS.rename(path, String(BAD_DIR) + "/" + name);
    emit("RESULT ERR %s %d", id.c_str(), code);
  } else {
    if (code > 0) {
      emit("RESULT QUEUED %s http%d", id.c_str(), code);
    } else {
      emit("UPLOAD %s failed in %lu ms: %s", id.c_str(), (unsigned long)ms,
           HTTPClient::errorToString(code).c_str());
      emit("RESULT QUEUED %s net", id.c_str());
    }
    const size_t n = sizeof RETRY_S / sizeof RETRY_S[0];
    s_next_try = millis() + RETRY_S[s_tries < n ? s_tries : n - 1] * 1000u;
    s_tries++;
  }
  emit_queue();
#else
  (void)now;
  (void)up;
#endif
}

static void handle_cmd(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  // RESULT lines carry JSON: never change their case.
  if (cmd.startsWith("RESULT ")) {
    result_line(cmd);
    return;
  }
  cmd.toUpperCase();
  if (cmd == "SCAN")          do_scan();
  else if (cmd == "FETCH")    do_fetch();
  else if (cmd == "STATUS")   emit_status();
  else if (cmd == "QUEUE")    emit_queue();
  else if (cmd == "WIFI ON")  { s_wifi_wanted = true; radio_on(); s_next_try = millis(); }
  else if (cmd == "WIFI OFF") s_wifi_wanted = false;  // loop() turns it off when idle
  else if (cmd == "HELP")     emit("HELP commands: SCAN FETCH STATUS QUEUE WIFI ON|OFF RESULT ...");
  else                        emit("ERR unknown command: %s", cmd.c_str());
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
  OG.setRxBufferSize(OG_RX_BUFFER);  // must come before begin()
  OG.begin(OG_BAUD, SERIAL_8N1, OG_RX_PIN, OG_TX_PIN);
  // Bringing the UART up can put a junk byte on the line; end it on a line
  // of its own so it does not prefix HELLO.
  OG.print('\n');
  // ESP-IDF logs to UART0 too, which would interleave with our lines.
  esp_log_level_set("*", ESP_LOG_NONE);

  emit("HELLO %s", FW_VERSION);
  fs_init();
  emit_queue();
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

  const bool up = !s_radio_off && WiFi.status() == WL_CONNECTED;
  if (up && !s_was_up) {
    s_up_since = now;
    s_next_try = now;
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
  if (!up && !s_radio_off && now - s_join_started >= REJOIN_AFTER_MS) join();

  // A result whose END never came (the FreeWili reset mid-way): forget it.
  if (s_rx.active && now - s_rx.last_ms >= RX_STALE_MS) s_rx.active = false;

  // Uploads, then WIFI OFF once there is nothing left to send.
  upload_tick(now, up);
  if (!s_wifi_wanted && !s_radio_off && s_queue_n == 0 && !s_rx.active) radio_off();

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
