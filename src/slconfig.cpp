
#include <WiFi.h>
#include "slconfig.hpp"
#include "base64.h"

extern "C" {
#include  "zstd.h"
}

String prefix = "https://sensorlogger.app/link/config/";

// take a string, compress and b64encode, prefix with sensorlogger deeplink
bool sensorloggerCfg(const String &input, String &output) {
    // determine max compressed  size
    size_t buffer_size = ZSTD_compressBound(input.length());
    uint8_t *buffer = (uint8_t *)malloc(buffer_size);
    if (buffer == NULL)
        return false;

    // Compress
    size_t compressed_size = ZSTD_compress(buffer, buffer_size, input.c_str(), input.length(), 0);

    if (ZSTD_isError(compressed_size)) {
        log_e("compression error: %s\n", ZSTD_getErrorName(compressed_size));
        free(buffer);
        return false;
    }
    output = prefix + base64::encode(buffer, compressed_size) + "==";

    log_d("src %u compressed %u final %zu",input.length(), compressed_size, output.length());
    free(buffer);
    return true;
}

// take a JsonDocument, compress and b64encode, prefix with sensorlogger deeplink
bool sensorloggerCfg(const JsonDocument &doc, String &output) {
    String jsonString;
    serializeJson(doc, jsonString);
    return sensorloggerCfg(jsonString, output);
}

// recreated from sensorlogger "reset to defaults" and exported
void genDefaultCfg(JsonDocument &doc) {

    doc["merge"] = true;
    JsonObject mqtt = doc["mqtt"].to<JsonObject>();

    mqtt["enabled"] = false;
    mqtt["url"] = WiFi.localIP().toString();
    mqtt["port"] = "1883";
    mqtt["tls"] = false;
    mqtt["topic"] = "sensor-logger";
    mqtt["connectionType"] = "TCP";
    mqtt["subscribeTopic"] = "tofsensor";   // no effect
    mqtt["skip"] = false;
    mqtt["subscribeEnabled"] = true;
    mqtt["batchPeriod"] = 1000;
}
