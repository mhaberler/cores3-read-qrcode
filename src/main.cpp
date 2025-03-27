#include <M5CoreS3.h>
#include <WiFi.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <quirc.h>
#include "esp_wifi.h"
#include "LogCanvas.h"
#include "slconfig.hpp"

#include <qrcode.h>

#include "734446__universfield__error-10.h"
#include "734443__universfield__system-notification-4.h"


typedef enum {
    AS_UNDEFINED,
    AS_UNCONFIGURED,  // no wifi config available
    AS_CONNECTING,    // wifi config available
    AS_CONNECTED,
    AS_CFG_SENSORLOGGER,
    AS_SERVICING,
    AS_CONNECT_FAILED, // wifi config present but connect failed
    AS_SCANNING_QRCODE
} app_state_t;

// Struct to hold the parsed WiFi configuration
struct WiFiConfig {
    String SSID;
    String type;
    String password;
};

wl_status_t wifi_status = WL_STOPPED;
struct WiFiConfig wcfg;

struct quirc_code *code;
struct quirc_data *data;
struct quirc *qr = nullptr;

app_state_t appstate = AS_UNCONFIGURED;
app_state_t prev_appstate = AS_UNDEFINED;

WiFiConfig parseWiFiQR(const String& qrText);

// {scaleX, skewX, transX, skewY, scaleY, transY}
float affine[6] = {0.25, 0, 0, 0,  0.25, 0};

// M5Canvas canvas(&CoreS3.Display);
M5GFX &display = CoreS3.Display;
LogCanvas canvas(&display);


#define VSPACE 5

#define MQTT_PORT 1883
#define MQTTWS_PORT 81

WiFiServer tcp_server(MQTT_PORT);
WiFiServer websocket_underlying_server(MQTTWS_PORT);
PicoWebsocket::Server<::WiFiServer> websocket_server(websocket_underlying_server);

const char* hostname = "picomqtt";

class CustomMQTTServer: public PicoMQTT::Server {

    using  PicoMQTT::Server::Server;

  public:
    int32_t connected, subscribed, messages;

  protected:
    void on_connected(const char * client_id) override {
        canvas.printf("client %s connected\r\n", client_id);
        connected++;
    }
    virtual void on_disconnected(const char * client_id) override {
        canvas.printf("client %s disconnected\r\n", client_id);
        connected--;
    }
    virtual void on_subscribe(const char * client_id, const char * topic) override {
        canvas.printf("client %s subscribed %s\r\n", client_id, topic);
        subscribed++;
    }
    virtual void on_unsubscribe(const char * client_id, const char * topic) override {
        canvas.printf("client %s unsubscribed %s\r\n", client_id, topic);
        subscribed--;
    }
    virtual void on_message(const char * topic, PicoMQTT::IncomingPacket & packet) override {
        // canvas.printf("message topic=%s\r\n", topic);
        PicoMQTT::Server::Server::on_message(topic, packet);
        messages++;
    }
};

CustomMQTTServer mqtt(tcp_server, websocket_server);

void chimeError(void) {
    CoreS3.Speaker.playRaw(
        __734446__universfield__error_10_wav,
        __734446__universfield__error_10_wav_len,
        44100, true);
}

void chimeSuccess(void) {
    CoreS3.Speaker.playRaw(
        __734443__universfield__system_notification_4_wav,
        __734443__universfield__system_notification_4_wav_len,
        44100, true);
}

bool readStoredWiFiConfig() {
    wifi_config_t config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) {
        char ssid[33];
        memcpy(ssid, config.sta.ssid, 32);
        ssid[32] = '\0';
        char password[65];
        memcpy(password, config.sta.password, 64);
        password[64] = '\0';
        log_i("Stored SSID: %s", ssid);
        log_i("Stored Password: %s", password);
        return strlen(ssid) > 0;
    } else {
        log_e("Failed to get Wi-Fi configuration.");
    }
    return false;
}

void setup() {

    M5.begin();
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setAllChannelVolume(255);
    CoreS3.Speaker.tone(440, 200);

    canvas.resize(0, display.height() / 2 + VSPACE, display.width(), display.height()/2 - VSPACE);

    // tweak the default camera config
    CoreS3.Camera.config->pixel_format = PIXFORMAT_GRAYSCALE;
    CoreS3.Camera.config->frame_size = FRAMESIZE_VGA;
    if (!CoreS3.Camera.begin()) {
        canvas.printf("Camera Init failed\r\n");
        while (1);
    }

    code = (struct quirc_code *)ps_malloc(sizeof(struct quirc_code));
    data = (struct quirc_data *)ps_malloc(sizeof(struct quirc_data));

    assert(code != NULL);
    assert(data != NULL);

    WiFi.begin();

    if (readStoredWiFiConfig()) {
        canvas.printf("Click Power button for reset to defaults\r\n");
        appstate = AS_CONNECTING;
    } else {
        appstate = AS_SCANNING_QRCODE;
    }
}

