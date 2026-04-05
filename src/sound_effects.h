#pragma once

#include <Arduino.h>

#include "settings_schema.h"

class SoundEffectsManager {
  public:
    void begin(const SettingsBundle& settings);
    void applySettings(const SettingsBundle& settings);
    void setMuted(bool muted);
    void setVolumePercent(uint8_t volumePercent);

    void playBoot();
    void playWifiConnected();
    void playWifiDisconnected();
    void playMqttConnected();
    void playPlaybackStart();
    void playPlaybackStop();

  private:
    void playEffect(const char* effectName);
};