#include "audio_player.h"

#include <WS2812FX.h>

#include "default_config.h"

namespace {
String normalizeHexColor(const String& raw, const String& fallback = "#FFFFFF") {
    String color = raw;
    color.trim();
    if (color.isEmpty()) {
        return fallback;
    }
    if (!color.startsWith("#")) {
        color = "#" + color;
    }
    if (color.length() != 7) {
        return fallback;
    }
    color.toUpperCase();
    return color;
}

uint32_t parseColor(const String& color) {
    const String normalized = normalizeHexColor(color);
    return static_cast<uint32_t>(strtoul(normalized.substring(1).c_str(), nullptr, 16));
}

uint16_t speedToWs2812fx(uint8_t speedPercent) {
    const uint8_t clamped = constrain(speedPercent, static_cast<uint8_t>(1), static_cast<uint8_t>(255));
    return map(clamped, 1, 255, 5000, 60);
}

uint8_t applyPowerLimit(uint8_t requestedBrightness, uint16_t pixelCount, float powerLimiterAmps) {
    const float maxMilliAmps = powerLimiterAmps * 1000.0f;
    const float fullWhiteMilliAmps = static_cast<float>(max<uint16_t>(pixelCount, 1)) * 60.0f;
    if (maxMilliAmps <= 0.0f || fullWhiteMilliAmps <= 0.0f) {
        return requestedBrightness;
    }
    const float limitedPercent = min(100.0f, (maxMilliAmps / fullWhiteMilliAmps) * 100.0f);
    return min<uint8_t>(requestedBrightness, static_cast<uint8_t>(limitedPercent + 0.5f));
}

bool isNumeric(const String& value) {
    if (value.isEmpty()) {
        return false;
    }
    for (size_t index = 0; index < value.length(); ++index) {
        if (!isDigit(value[index])) {
            return false;
        }
    }
    return true;
}
}  // namespace

class AudioPlayer::Impl {
  public:
    WS2812FX* strip = nullptr;
    AppState* appState = nullptr;
    uint8_t dataPin = DefaultConfig::NEOPIXEL_PIN;
    uint8_t volume = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    uint16_t pixelCount = DefaultConfig::DEFAULT_PIXEL_COUNT;
    uint16_t effectIndex = 0;
    uint8_t effectSpeed = 128;
    uint8_t effectIntensity = 128;
    float powerLimiterAmps = DefaultConfig::DEFAULT_POWER_LIMITER_AMPS;
    bool powerEnabled = true;
    String state = "idle";
    String type = "Static";
    String title = "Off";
    String primaryColor = "#FFFFFF";
    String secondaryColor = "#000000";
    String tertiaryColor = "#FF7B00";
    String source = "manual";

    ~Impl() {
        delete strip;
    }

    void rebuildStripIfNeeded() {
        if (strip != nullptr && pixelCount == strip->getLength() && dataPin == strip->getPin()) {
            return;
        }

        delete strip;
        strip = new WS2812FX(pixelCount, dataPin, NEO_GRB + NEO_KHZ800);
        strip->init();
        strip->setBrightness(0);
        strip->setMode(0);
        strip->setColor(parseColor(primaryColor));
        strip->start();
        strip->service();
    }

    uint8_t effectiveBrightness() const {
        return applyPowerLimit(volume, pixelCount, powerLimiterAmps);
    }

    String currentEffectName() const {
        if (strip == nullptr) {
            return "Static";
        }
        const __FlashStringHelper* name = strip->getModeName(effectIndex);
        return name == nullptr ? String("Static") : String(name);
    }

    void applyToStrip() {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        strip->setMode(effectIndex);
        strip->setSpeed(speedToWs2812fx(effectSpeed));
        strip->setColor(parseColor(primaryColor));
        strip->setBrightness(powerEnabled ? map(effectiveBrightness(), 0, 100, 0, 255) : 0);
        strip->start();
        strip->trigger();
        strip->service();

        type = currentEffectName();
        title = powerEnabled ? type : String("Off");
        state = powerEnabled ? "playing" : "idle";
    }

    String currentUrlSummary() const {
        String summary = primaryColor;
        summary += " | ";
        summary += String(pixelCount);
        summary += " px | ";
        summary += String(powerLimiterAmps, 1);
        summary += " A";
        return summary;
    }

    void publish() {
        if (appState != nullptr) {
            appState->setPlayback(state, type, title, currentUrlSummary(), source, volume);
        }
    }
};

void AudioPlayer::begin(uint8_t dataPin, uint16_t pixelCount, uint8_t initialBrightnessPercent, AppState& appState) {
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }

    impl_->appState = &appState;
    impl_->dataPin = dataPin;
    impl_->pixelCount = pixelCount;
    setVolumePercent(initialBrightnessPercent);
    impl_->applyToStrip();
    impl_->publish();
}

