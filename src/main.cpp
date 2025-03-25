#include <M5CoreS3.h>
#include <WiFi.h>
#include <quirc.h>
#include "734446__universfield__error-10.h"
#include "734443__universfield__system-notification-4.h"

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

WiFiConfig parseWiFiQR(const String& qrText);

// {scaleX, skewX, transX, skewY, scaleY, transY}
float affine[6] = {0.25, 0, 0, 0,  0.25, 0};

M5Canvas canvas(&CoreS3.Display);
M5GFX &display = CoreS3.Display;

#define VSPACE 5

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

void setup() {

    M5.begin();
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setAllChannelVolume(255);
    // CoreS3.Speaker.tone(440, 200);

    canvas.setColorDepth(1); // mono color
    canvas.createSprite(display.width(), display.height()/2 - VSPACE);
    canvas.setFont(&fonts::FreeSans9pt7b);
    canvas.setTextScroll(true);

    // tweak the default camera config
    CoreS3.Camera.config->pixel_format = PIXFORMAT_GRAYSCALE;
    CoreS3.Camera.config->frame_size = FRAMESIZE_VGA;
    if (!CoreS3.Camera.begin()) {
        canvas.printf("Camera Init failed\r\n");
        canvas.pushSprite(0, display.height()/2 + VSPACE);
    }

    code = (struct quirc_code *)ps_malloc(sizeof(struct quirc_code));
    data = (struct quirc_data *)ps_malloc(sizeof(struct quirc_data));

    assert(code != NULL);
    assert(data != NULL);

    canvas.printf("Camera Init OK\r\n");
    canvas.pushSprite(0, display.height()/2 + VSPACE);
}

void loop() {
    M5.update();

    wl_status_t ws = WiFi.status();
    if (ws ^ wifi_status) {
        wifi_status = ws; // track changes

        switch (ws) {
            case WL_CONNECTED:
                canvas.printf("WiFi: Connected\r\n");
                canvas.printf("IP: %s\r\n", WiFi.localIP().toString().c_str());
                break;
            case WL_NO_SSID_AVAIL:
                canvas.printf("WiFi: SSID %s not found\r\n", wcfg.SSID.c_str());
                break;
            case WL_DISCONNECTED:
                canvas.printf("WiFi: disconnected\r\n");
                break;
            default:
                canvas.printf("WiFi status: %d\r\n", ws);
                break;
        }
        canvas.pushSprite(0, display.height()/2 + VSPACE);

        log_i("wifi_status=%d", wifi_status);
    }
    if ((ws != WL_CONNECTED) && CoreS3.Camera.get()) {
        camera_fb_t *fb = CoreS3.Camera.fb;
        if (fb) {

            CoreS3.Display.pushGrayscaleImageAffine(affine, fb->width, fb->height,
                                                    (uint8_t *)CoreS3.Camera.fb->buf,
                                                    lgfx::v1::grayscale_8bit,
                                                    TFT_WHITE, TFT_BLACK);
            struct quirc *qr = quirc_new();
            int width = fb->width;
            int height = fb->height;

            if (qr && quirc_resize(qr, width, height) >= 0) {
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
                                canvas.printf("SSID: %s\r\n", wcfg.SSID.c_str());
                                canvas.printf("Password: %s\r\n", wcfg.password.c_str());
                                canvas.pushSprite(0, display.height()/2+ VSPACE);
                            } else {
                                canvas.printf("QR: %s\r\n", payload.c_str());
                                canvas.pushSprite(0, display.height()/2+ VSPACE);
                            }
                            delay(3000);
                        } else {
                            chimeError();
                            canvas.printf("decode error: %d\r\n",err);
                            canvas.pushSprite(0, display.height()/2+ VSPACE);

                            delay(500);
                        }
                    }
                }
                quirc_destroy(qr);
            }
            CoreS3.Camera.free();
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

