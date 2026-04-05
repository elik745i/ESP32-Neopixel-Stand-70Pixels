#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <functional>

#include "settings_schema.h"

struct PlaybackCommand {
    String action;
    String url;
    String label;
    String source;
    String mediaType;
    String rawPayload;
    int32_t pixelIndex = -1;
    uint8_t volumePercent = 0;
    bool powerEnabled = true;
};

namespace HaBridge {

using LightEffectsGetter = std::function<void(JsonArray)>;

String availabilityTopic(const SettingsBundle& settings);
String playbackStateTopic(const SettingsBundle& settings);
String colorStateTopic(const SettingsBundle& settings);
String networkStateTopic(const SettingsBundle& settings);
String batteryStateTopic(const SettingsBundle& settings);
#ifdef APP_ENABLE_HACS_MQTT
String hacsMediaPlayerDiscoveryTopic(const SettingsBundle& settings);
String hacsMediaPlayerStateTopic(const SettingsBundle& settings, const char* field);
String hacsMediaPlayerCommandTopic(const SettingsBundle& settings, const char* command);
#endif
String commandTopic(const SettingsBundle& settings, const char* command);
String colorCommandTopic(const SettingsBundle& settings);
String pixelsCommandTopic(const SettingsBundle& settings);
String pixelCommandPrefix(const SettingsBundle& settings);
String pixelCommandWildcardTopic(const SettingsBundle& settings);
String entityUniqueId(const SettingsBundle& settings, const char* suffix);
String discoveryTopic(const SettingsBundle& settings, const char* component, const char* objectId);
String discoveryPayloadSensor(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* valueTemplate, const char* unit, const char* deviceClass, const char* stateClass, const char* icon = nullptr, int suggestedDisplayPrecision = -1);
String discoveryPayloadLight(const SettingsBundle& settings, const char* objectId, const char* name, LightEffectsGetter lightEffectsGetter = nullptr);
String discoveryPayloadNumber(const SettingsBundle& settings, const char* objectId, const char* name, const char* stateTopic, const char* commandTopic, int minValue, int maxValue, int step, const char* unit, const char* icon = nullptr);
String discoveryPayloadButton(const SettingsBundle& settings, const char* objectId, const char* name, const char* commandTopic, const char* payloadPress, const char* icon = nullptr);
String discoveryPayloadText(const SettingsBundle& settings, const char* objectId, const char* name, const char* commandTopic, const char* stateTopic = nullptr, const char* valueTemplate = nullptr, const char* icon = nullptr);
#ifdef APP_ENABLE_HACS_MQTT
String discoveryPayloadHacsMediaPlayer(const SettingsBundle& settings);
#endif

}  // namespace HaBridge
