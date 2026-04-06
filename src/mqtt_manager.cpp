#include "mqtt_manager.h"

namespace {
String payloadToString(char* payload, size_t len) {
    String value;
    value.reserve(len);
    for (size_t index = 0; index < len; ++index) {
        value += payload[index];
    }
    return value;
}

bool mqttReconnectRequired(const SettingsBundle& current, const SettingsBundle& next) {
    return current.mqtt.host != next.mqtt.host ||
           current.mqtt.port != next.mqtt.port ||
           current.mqtt.username != next.mqtt.username ||
           current.mqtt.password != next.mqtt.password ||
           current.mqtt.clientId != next.mqtt.clientId ||
           current.device.deviceName != next.device.deviceName;
}

String normalizeMediaType(const String& value, bool announce) {
    String mediaType = value;
    mediaType.trim();
    mediaType.toLowerCase();

    if (announce || mediaType.indexOf("tts") >= 0 || mediaType.indexOf("announce") >= 0 || mediaType.indexOf("speech") >= 0) {
        return "tts";
    }

    if (mediaType.isEmpty()) {
        return "stream";
    }

    if (mediaType == "music" || mediaType == "audio" || mediaType == "stream" || mediaType == "media" || mediaType == "radio") {
        return "stream";
    }

    return mediaType;
}

String normalizeEffectName(const String& value) {
    String effect = value;
    effect.trim();
    return effect;
}

String normalizeRgbPayload(const String& value) {
    String payload = value;
    payload.trim();
    if (payload.isEmpty()) {
        return String();
    }

    if (payload.startsWith("#")) {
        String hex = payload;
        hex.toUpperCase();
        return hex;
    }

    int firstSeparator = payload.indexOf(',');
    if (firstSeparator < 0) {
        firstSeparator = payload.indexOf(';');
    }
    if (firstSeparator < 0) {
        return payload;
    }

    const char separator = payload[firstSeparator];
    const int secondSeparator = payload.indexOf(separator, firstSeparator + 1);
    if (secondSeparator < 0) {
        return payload;
    }

    const int red = constrain(payload.substring(0, firstSeparator).toInt(), 0, 255);
    const int green = constrain(payload.substring(firstSeparator + 1, secondSeparator).toInt(), 0, 255);
    const int blue = constrain(payload.substring(secondSeparator + 1).toInt(), 0, 255);

    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", red, green, blue);
    return String(buffer);
}

String hexToRgbCsv(const String& value) {
    String color = value;
    color.trim();
    if (!color.startsWith("#") || color.length() != 7) {
        return String("255,255,255");
    }

    const long packed = strtol(color.substring(1).c_str(), nullptr, 16);
    const int red = (packed >> 16) & 0xFF;
    const int green = (packed >> 8) & 0xFF;
    const int blue = packed & 0xFF;
    return String(red) + "," + String(green) + "," + String(blue);
}

uint8_t percentFromVolumeLevel(float level) {
    const float clamped = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    return static_cast<uint8_t>((clamped * 100.0f) + 0.5f);
}

uint8_t parseBrightnessPercent(const String& payload) {
    String value = payload;
    value.trim();
    if (value.isEmpty()) {
        return 0;
    }

    const float parsed = value.toFloat();
    if (value.indexOf('.') >= 0) {
        if (parsed >= 0.0f && parsed <= 1.0f) {
            return percentFromVolumeLevel(parsed);
        }
        return static_cast<uint8_t>(constrain(static_cast<int>(parsed + 0.5f), 0, 100));
    }

    return static_cast<uint8_t>(constrain(value.toInt(), 0, 100));
}

bool parsePowerPayload(const String& payload, bool defaultValue) {
    if (!payload.startsWith("{")) {
        return !(payload.equalsIgnoreCase("0") || payload.equalsIgnoreCase("false") || payload.equalsIgnoreCase("off"));
    }

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        return defaultValue;
    }

    if (!doc["powerEnabled"].isNull()) {
        return doc["powerEnabled"].as<bool>();
    }
    if (!doc["on"].isNull()) {
        return doc["on"].as<bool>();
    }
    if (!doc["state"].isNull()) {
        const String stateValue = String(static_cast<const char*>(doc["state"] | ""));
        return !(stateValue.equalsIgnoreCase("0") || stateValue.equalsIgnoreCase("false") || stateValue.equalsIgnoreCase("off"));
    }

