#include "audio_player.h"

#include <WS2812FX.h>

#include "default_config.h"

namespace {
constexpr char AP_MODE_PRIMARY_COLOR[] = "#0000FF";
constexpr unsigned long AP_MODE_PULSE_PERIOD_MS = 2000UL;
constexpr uint8_t AP_MODE_BOOT_PULSE_COUNT = 5;

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
        enum class ApIndicatorMode : uint8_t {
                None,
                ContinuousUntilSaved,
                PulseThenRestore,
        };

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
    bool apIndicatorActive = false;
    bool allowInteractiveIndicatorCancel = false;
    ApIndicatorMode apIndicatorMode = ApIndicatorMode::None;
    unsigned long apIndicatorStartedAt = 0;
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

    uint8_t apIndicatorBrightness() const {
        const uint8_t requestedBrightness = volume < DefaultConfig::DEFAULT_VOLUME_PERCENT ? DefaultConfig::DEFAULT_VOLUME_PERCENT : volume;
        return applyPowerLimit(requestedBrightness, pixelCount, powerLimiterAmps);
    }

    String effectNameForIndex(uint16_t index) const {
        if (strip == nullptr) {
            return "Static";
        }
        const __FlashStringHelper* name = strip->getModeName(index);
        return name == nullptr ? String("Static") : String(name);
    }

    void initializeApIndicatorMode(bool usingSavedSettings) {
        apIndicatorMode = usingSavedSettings ? ApIndicatorMode::PulseThenRestore : ApIndicatorMode::ContinuousUntilSaved;
        apIndicatorStartedAt = 0;
        apIndicatorActive = false;
        allowInteractiveIndicatorCancel = false;
    }

    void dismissApIndicator() {
        apIndicatorMode = ApIndicatorMode::None;
        apIndicatorActive = false;
        apIndicatorStartedAt = 0;
    }

    bool shouldShowApIndicator(const AppStateSnapshot& snapshot) {
        if (snapshot.network.wifiConnected) {
            apIndicatorMode = ApIndicatorMode::None;
            return false;
        }

        if (!snapshot.network.apMode) {
            return false;
        }

        if (apIndicatorMode == ApIndicatorMode::ContinuousUntilSaved) {
            if (snapshot.settings.usingSaved) {
                apIndicatorMode = ApIndicatorMode::None;
                return false;
            }
            return true;
        }

        if (apIndicatorMode != ApIndicatorMode::PulseThenRestore) {
            return false;
        }

        if (apIndicatorStartedAt == 0) {
            return true;
        }

        const unsigned long elapsed = millis() - apIndicatorStartedAt;
        if (elapsed >= (AP_MODE_PULSE_PERIOD_MS * AP_MODE_BOOT_PULSE_COUNT)) {
            apIndicatorMode = ApIndicatorMode::None;
            return false;
        }
        return true;
    }

    void applyApIndicatorFrame(unsigned long now) {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        if (apIndicatorStartedAt == 0) {
            apIndicatorStartedAt = now;
            strip->setMode(FX_MODE_STATIC);
            strip->setColor(parseColor(AP_MODE_PRIMARY_COLOR));
            strip->start();
        }

        const unsigned long elapsed = now - apIndicatorStartedAt;
        const unsigned long cyclePosition = elapsed % AP_MODE_PULSE_PERIOD_MS;
        const float phase = static_cast<float>(cyclePosition) / static_cast<float>(AP_MODE_PULSE_PERIOD_MS);
        const float envelope = phase < 0.5f ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
        const uint8_t brightnessPercent = static_cast<uint8_t>(apIndicatorBrightness() * envelope + 0.5f);

        strip->setBrightness(map(brightnessPercent, 0, 100, 0, 255));
        strip->trigger();
        strip->service();
    }

    void applyToStrip() {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        if (apIndicatorActive) {
            strip->setMode(FX_MODE_STATIC);
            strip->setColor(parseColor(AP_MODE_PRIMARY_COLOR));
            strip->setBrightness(0);
        } else {
            strip->setMode(effectIndex);
            strip->setSpeed(speedToWs2812fx(effectSpeed));
            strip->setColor(parseColor(primaryColor));
            strip->setBrightness(powerEnabled ? map(effectiveBrightness(), 0, 100, 0, 255) : 0);
        }
        strip->start();
        strip->trigger();
        strip->service();

        type = effectNameForIndex(effectIndex);
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
    impl_->initializeApIndicatorMode(appState.snapshot().settings.usingSaved);
    impl_->dataPin = dataPin;
    impl_->pixelCount = pixelCount;
    setVolumePercent(initialBrightnessPercent);
    impl_->applyToStrip();
    impl_->publish();
}

void AudioPlayer::finishStartup() {
    if (impl_ == nullptr) {
        return;
    }

    impl_->allowInteractiveIndicatorCancel = true;
}

void AudioPlayer::loop() {
    if (impl_ == nullptr) {
        return;
    }

    syncStatusIndicators();
    if (impl_->apIndicatorActive) {
        impl_->applyApIndicatorFrame(millis());
    } else if (impl_->strip != nullptr) {
        impl_->strip->service();
    }
}

bool AudioPlayer::play(const String& primaryColor, const String& secondaryColor, const String& effectName, const String& source) {
    if (impl_ == nullptr) {
        return false;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
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

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
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

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
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

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    impl_->powerEnabled = enabled;
    impl_->applyToStrip();
    impl_->publish();
}

void AudioPlayer::syncStatusIndicators() {
    if (impl_ == nullptr || impl_->appState == nullptr) {
        return;
    }

    const AppStateSnapshot snapshot = impl_->appState->snapshot();
    const bool shouldShowApIndicator = impl_->shouldShowApIndicator(snapshot);
    if (impl_->apIndicatorActive == shouldShowApIndicator) {
        return;
    }

    impl_->apIndicatorActive = shouldShowApIndicator;
    if (!shouldShowApIndicator) {
        impl_->apIndicatorStartedAt = 0;
    }
    impl_->applyToStrip();
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
    return impl_->effectNameForIndex(index);
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