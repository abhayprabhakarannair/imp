#include <Arduino.h>
#include <array>
#include <utility>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "esp_system.h"
#include "mqtt_client.h"
#include "ping/ping_sock.h"

#include "certs.h"
#include "config.h"
#include "site.h"
#include "src/command.h"
#include "src/led.h"
#include "src/monitor.h"
#include "src/report.h"
#include "src/wol.h"

namespace {

constexpr char kTopicMeta[] = IMP_TOPIC "/meta";
constexpr char kTopicStatus[] = IMP_TOPIC "/status";
constexpr char kTopicState[] = IMP_TOPIC "/state";
constexpr char kTopicEvent[] = IMP_TOPIC "/event";
constexpr char kTopicCmd[] = IMP_TOPIC "/cmd";

struct TargetConfig {
  const char* name;
  imp::Mac mac;
  uint8_t ip[4];
};

constexpr TargetConfig kTargets[] = IMP_TARGETS;
constexpr size_t kTargetCount = sizeof(kTargets) / sizeof(kTargets[0]);

constexpr bool targets_valid() {
  for (const auto& t : kTargets)
    if (!imp::valid_name(t.name)) return false;
  return true;
}
static_assert(kTargetCount > 0, "IMP_TARGETS is empty");
static_assert(targets_valid(), "IMP_TARGETS has an invalid name");

template <size_t... I>
std::array<imp::Monitor, sizeof...(I)> make_monitors(std::index_sequence<I...>) {
  return {((void)I, imp::Monitor(IMP_OFFLINE_AFTER, IMP_WAKE_TIMEOUT_MS))...};
}

std::array<imp::Monitor, kTargetCount> monitors = make_monitors(std::make_index_sequence<kTargetCount>{});
imp::TargetView views[kTargetCount];
SemaphoreHandle_t state_lock;

esp_mqtt_client_handle_t mqtt;
volatile bool bus_up = false;
volatile bool state_dirty = true;
QueueHandle_t commands;

struct RawCommand {
  char text[imp::kMaxPayload + 1];
};

struct Locked {
  Locked() { xSemaphoreTake(state_lock, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(state_lock); }
};

void publish(const char* topic, const char* payload, bool retain) {
  if (bus_up) esp_mqtt_client_publish(mqtt, topic, payload, 0, 1, retain ? 1 : 0);
}

const char* reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_EXT: return "reset_button";
    case ESP_RST_USB: return "usb";
    default: return "other";
  }
}

// ---- events ----

struct Event {
  char buf[192];
  imp::JsonWriter w{buf, sizeof buf};
  explicit Event(const char* name) { w.str("event", name); }
  ~Event() {
    if (w.finish()) publish(kTopicEvent, buf, false);
  }
};

void publish_meta() {
  char buf[256];
  if (imp::write_meta(buf, sizeof buf, IMP_VERSION, views, kTargetCount)) publish(kTopicMeta, buf, true);
}

void publish_state() {
  char buf[512];
  bool ok;
  {
    Locked lock;
    ok = imp::write_state(buf, sizeof buf, millis(), millis() / 1000, WiFi.RSSI(),
                          WiFi.localIP().toString().c_str(), views, kTargetCount);
  }
  if (ok) publish(kTopicState, buf, true);
}

// ---- wake ----

bool send_magic_packet(const imp::Mac& mac) {
  const imp::MagicPacket packet = imp::magic_packet(mac);
  const IPAddress destinations[] = {WiFi.broadcastIP(), IPAddress(255, 255, 255, 255)};
  constexpr uint16_t ports[] = {9, 7};
  WiFiUDP udp;
  bool sent = false;
  for (int round = 0; round < 3; ++round) {
    for (const IPAddress& dst : destinations)
      for (uint16_t port : ports)
        if (udp.beginPacket(dst, port)) {
          udp.write(packet.data(), packet.size());
          sent |= udp.endPacket() == 1;
        }
    delay(30);
  }
  return sent;
}