    return defaultValue;
}

String configurationUrlForSnapshot(const AppStateSnapshot& snapshot) {
    if (!snapshot.network.ip.isEmpty()) {
        return "http://" + snapshot.network.ip + "/";
    }
    return String();
}

#ifdef APP_ENABLE_HACS_MQTT
[[maybe_unused]]
String normalizedHacsPlaybackState(const String& value) {
    String state = value;
    state.trim();
    state.toLowerCase();

    if (state == "buffering") {
        return "playing";
    }

    if (state == "playing" || state == "paused" || state == "idle" || state == "off" || state == "stopped") {
        return state;
    }

    return "idle";
}

[[maybe_unused]]
String normalizedHacsMediaType(const String& value) {
    String mediaType = value;
    mediaType.trim();
    mediaType.toLowerCase();

    if (mediaType == "tts" || mediaType == "speech" || mediaType == "announce") {
        return "music";
    }

    if (mediaType.isEmpty() || mediaType == "idle" || mediaType == "stream" || mediaType == "radio" || mediaType == "audio") {
        return "music";
    }

    return mediaType;
}

[[maybe_unused]]
String hacsVolumePayload(uint8_t volumePercent) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%.2f", static_cast<float>(volumePercent) / 100.0f);
    return String(buffer);
}
#endif
}  // namespace

void MqttManager::begin(const SettingsBundle& settings, AppState& appState, WiFiManager& wifiManager, CommandHandler commandHandler, LightEffectsGetter lightEffectsGetter) {
    appState_ = &appState;
    wifiManager_ = &wifiManager;
    commandHandler_ = commandHandler;
    lightEffectsGetter_ = lightEffectsGetter;

    client_.onConnect([this](bool sessionPresent) { handleConnected(sessionPresent); });
    client_.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { handleDisconnected(reason); });
    client_.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        handleMessage(topic, payload, properties, len, index, total);
    });
    applySettings(settings);
}

void MqttManager::applySettings(const SettingsBundle& settings) {
    const bool needsReconfigure = !configured_ || mqttReconnectRequired(settings_, settings) || !client_.connected();
    settings_ = settings;
    if (settings_.mqtt.host.isEmpty()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
    }
    if (needsReconfigure) {
        configureClient();
        configured_ = true;
        return;
    }

    if (client_.connected()) {
        publishDiscovery();
        publishState();
    }
}

void MqttManager::configureClient() {
    client_.disconnect(true);
    client_.setServer(settings_.mqtt.host.c_str(), settings_.mqtt.port);
    client_.setClientId(settings_.mqtt.clientId.isEmpty() ? settings_.device.deviceName.c_str() : settings_.mqtt.clientId.c_str());
    client_.setCredentials(
        settings_.mqtt.username.isEmpty() ? nullptr : settings_.mqtt.username.c_str(),
        settings_.mqtt.username.isEmpty() ? nullptr : settings_.mqtt.password.c_str());
    client_.setKeepAlive(30);
    client_.setWill(HaBridge::availabilityTopic(settings_).c_str(), 1, true, "offline");
    lastConnectAttemptAt_ = 0;
    if (settings_.mqtt.host.isEmpty()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
        setConnectionDetail("MQTT host is not configured.");
    } else if (!connectionEnabled_) {
        setConnectionDetail("MQTT connection is disabled.");
    } else if (wifiManager_ != nullptr && !wifiManager_->isConnected()) {
        setConnectionDetail("Waiting for Wi-Fi before MQTT can connect.");
    }

    if (!connectionEnabled_) {
        return;
    }

    if (!settings_.mqtt.host.isEmpty() && wifiManager_ != nullptr && wifiManager_->isConnected()) {
        Serial.printf("[mqtt] immediate reconnect attempt %u/%u to %s:%u\n",
                      static_cast<unsigned>(consecutiveFailureCount_ + 1),
                      static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES),
                      settings_.mqtt.host.c_str(), settings_.mqtt.port);
        lastConnectAttemptAt_ = millis();
        client_.connect();
    }
}