void AudioPlayer::loop() {
    if (impl_ != nullptr && impl_->strip != nullptr) {
        impl_->strip->service();
    }
}

bool AudioPlayer::play(const String& primaryColor, const String& secondaryColor, const String& effectName, const String& source) {
    if (impl_ == nullptr) {
        return false;
    }

    if (!primaryColor.isEmpty()) {
        impl_->primaryColor = normalizeHexColor(primaryColor, impl_->primaryColor);
    }
    if (!secondaryColor.isEmpty()) {
        impl_->secondaryColor = normalizeHexColor(secondaryColor, impl_->secondaryColor);
    }
    if (!effectName.isEmpty()) {
        impl_->effectIndex = findEffectIndex(effectName);
    }

    impl_->source = source.isEmpty() ? String("manual") : source;
    impl_->powerEnabled = true;
    impl_->applyToStrip();
    impl_->publish();
    return true;
}

void AudioPlayer::stop() {
    if (impl_ == nullptr) {
        return;
    }

    impl_->powerEnabled = false;
    impl_->source = "manual";
    impl_->applyToStrip();
    impl_->publish();
}

void AudioPlayer::setVolumePercent(uint8_t brightnessPercent) {
    if (impl_ == nullptr) {
        return;
    }

    const uint8_t nextBrightness = constrain(brightnessPercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    if (impl_->volume == nextBrightness) {
        return;
    }

    impl_->volume = nextBrightness;
    impl_->applyToStrip();
    impl_->publish();
}

void AudioPlayer::applyLightSettings(const LightSettings& settings) {
    if (impl_ == nullptr) {
        return;
    }

    impl_->pixelCount = settings.pixelCount;
    impl_->dataPin = settings.dataPin;
    impl_->powerLimiterAmps = settings.powerLimiterAmps;
    impl_->effectIndex = settings.effectIndex;
    impl_->effectSpeed = settings.effectSpeed;
    impl_->effectIntensity = settings.effectIntensity;
    impl_->primaryColor = normalizeHexColor(settings.primaryColor, impl_->primaryColor);
    impl_->secondaryColor = normalizeHexColor(settings.secondaryColor, impl_->secondaryColor);
    impl_->tertiaryColor = normalizeHexColor(settings.tertiaryColor, impl_->tertiaryColor);
    impl_->powerEnabled = settings.powerEnabled;
    impl_->applyToStrip();
    impl_->publish();
}

void AudioPlayer::setPowerEnabled(bool enabled) {
    if (impl_ == nullptr) {
        return;
    }

    impl_->powerEnabled = enabled;
    impl_->applyToStrip();
    impl_->publish();
}

uint8_t AudioPlayer::volumePercent() const {
    return impl_ == nullptr ? 0 : impl_->volume;
}

String AudioPlayer::currentTitle() const {
    return impl_ == nullptr ? String("Off") : impl_->title;
}

String AudioPlayer::currentUrl() const {
    return impl_ == nullptr ? String() : impl_->currentUrlSummary();
}

String AudioPlayer::currentState() const {
    return impl_ == nullptr ? String("idle") : impl_->state;
}

String AudioPlayer::effectName(uint16_t index) const {
    if (impl_ == nullptr || impl_->strip == nullptr || index >= impl_->strip->getModeCount()) {
        return "Static";
    }
    const __FlashStringHelper* name = impl_->strip->getModeName(index);
    return name == nullptr ? String("Static") : String(name);
}

uint16_t AudioPlayer::effectCount() const {
    return (impl_ == nullptr || impl_->strip == nullptr) ? 0 : impl_->strip->getModeCount();
}

uint16_t AudioPlayer::findEffectIndex(const String& name) const {
    if (impl_ == nullptr || impl_->strip == nullptr) {
        return 0;
    }

    if (isNumeric(name)) {
        const uint16_t index = static_cast<uint16_t>(name.toInt());
        return index < impl_->strip->getModeCount() ? index : 0;
    }

    String wanted = name;
    wanted.trim();
    wanted.toLowerCase();
    for (uint16_t index = 0; index < impl_->strip->getModeCount(); ++index) {
        String effect = effectName(index);
        effect.toLowerCase();
        if (effect == wanted) {
            return index;
        }
    }
    return 0;
}

void AudioPlayer::onStationName(const char* text) { (void)text; }
void AudioPlayer::onStreamTitle(const char* text) { (void)text; }
void AudioPlayer::onInfo(const char* text) { (void)text; }
void AudioPlayer::onEof(const char* text) { (void)text; }