void wake(size_t i, std::string_view source) {
  bool seemed_on;
  {
    Locked lock;
    seemed_on = monitors[i].power() == imp::Power::Online;
  }
  const bool sent = WiFi.isConnected() && send_magic_packet(kTargets[i].mac);
  if (sent) {
    Locked lock;
    monitors[i].on_wake_sent(millis());
  }
  Event e(sent ? "wake_sent" : "wake_failed");
  e.w.str("target", kTargets[i].name).str("source", source.empty() ? "mqtt" : source);
  if (seemed_on) e.w.flag("seemed_on", true);
  state_dirty = true;
}

int target_index(std::string_view name) {
  for (size_t i = 0; i < kTargetCount; ++i)
    if (name == kTargets[i].name) return static_cast<int>(i);
  return -1;
}

void handle(const char* text) {
  imp::Command cmd{};
  if (!imp::parse(text, cmd)) {
    Event("rejected").w.str("reason", "malformed");
    return;
  }
  switch (cmd.action) {
    case imp::Action::Wake: {
      const int i = target_index(cmd.target);
      if (i < 0) {
        Event("rejected").w.str("reason", "unknown target").str("target", cmd.target);
        return;
      }
      wake(static_cast<size_t>(i), cmd.source);
      return;
    }
    case imp::Action::Refresh:
      publish_meta();
      state_dirty = true;
      return;
    case imp::Action::Restart:
      { Event("restarting"); }
      delay(500);
      ESP.restart();
  }
}

// ---- monitoring ----

struct PingResult {
  SemaphoreHandle_t done;
  uint32_t replies;
};

bool ping(const uint8_t ip[4]) {
  PingResult result{xSemaphoreCreateBinary(), 0};
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  IP_ADDR4(&cfg.target_addr, ip[0], ip[1], ip[2], ip[3]);
  cfg.count = 2;
  cfg.interval_ms = 300;
  cfg.timeout_ms = 1000;
  esp_ping_callbacks_t cbs = {};
  cbs.cb_args = &result;
  cbs.on_ping_success = [](esp_ping_handle_t, void* r) { static_cast<PingResult*>(r)->replies++; };
  cbs.on_ping_end = [](esp_ping_handle_t, void* r) { xSemaphoreGive(static_cast<PingResult*>(r)->done); };

  esp_ping_handle_t session;
  bool reachable = false;
  if (esp_ping_new_session(&cfg, &cbs, &session) == ESP_OK) {
    esp_ping_start(session);
    xSemaphoreTake(result.done, pdMS_TO_TICKS(5000));
    esp_ping_stop(session);
    esp_ping_delete_session(session);
    reachable = result.replies > 0;
  }
  vSemaphoreDelete(result.done);
  return reachable;
}

void report(size_t i, const imp::Transition& t) {
  if (t.kind == imp::Transition::None) return;
  state_dirty = true;
  const char* name = t.kind == imp::Transition::Up     ? "up"
                     : t.kind == imp::Transition::Down ? "down"
                                                       : "wake_timeout";
  Event e(name);
  e.w.str("target", kTargets[i].name);
  if (t.after_wake) e.w.num("after_s", t.took_ms / 1000);
}

void monitor_task(void*) {
  for (;;) {
    bool any_waking = false;
    if (WiFi.isConnected()) {
      for (size_t i = 0; i < kTargetCount; ++i) {
        const bool up = ping(kTargets[i].ip);
        imp::Transition seen, timeout;
        {
          Locked lock;
          seen = monitors[i].on_ping(up, millis());
          timeout = monitors[i].tick(millis());
          any_waking |= monitors[i].waking();
        }
        report(i, seen);
        report(i, timeout);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(any_waking ? IMP_PING_INTERVAL_WAKING_MS : IMP_PING_INTERVAL_MS));
  }
}

// ---- bus ----

void on_mqtt(void*, esp_event_base_t, int32_t id, void* data) {
  auto* e = static_cast<esp_mqtt_event_handle_t>(data);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
      bus_up = true;
      esp_mqtt_client_publish(mqtt, kTopicStatus, "online", 0, 1, 1);
      esp_mqtt_client_subscribe(mqtt, kTopicCmd, 1);
      state_dirty = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      bus_up = false;
      break;
    case MQTT_EVENT_DATA: {
      RawCommand raw{};
      if (e->current_data_offset != 0 || e->total_data_len != e->data_len) break;
      if (e->data_len > static_cast<int>(imp::kMaxPayload)) {
        xQueueSend(commands, &raw, 0);
        break;
      }
      memcpy(raw.text, e->data, static_cast<size_t>(e->data_len));
      xQueueSend(commands, &raw, 0);
      break;
    }
    case MQTT_EVENT_ERROR:
      if (!e->error_handle) break;
      if (e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        Serial.printf("[bus] refused (%d)\n", e->error_handle->connect_return_code);
      else if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        Serial.printf("[bus] transport error tls=0x%x stack=0x%x\n", e->error_handle->esp_tls_last_esp_err,
                      e->error_handle->esp_tls_stack_err);
      break;
    default:
      break;
  }
}

void start_bus() {
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = IMP_MQTT_URI;
  cfg.broker.verification.certificate = kBrokerRootCa;
  cfg.credentials.username = IMP_MQTT_USER;
  cfg.credentials.client_id = IMP_HOSTNAME;
  cfg.credentials.authentication.password = IMP_MQTT_PASS;
  cfg.session.keepalive = 30;
  cfg.session.last_will.topic = kTopicStatus;
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.reconnect_timeout_ms = 5000;
  cfg.network.timeout_ms = 15000;
  mqtt = esp_mqtt_client_init(&cfg);
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, on_mqtt, nullptr);
  esp_mqtt_client_start(mqtt);
}