void MqttManager::loop() {
    connectIfNeeded();
    if (isConnected() && millis() - lastStatePublishAt_ > 30000UL) {
        publishState();
    }
}

void MqttManager::connectIfNeeded() {
    if (!connectionEnabled_ || settings_.mqtt.host.isEmpty() || wifiManager_ == nullptr || !wifiManager_->isConnected() || client_.connected()) {
        return;
    }
    if (millis() - lastConnectAttemptAt_ < 5000UL) {
        return;
    }
    Serial.printf("[mqtt] connect attempt %u/%u to %s:%u\n",
                  static_cast<unsigned>(consecutiveFailureCount_ + 1),
                  static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES),
                  settings_.mqtt.host.c_str(), settings_.mqtt.port);
    lastConnectAttemptAt_ = millis();
    client_.connect();
}

void MqttManager::handleConnected(bool sessionPresent) {
    (void)sessionPresent;
    Serial.printf("[mqtt] connected host=%s port=%u\n", settings_.mqtt.host.c_str(), settings_.mqtt.port);
    consecutiveFailureCount_ = 0;
    recoveryRebootRecommended_ = false;
    clearFrontendError();
    if (appState_ != nullptr) {
        appState_->setMqttConnected(true);
    }
    setConnectionDetail("");
    client_.publish(HaBridge::availabilityTopic(settings_).c_str(), 1, true, "online");
    client_.subscribe(HaBridge::commandTopic(settings_, "play").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "tts").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "stop").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "volume").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "power").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "effect").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "color").c_str(), 1);
    client_.subscribe(HaBridge::colorCommandTopic(settings_).c_str(), 1);
    client_.subscribe(HaBridge::pixelsCommandTopic(settings_).c_str(), 1);
    client_.subscribe(HaBridge::pixelCommandWildcardTopic(settings_).c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "brightness").c_str(), 1);
#ifdef APP_ENABLE_HACS_MQTT
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "play").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "pause").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "playpause").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "next").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "previous").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "stop").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "volume").c_str(), 1);
    client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "playmedia").c_str(), 1);
#endif
    publishDiscovery();
    publishState();
}

void MqttManager::handleDisconnected(AsyncMqttClientDisconnectReason reason) {
    Serial.printf("[mqtt] disconnected reason=%d\n", static_cast<int>(reason));
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }

    if (!connectionEnabled_) {
        setConnectionDetail("MQTT disconnected by user request.");
    } else if (wifiManager_ != nullptr && !wifiManager_->isConnected()) {
        setConnectionDetail("Wi-Fi disconnected. Waiting to reconnect MQTT.");
    } else {
        setConnectionDetail("MQTT disconnected: " + disconnectReasonToString(reason));
    }

    registerFailedAttempt(reason);
}

void MqttManager::handleMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    (void)properties;
    if (index != 0 || total != len || commandHandler_ == nullptr) {
        return;
    }

    const String topicValue = topic;
    const String payloadValue = payloadToString(payload, len);
    PlaybackCommand command;
#ifdef APP_ENABLE_HACS_MQTT
    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "stop")) {
        command.action = "stop";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "pause")) {
        command.action = "pause";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "play") ||
        topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "playpause") ||
        topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "next") ||
        topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "previous")) {
        command.action = topicValue.substring(topicValue.lastIndexOf('/') + 1);
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "volume")) {
        command.action = "volume";
        command.volumePercent = parseBrightnessPercent(payloadValue);
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "playmedia")) {
        command.action = "play";
    }
