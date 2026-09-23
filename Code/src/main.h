#pragma once
#include <Arduino.h>
#include <ArduinoOTA.h>
// #include <DNSServer.h>

#ifdef ESP8266

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include "user_interface.h"

#else

#include <WebServer.h>
#include <WiFi.h>

#endif

#include <OneWire.h>
#include <DallasTemperature.h>
#include <LittleFS.h>
#include <PubSubClient.h> // ** Requires library 2.8.0 or higher ** https://github.com/knolleary/pubsubclient
#include <Ticker.h>
#include <WebSocketsServer.h>
#include <umm_malloc/umm_heap_select.h>

#include "bwc.h"
#include "config.h"
#include "util.h"
#include "bwc_debug.h"
#include "fw_update.h"


BWC *bwc = nullptr;

/**  Tickers cb function runs in interrupt context and cannot be long... */
Ticker* periodicTimer;
Ticker* startComplete_ticker;
Ticker* ntpCheck_ticker;
Ticker* updateWSTimer;
Ticker* updateMqttTimer;

/**  ...Hence these flags to do the work in normal context*/
bool periodicTimerFlag = false;
bool checkNTP_flag = false;
bool sendWSFlag = false;
bool sendMQTTFlag = false;
bool send_mqtt_cfg_needed = false;
bool gotIP_flag = false;
bool disconnected_flag = false;
/** why the station dropped, see WiFiDisconnectReason. Logged from loop() */
volatile uint8_t last_disconnect_reason = 0;
/** how long to wait after a failed attempt before trying the station again */
#define WIFI_RETRY_MS 5000
/** retry anyway when an attempt produced no event at all within this time */
#define WIFI_WATCHDOG_MS 20000
/** when the next station connect attempt is due */
uint32_t next_wifi_retry = 0;
/** when we last called WiFi.begin() */
uint32_t last_wifi_attempt = 0;

/*
 * Timeline of the first station connection after boot, one line per boot in
 * wifilog.txt. Only the serial port showed why a power cycle sometimes took
 * minutes to get back online; this keeps it.
 * A boot that never gets online must be recorded too, so until NTP has set
 * the clock the line is rewritten to wifilog.cur now and then, and the next
 * boot moves whatever it finds there into wifilog.txt.
 * kind: B boot attempt, R retry after a disconnect, W watchdog retry,
 *       P periodic timer retry, D disconnect (with reason and the access
 *       point that dropped us), I got an IP (with RSSI)
 */
#define WIFI_LOG_EVENTS 24
/** when the array is full the first events stay, the rest is a sliding window */
#define WIFI_LOG_KEEP_FIRST 4
#define WIFI_LOG_MAX_SIZE 8192
/** first progress save of a boot that is not online yet; the gap doubles */
#define WIFI_LOG_FIRST_SAVE_MS 15000
struct wifi_log_event { uint32_t ms; char kind; uint8_t reason; int8_t rssi; uint16_t ap; };
wifi_log_event wifi_log[WIFI_LOG_EVENTS];
uint8_t wifi_log_len = 0;
/** events that were pushed out of the sliding window */
uint16_t wifi_log_dropped = 0;
bool wifi_log_done = false;
uint32_t wifi_gotip_ms = 0;
uint32_t wifi_log_next_save = WIFI_LOG_FIRST_SAVE_MS;
/** last two bytes of the BSSID in the last disconnect event */
volatile uint16_t last_disconnect_ap = 0;

int periodicTimerInterval = 60;
sWifi_info* wifi_info;

/** A file to store the uploads */
File fsUploadFile;

/** empty unless the last browser pushed firmware upload failed */
String fw_push_error;

/** a webserver object that listens on port 80 */
#if defined(ESP8266)
ESP8266WebServer *server = nullptr;
#elif defined(ESP32)
WebServer server(80);
#endif

/** a websocket object that listens on port 81 */
WebSocketsServer *webSocket = nullptr;

/** a WiFi client beeing used by the MQTT client */
WiFiClient *aWifiClient = nullptr;
/** a MQTT client */
PubSubClient *mqttClient = nullptr;
/**  */
// bool checkMqttConnection = false;

/** Count of how may times we've connected to the MQTT server since booting (should always be 1 or more) */
int mqtt_connect_count;
// bool enableMqtt = false;  /** Now use mqtt_info->useMqtt
/**  */
String prevButtonName = "";
/**  */
bool prevunit = 1;
/**  */
bool firstNtpSyncAfterBoot = true;

void cb_gotIP(const WiFiEventStationModeGotIP &event);
void gotIP();
void cb_disconnected(const WiFiEventStationModeDisconnected &event);
void sendWS();
void getOtherInfo(String &rtn);
void sendMQTT();
void sendMQTTConfig();
void startWiFi();
void wifi_manual_reconnect(char why = 'P');
void wifi_log_add(char kind, uint8_t reason = 0, int8_t rssi = 0, uint16_t ap = 0);
String wifiLogLine(bool final);
void rotateWifiLog();
void carryOverWifiLog();
void saveWifiLogProgress();
void saveWifiLog();
void startSoftAp();
void checkNTP_ISR();
void checkNTP();
void startNTP();
void startOTA();
void stopall();
void pause_all(bool action);
void startWebSocket();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t len);
void startHttpServer();
void handleGetHardware();
void handleSetHardware();
void handleHWtest();
void handleNotFound();
String getContentType(const String& filename);
bool handleFileRead(String path);
bool checkHttpPost(HTTPMethod method);
void handleGetConfig();
void handleSetConfig();
void handleGetCommandQueue();
void handleAddCommand();
void handleEditCommand();
void handleDelCommand();
void handle_cmdq_file();
void copyFile(String source, String dest);
void loadWebConfig();
void saveWebConfig();
void handleGetWebConfig();
void handleSetWebConfig();
void loadWifi();
void saveWifi();
void handleGetWifi();
void handleSetWifi();
void handleResetWifi();
void resetWiFi();
void loadMqtt();
void saveMqtt();
void handleGetMqtt();
void handleSetMqtt();
void handleDir();
void handleFileUpload();
void handleFileRemove();
void handleRestart();
/* firmware update from github (see fw_update.h) */
void handleGetVersions();
void handleFwStatus();
void handleFwCheck();
void handleFwUpdate();
void handleGetFwSource();
void handleSetFwSource();
/* firmware pushed to us by the browser */
void handleFwPushDone();
void handleFwPushUpload();
void fwupdate_prepare();
void fwupdate_resume();
void fwupdate_yield();
void startMqtt();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttConnect();
time_t getBootTime();
void handleESPInfo();
void setTemperatureFromSensor();
void setupHA();
void handlePrometheusMetrics();

/* Debug */
void preparefortest();
void handleInputs();