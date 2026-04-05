#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "settings_schema.h"

class LightPlayer {
  public:
    class Impl;

    void begin(uint8_t dataPin, uint16_t pixelCount, uint8_t initialBrightnessPercent, AppState& appState);
    void finishStartup();
    void loop();
    bool play(const String& primaryColor, const String& secondaryColor, const String& effectName, const String& source);
    void stop();
    void setVolumePercent(uint8_t brightnessPercent);
    void applyLightSettings(const LightSettings& settings);
    void setPowerEnabled(bool enabled);
    bool applyPixelCommand(const String& payload, const String& source, String& error);
    bool setPixelColor(uint16_t index, const String& color, const String& source, String& error);
    void clearPixelOverride(const String& source = "manual");
    void setOtaProgress(uint8_t progressPercent, bool active);
    void syncStatusIndicators();
    uint8_t volumePercent() const;
    String currentTitle() const;
    String currentUrl() const;
    String currentState() const;
    String effectName(uint16_t index) const;
    uint16_t effectCount() const;
    uint16_t findEffectIndex(const String& name) const;

    void onStationName(const char* text);
    void onStreamTitle(const char* text);
    void onInfo(const char* text);
    void onEof(const char* text);

  private:
    Impl* impl_ = nullptr;
};