#endif

    if (topicValue == HaBridge::commandTopic(settings_, "stop")) {
        command.action = "stop";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "power")) {
        command.action = "power";
        command.powerEnabled = parsePowerPayload(payloadValue, true);
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "effect")) {
        command.action = "effect";
        command.mediaType = normalizeEffectName(payloadValue);
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "color") ||
        topicValue == HaBridge::colorCommandTopic(settings_)) {
        command.action = "color";
        command.url = topicValue == HaBridge::colorCommandTopic(settings_)
            ? normalizeRgbPayload(payloadValue)
            : payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::pixelsCommandTopic(settings_)) {
        command.action = "pixels";
        command.rawPayload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue.startsWith(HaBridge::pixelCommandPrefix(settings_))) {
        const String indexSegment = topicValue.substring(HaBridge::pixelCommandPrefix(settings_).length());
        if (indexSegment.isEmpty()) {
            return;
        }
        for (size_t index = 0; index < indexSegment.length(); ++index) {
            if (!isDigit(static_cast<unsigned char>(indexSegment[index]))) {
                return;
            }
        }
        command.action = "pixel";
        command.pixelIndex = indexSegment.toInt();
        command.url = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "volume") || topicValue == HaBridge::commandTopic(settings_, "brightness")) {
        command.action = "volume";
        if (payloadValue.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
                if (!doc["volumePercent"].isNull() || !doc["volume"].isNull()) {
                    command.volumePercent = doc["volumePercent"] | doc["volume"] | 0;
                } else if (!doc["brightness"].isNull()) {
                    command.volumePercent = doc["brightness"] | 0;
                } else if (!doc["volume_level"].isNull()) {
                    command.volumePercent = percentFromVolumeLevel(doc["volume_level"] | 0.0f);
                }
            }
        } else {
            command.volumePercent = parseBrightnessPercent(payloadValue);
        }
        commandHandler_(command);
        return;
    }

    if (command.action.isEmpty()) {
        command.action = topicValue.endsWith("/tts") ? "tts" : "play";
    }
    if (payloadValue.startsWith("{")) {
        JsonDocument doc;
        if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
            const bool announce = doc["announce"] | false;
            const String mediaContentType = String(static_cast<const char*>(doc["media_content_type"] | doc["media_type"] | ""));
            const String explicitType = String(static_cast<const char*>(doc["type"] | ""));
            command.url = String(static_cast<const char*>(doc["url"] | doc["media_content_id"] | doc["media_id"] | doc["mediaId"] | ""));
            command.label = String(static_cast<const char*>(doc["label"] | doc["title"] | doc["media_title"] | doc["extra"]["title"] | ""));
            command.source = String(static_cast<const char*>(doc["source"] | ""));
            command.mediaType = normalizeMediaType(
                explicitType.isEmpty() ? mediaContentType : explicitType,
                command.action == "tts" || announce);

            if (command.label.isEmpty() && !command.url.isEmpty()) {
                command.label = command.url;
            }
        }
    } else {
        command.url = payloadValue;
        command.mediaType = command.action == "tts" ? "tts" : "stream";
    }
    commandHandler_(command);
}

void MqttManager::publishJson(const String& topic, const JsonDocument& doc, bool retained) {
    if (!client_.connected()) {
        return;
    }
    String payload;
    serializeJson(doc, payload);
    client_.publish(topic.c_str(), 1, retained, payload.c_str());
}