void start_ota() {
  ArduinoOTA.setHostname(IMP_HOSTNAME);
  ArduinoOTA.setPassword(IMP_OTA_PASS);
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([] { Event("ota_start"); });
  ArduinoOTA.onError([](ota_error_t err) { Event("ota_failed").w.num("code", static_cast<int>(err)); });
  ArduinoOTA.begin();
}

// ---- local I/O ----

void update_led() {
  static imp::Rgb last{1, 1, 1};
  const imp::Link link = !WiFi.isConnected() ? imp::Link::NoWifi : bus_up ? imp::Link::Up : imp::Link::NoBus;
  imp::Power primary;
  {
    Locked lock;
    primary = monitors[0].power();
  }
  const imp::Rgb c = imp::led(link, primary, millis(), IMP_LED_LEVEL);
  if (c == last) return;
  last = c;
  rgbLedWrite(IMP_LED_PIN, c.r, c.g, c.b);
}

void poll_button() {
  static bool last = HIGH, stable = HIGH;
  static uint32_t changed_at = 0;
  const bool now = digitalRead(IMP_BUTTON_PIN);
  if (now != last) {
    last = now;
    changed_at = millis();
  }
  if (millis() - changed_at > 40 && now != stable) {
    stable = now;
    if (stable == LOW) wake(0, "button");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(IMP_BUTTON_PIN, INPUT_PULLUP);
  state_lock = xSemaphoreCreateMutex();
  commands = xQueueCreate(8, sizeof(RawCommand));
  for (size_t i = 0; i < kTargetCount; ++i) views[i] = {kTargets[i].name, &monitors[i]};

  WiFi.setHostname(IMP_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(IMP_WIFI_SSID, IMP_WIFI_PASS);

  start_bus();
  xTaskCreatePinnedToCore(monitor_task, "monitor", 4096, nullptr, 1, nullptr, 0);
}

void loop() {
  static bool network_ready = false, announced = false;
  static uint32_t wifi_lost_at = 0, state_at = 0;

  if (WiFi.isConnected()) {
    wifi_lost_at = 0;
    if (!network_ready) {
      network_ready = true;
      MDNS.begin(IMP_HOSTNAME);
      start_ota();
    }
  } else {
    if (!wifi_lost_at) wifi_lost_at = millis();
    if (millis() - wifi_lost_at > IMP_WIFI_RESTART_MS) ESP.restart();
  }

  if (bus_up && !announced) {
    announced = true;
    publish_meta();
    Event("boot").w.str("version", IMP_VERSION).str("reset", reset_reason());
  }

  RawCommand raw;
  while (xQueueReceive(commands, &raw, 0) == pdTRUE) handle(raw.text);

  if (bus_up && (state_dirty || millis() - state_at > IMP_STATE_EVERY_MS)) {
    state_dirty = false;
    state_at = millis();
    publish_state();
  }

  ArduinoOTA.handle();
  poll_button();
  update_led();
  delay(5);
}