void loop() {
    esp_err_t err;

    M5.update();
    if (CoreS3.BtnPWR.wasClicked()) {
        canvas.printf("erasing WiFi config\r\n");
        WiFi.eraseAP();
        WiFi.disconnect(); // reboot here
        canvas.printf("rebooting..\r\n");
        delay(300);
        ESP.restart();
    }

    wl_status_t ws = WiFi.status();
    if (ws ^ wifi_status) {
        wifi_status = ws; // track changes

        switch (ws) {
            case WL_CONNECTED:
                canvas.printf("WiFi: Connected\r\n");
                canvas.printf("IP: %s\r\n", WiFi.localIP().toString().c_str());
                appstate = AS_CONNECTED;
                break;
            case WL_NO_SSID_AVAIL:
                canvas.printf("WiFi: SSID %s not found\r\n", wcfg.SSID.c_str());
                break;
            case WL_DISCONNECTED:
                canvas.printf("WiFi: disconnected\r\n");
                break;
            default:
                // canvas.printf("WiFi status: %d\r\n", ws);
                break;
        }
        log_i("wifi_status=%d", wifi_status);
    }

    if (appstate ^ prev_appstate) { // appstate changes
        prev_appstate = appstate;
        switch (appstate) {
            case AS_CONNECTING:
                wifi_config_t config;
                err = esp_wifi_get_config(WIFI_IF_STA, &config);
                if (strlen((const char *)config.sta.ssid) == 0) {
                    // no stored WiFi config, enter QR scan mode
                } else {
                    // try stored WiFi config
                    canvas.printf("trying SSID %s\r\n", config.sta.ssid);
                }
                break;
            case AS_SCANNING_QRCODE:
                canvas.printf("point camera at WiFi QRcode:\r\n");
                break;
            case AS_CONNECTED:
                M5.Display.clear();
                // Start mDNS
                if (MDNS.begin(hostname)) {
                    MDNS.addService("mqtt", "tcp", 1883);
                    MDNS.addService("mqtt-ws", "tcp", 81);
                    mdns_service_instance_name_set("_mqtt", "_tcp", "MQTT/TCP broker");
                    mdns_service_instance_name_set("_mqtt-ws", "_tcp", "MQTT/Websockets broker");
                }
                mqtt.begin();
                {
                    String qrcode;
                    JsonDocument doc;
                    genDefaultCfg(doc);

                    // patch up the default config as needed
                    JsonObject sensorState = doc["sensorState"].to<JsonObject>();

                    sensorState["Barometer"]["enabled"] = true;
                    sensorState["Barometer"]["speed"] = 1000;

                    sensorState["Location"]["enabled"] = true;
                    sensorState["Location"]["speed"] = 1000;

                    sensorState["Microphone"]["enabled"] = true;
                    sensorState["Microphone"]["speed"] = "lossless";

                    // doc["http"]["enabled"] = false;
                    // doc["http"]["url"] = "http://" + WiFi.localIP().toString() + ":80/data";

                    doc["mqtt"]["enabled"] =  false;
                    doc["mqtt"]["url"] = WiFi.localIP().toString() ;
                    doc["mqtt"]["port"] = "1883";
                    doc["mqtt"]["tls"] =  false;
                    doc["mqtt"]["connectionType"] = "TCP";
                    doc["mqtt"]["subscribeTopic"] = "#";
                    doc["mqtt"]["subscribeEnabled"] = true;
                    doc["mqtt"]["skip"] =  false;

                    // serializeJsonPretty(doc, Serial);

                    sensorloggerCfg(doc, qrcode);
                    log_d("qrcode: %s", qrcode.c_str());

                    M5.Display.qrcode(qrcode.c_str());
                }
                appstate = AS_CFG_SENSORLOGGER;
                break;

            case AS_SERVICING:
                canvas.printf("serving client\r\n");
                break;

            default:
                ;
        }
    }

    if ((appstate == AS_SCANNING_QRCODE) && CoreS3.Camera.get()) {
        camera_fb_t *fb = CoreS3.Camera.fb;
        if (fb) {
            display.pushGrayscaleImageAffine(affine, fb->width, fb->height,
                                             (uint8_t *)CoreS3.Camera.fb->buf,
                                             lgfx::v1::grayscale_8bit,
                                             TFT_WHITE, TFT_BLACK);
            int width = fb->width;
            int height = fb->height;
            if (!qr) {
                // once only - if not needed anymore, free with quirc_destroy(qr)
                qr = quirc_new();
                assert(quirc_resize(qr, width, height) >= 0);
            }

            uint8_t *image = quirc_begin(qr, &width, &height);
            if (image) {
                memcpy(image, fb->buf, fb->len);

                quirc_end(qr);
                int num_codes = quirc_count(qr);
                if (num_codes) {
                    log_i("width %u height %u num_codes %d",
                          fb->width, fb->height,num_codes);
                }

                for (int i = 0; i < num_codes; i++) {
                    quirc_extract(qr, i, code);
                    quirc_decode_error_t err = quirc_decode(code, data);
                    if (err == QUIRC_ERROR_DATA_ECC) {
                        quirc_flip(code);
                        err = quirc_decode(code, data);
                    }
                    if (!err) {
                        chimeSuccess();

                        log_i("payload '%s'", data->payload);
                        log_i("Version: %d", data->version);
                        log_i("ECC level: %c", "MLHQ"[data->ecc_level]);
                        log_i("Mask: %d", data->mask);
                        log_i("Length: %d", data->payload_len);
                        log_i("Payload: %s", data->payload);

                        const String payload = String((const char *)data->payload);

                        wcfg = parseWiFiQR(payload);
                        log_i("SSID '%s'", wcfg.SSID.c_str());
                        log_i("type '%s'", wcfg.type.c_str());
                        log_i("password '%s'", wcfg.password.c_str());

                        if (wcfg.SSID.length() > 0) {
                            WiFi.begin(wcfg.SSID.c_str(), wcfg.password.c_str());
                            WiFi.persistent(true);
                            appstate = AS_CONNECTING;
                            canvas.printf("SSID: %s\r\n", wcfg.SSID.c_str());
                            // canvas.printf("Password: %s\r\n", wcfg.password.c_str());
                        } else {
                            canvas.printf("QR: %s\r\n", payload.c_str());
                        }
                        delay(3000);
                    } else {
                        chimeError();
                        canvas.printf("decode: %s\r\n",quirc_strerror(err));
                        delay(500);
                    }
                }
            }
            CoreS3.Camera.free();
        }
    }
    // Handle MQTT
    mqtt.loop();
    if (appstate == AS_CFG_SENSORLOGGER) {
        auto touchPoint = CoreS3.Touch.getDetail();
        bool touched = (touchPoint.state == m5::touch_state_t::touch);
        if ((mqtt.messages > 0) || mqtt.connected || touched ) {
            // use full screen for logging
            canvas.resize(0, 0, display.width(), display.height());
            if (!touched) {
                canvas.printf("MQTT client seen\r\n");
            }
            appstate = AS_SERVICING;
        }
    }

    yield();
}