void MqttManager::publishState() {
    if (!client_.connected() || appState_ == nullptr) {
        return;
    }
    lastStatePublishAt_ = millis();
    const AppStateSnapshot snapshot = appState_->snapshot();
    const uint8_t publishedBrightness = snapshot.playback.powerEnabled ? snapshot.playback.volumePercent : 0;

    JsonDocument playback;
    playback["state"] = snapshot.playback.state;
    playback["effect"] = snapshot.playback.type;
    playback["title"] = snapshot.playback.title;
    playback["color"] = snapshot.playback.primaryColor;
    playback["details"] = snapshot.playback.url;
    playback["source"] = snapshot.playback.source;
    playback["brightness"] = publishedBrightness;
    playback["volumePercent"] = publishedBrightness;
    playback["powerEnabled"] = snapshot.playback.powerEnabled;
    publishJson(HaBridge::playbackStateTopic(settings_), playback, true);

    JsonDocument network;
    network["wifiConnected"] = snapshot.network.wifiConnected;
    network["apMode"] = snapshot.network.apMode;
    network["ip"] = snapshot.network.ip;
    network["ssid"] = snapshot.network.ssid;
    network["wifiRssi"] = snapshot.network.wifiRssi;
    network["mqttConnected"] = snapshot.network.mqttConnected;
    publishJson(HaBridge::networkStateTopic(settings_), network, true);

    JsonDocument battery;
    battery["voltage"] = snapshot.battery.voltage;
    battery["rawAdcVoltage"] = snapshot.battery.rawAdcVoltage;
    battery["rawAdc"] = snapshot.battery.rawAdc;
    publishJson(HaBridge::batteryStateTopic(settings_), battery, true);

    client_.publish(HaBridge::lightPowerStateTopic(settings_).c_str(), 1, true, snapshot.playback.powerEnabled ? "ON" : "OFF");
    client_.publish(HaBridge::lightEffectStateTopic(settings_).c_str(), 1, true, snapshot.playback.type.c_str());
    client_.publish((settings_.mqtt.baseTopic + "/state/brightness").c_str(), 1, true, String(publishedBrightness).c_str());
    client_.publish(HaBridge::colorStateTopic(settings_).c_str(), 1, true, hexToRgbCsv(snapshot.playback.primaryColor).c_str());
#ifdef APP_ENABLE_HACS_MQTT
    client_.publish(HaBridge::hacsMediaPlayerStateTopic(settings_, "state").c_str(), 1, true, normalizedHacsPlaybackState(snapshot.playback.state).c_str());
    client_.publish(HaBridge::hacsMediaPlayerStateTopic(settings_, "title").c_str(), 1, true, snapshot.playback.title.c_str());
    client_.publish(HaBridge::hacsMediaPlayerStateTopic(settings_, "mediatype").c_str(), 1, true, normalizedHacsMediaType(snapshot.playback.type).c_str());
    client_.publish(HaBridge::hacsMediaPlayerStateTopic(settings_, "volume").c_str(), 1, true, hacsVolumePayload(publishedBrightness).c_str());
#endif
}

void MqttManager::publishBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc) {
    if (!client_.connected()) {
        return;
    }
    JsonDocument battery;
    battery["voltage"] = voltage;
    battery["rawAdcVoltage"] = rawAdcVoltage;
    battery["rawAdc"] = rawAdc;
    publishJson(HaBridge::batteryStateTopic(settings_), battery, true);
}

void MqttManager::publishDiscovery() {
    if (!client_.connected() || !settings_.mqtt.discoveryEnabled) {
        return;
    }
    const String configurationUrl = appState_ == nullptr ? String() : configurationUrlForSnapshot(appState_->snapshot());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "light", "light").c_str(), 1, true,
        HaBridge::discoveryPayloadLight(settings_, "light", "Light", lightEffectsGetter_, configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "sensor", "battery_voltage").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "battery_voltage", "Battery Voltage", HaBridge::batteryStateTopic(settings_).c_str(), "{{ value_json.voltage | float(0) | round(2) }}", "V", "voltage", "measurement", "mdi:battery", 2, configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "sensor", "wifi_rssi").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "wifi_rssi", "Wi-Fi RSSI", HaBridge::networkStateTopic(settings_).c_str(), "{{ value_json.wifiRssi }}", "dBm", "signal_strength", "measurement", "mdi:wifi", -1, configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "sensor", "playback_state").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "playback_state", "Light State", HaBridge::playbackStateTopic(settings_).c_str(), "{{ value_json.state }}", nullptr, nullptr, nullptr, "mdi:led-strip-variant", -1, configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "number", "volume").c_str(), 1, true,
        HaBridge::discoveryPayloadNumber(settings_, "volume", "Light Brightness", (settings_.mqtt.baseTopic + "/state/brightness").c_str(), HaBridge::commandTopic(settings_, "brightness").c_str(), 0, 100, 1, "%", "mdi:brightness-6", configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "button", "stop").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "stop", "Turn Light Off", HaBridge::commandTopic(settings_, "stop").c_str(), "stop", "mdi:power", configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "text", "play_url").c_str(), 1, true,
        HaBridge::discoveryPayloadText(
            settings_, "play_url", "Primary Color",
            HaBridge::commandTopic(settings_, "color").c_str(),
            HaBridge::playbackStateTopic(settings_).c_str(),
            "{{ value_json.color }}",
            "mdi:palette",
            configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "text", "light_effect").c_str(), 1, true,
        HaBridge::discoveryPayloadText(
            settings_, "light_effect", "Light Effect",
            HaBridge::commandTopic(settings_, "effect").c_str(),
            HaBridge::playbackStateTopic(settings_).c_str(),
            "{{ value_json.effect }}",
            "mdi:creation",
            configurationUrl).c_str());
