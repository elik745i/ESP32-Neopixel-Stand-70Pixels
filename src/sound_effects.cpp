#include "sound_effects.h"

void SoundEffectsManager::begin(const SettingsBundle& settings) {
    (void)settings;
}

void SoundEffectsManager::applySettings(const SettingsBundle& settings) {
    (void)settings;
}

void SoundEffectsManager::setMuted(bool muted) {
    (void)muted;
}

void SoundEffectsManager::setVolumePercent(uint8_t volumePercent) {
    (void)volumePercent;
}

void SoundEffectsManager::playBoot() {
}

void SoundEffectsManager::playWifiConnected() {
}

void SoundEffectsManager::playWifiDisconnected() {
}

void SoundEffectsManager::playMqttConnected() {
}

void SoundEffectsManager::playPlaybackStart() {
}

void SoundEffectsManager::playPlaybackStop() {
}

void SoundEffectsManager::playEffect(const char* effectName) {
    (void)effectName;
}