// Function to unescape special characters
String unescape(const String& str) {
    String result = "";
    int i = 0;
    while (i < str.length()) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            char next = str[i + 1];
            if (next == '\\') {
                result += '\\';
            } else if (next == ';') {
                result += ';';
            } else if (next == ',') {
                result += ',';
            } else if (next == '"') {
                result += '"';
            } else if (next == ':') {
                result += ':';
            } else {
                // Unknown escape, add both
                result += '\\';
                result += next;
            }
            i += 2; // Skip both \\ and next
        } else {
            result += str[i];
            i++;
        }
    }
    return result;
}

// Helper function to process a single key-value pair
void processPair(const String& pair, WiFiConfig& config) {
    int colon = pair.indexOf(':');
    if (colon != -1) {
        String key = pair.substring(0, colon);
        String value = pair.substring(colon + 1);
        value = unescape(value);
        if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.substring(1, value.length() - 1);
        }
        if (key == "S") {
            config.SSID = value;
        } else if (key == "T") {
            config.type = value;
        } else if (key == "P") {
            config.password = value;
        }
        // Add more fields if needed (e.g., H for hidden networks)
    }
}

// Main function to parse WiFi QR code text
WiFiConfig parseWiFiQR(const String& qrText) {
    WiFiConfig config;
    if (!qrText.startsWith("WIFI:")) {
        // Handle error: not a valid WiFi QR code
        return config;
    }
    String content = qrText.substring(5); // Remove "WIFI:"
    // Remove trailing semicolons
    while (content.endsWith(";")) {
        content = content.substring(0, content.length() - 1);
    }
    // Split by semicolons
    int start = 0;
    int end = content.indexOf(';');
    while (end != -1) {
        String pair = content.substring(start, end);
        processPair(pair, config);
        start = end + 1;
        end = content.indexOf(';', start);
    }
    // Process the last pair
    String lastPair = content.substring(start);
    processPair(lastPair, config);
    return config;
}