#ifdef APP_ENABLE_HACS_MQTT
    client_.publish(
        HaBridge::hacsMediaPlayerDiscoveryTopic(settings_).c_str(), 1, true,
        HaBridge::discoveryPayloadHacsMediaPlayer(settings_, configurationUrl).c_str());
    client_.publish(
        HaBridge::discoveryTopic(settings_, "media_player", "hacs_player").c_str(), 1, true,
        "");
#endif
}

bool MqttManager::isConnected() const {
    return client_.connected();
}

bool MqttManager::shouldRebootForRecovery() const {
    return recoveryRebootRecommended_;
}

uint8_t MqttManager::consecutiveFailureCount() const {
    return consecutiveFailureCount_;
}

bool MqttManager::requestConnect(String& error) {
    if (settings_.mqtt.host.isEmpty()) {
        error = "Enter an MQTT host first.";
        return false;
    }

    connectionEnabled_ = true;
    setConnectionDetail(wifiManager_ != nullptr && !wifiManager_->isConnected()
        ? "Waiting for Wi-Fi before MQTT can connect."
        : "MQTT connect requested.");
    if (client_.connected()) {
        publishDiscovery();
        publishState();
        return true;
    }

    configureClient();
    return true;
}

bool MqttManager::requestDisconnect(String& error) {
    error = "";
    connectionEnabled_ = false;
    consecutiveFailureCount_ = 0;
    recoveryRebootRecommended_ = false;
    lastConnectAttemptAt_ = millis();
    setConnectionDetail("MQTT disconnected by user request.");
    client_.disconnect(true);
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
    return true;
}

void MqttManager::registerFailedAttempt(AsyncMqttClientDisconnectReason reason) {
    if (!connectionEnabled_ || settings_.mqtt.host.isEmpty() || client_.connected()) {
        return;
    }

    if (wifiManager_ == nullptr || !wifiManager_->isConnected()) {
        return;
    }

    if (consecutiveFailureCount_ < MQTT_MAX_CONSECUTIVE_FAILURES) {
        ++consecutiveFailureCount_;
    }

    Serial.printf("[mqtt] connect failed reason=%d count=%u/%u\n", static_cast<int>(reason),
                  static_cast<unsigned>(consecutiveFailureCount_),
                  static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES));

    if (isCredentialFailureReason(reason)) {
        const String message = "MQTT broker rejected the configured client ID or credentials.";
        setConnectionDetail(message);
        setFrontendError(message);
        recoveryRebootRecommended_ = false;
        return;
    }

    setConnectionDetail("MQTT retry pending after " + disconnectReasonToString(reason) + ".");

    if (consecutiveFailureCount_ >= MQTT_MAX_CONSECUTIVE_FAILURES) {
        clearFrontendError();
        recoveryRebootRecommended_ = true;
        setConnectionDetail("MQTT recovery reboot recommended after repeated disconnects.");
        Serial.println("[mqtt] max consecutive failures reached, recovery reboot recommended");
    }
}

bool MqttManager::isCredentialFailureReason(AsyncMqttClientDisconnectReason reason) const {
    switch (reason) {
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            return true;
        default:
            return false;
    }
}

String MqttManager::disconnectReasonToString(AsyncMqttClientDisconnectReason reason) const {
    switch (reason) {
        case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED:
            return "TCP disconnected";
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
            return "unacceptable MQTT protocol version";
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
            return "client ID rejected";
        case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
            return "broker unavailable";
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
            return "malformed credentials";
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            return "not authorized";
        case AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE:
            return "not enough client buffer space";
        case AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT:
            return "TLS fingerprint mismatch";
        default:
            return "unknown reason (" + String(static_cast<int>(reason)) + ")";
    }
}

void MqttManager::setConnectionDetail(const String& detail) {
    if (appState_ != nullptr) {
        appState_->setMqttDetail(detail);
    }
}

void MqttManager::clearFrontendError() {
    if (appState_ != nullptr) {
        appState_->setLastError("");
    }
}

void MqttManager::setFrontendError(const String& message) {
    if (appState_ != nullptr) {
        appState_->setLastError(message);
    }
}
