#include "light_player.h"

#include <algorithm>
#include <vector>

#include <ArduinoJson.h>
#include <WS2812FX.h>

#include "default_config.h"

namespace {
constexpr char AP_MODE_PRIMARY_COLOR[] = "#0000FF";
constexpr unsigned long AP_MODE_PULSE_PERIOD_MS = 2000UL;
constexpr uint8_t AP_MODE_BOOT_PULSE_COUNT = 5;
constexpr char DEFAULT_WIFI_STATUS_EFFECT[] = "Scan";
constexpr char OTA_PROGRESS_ACTIVE_COLOR[] = "#00D26A";
constexpr char OTA_PROGRESS_INACTIVE_COLOR[] = "#081018";
constexpr char MANUAL_PIXELS_TITLE[] = "Manual Pixels";
constexpr unsigned long COLOR_TRANSITION_DURATION_MS = 1000UL;

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

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

RgbColor unpackColor(const String& color) {
    const uint32_t packed = parseColor(color);
    return RgbColor{
        static_cast<uint8_t>((packed >> 16) & 0xFF),
        static_cast<uint8_t>((packed >> 8) & 0xFF),
        static_cast<uint8_t>(packed & 0xFF),
    };
}

String packColor(const RgbColor& color) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.red, color.green, color.blue);
    return String(buffer);
}

uint8_t interpolateChannel(uint8_t start, uint8_t target, uint16_t progressScaled) {
    const int delta = static_cast<int>(target) - static_cast<int>(start);
    const int value = static_cast<int>(start) + ((delta * static_cast<int>(progressScaled)) / 1000);
    return static_cast<uint8_t>(constrain(value, 0, 255));
}

String interpolateColor(const String& startColor, const String& targetColor, uint16_t progressScaled) {
    const RgbColor start = unpackColor(startColor);
    const RgbColor target = unpackColor(targetColor);
    return packColor({
        interpolateChannel(start.red, target.red, progressScaled),
        interpolateChannel(start.green, target.green, progressScaled),
        interpolateChannel(start.blue, target.blue, progressScaled),
    });
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

String normalizeCommandColor(const String& raw, bool& valid) {
    String value = raw;
    value.trim();
    value.toLowerCase();
    if (value.isEmpty()) {
        valid = false;
        return String();
    }

    if (value == "off" || value == "clear" || value == "none" || value == "black" || value == "0") {
        valid = true;
        return "#000000";
    }

    if (!value.startsWith("#")) {
        value = "#" + value;
    }
    if (value.length() != 7) {
        valid = false;
        return String();
    }

    for (size_t index = 1; index < value.length(); ++index) {
        if (!isHexadecimalDigit(value[index])) {
            valid = false;
            return String();
        }
    }

    value.toUpperCase();
    valid = true;
    return value;
}
}  // namespace

class LightPlayer::Impl {
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
    uint16_t colorTransitionMs = 1000;
    String apModeEffect = DEFAULT_WIFI_STATUS_EFFECT;
    bool powerEnabled = true;
    bool pixelOverrideActive = false;
    std::vector<uint32_t> pixelColors;
    bool otaProgressActive = false;
    uint8_t otaProgressPercent = 0;
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
    String renderedPrimaryColor = "#FFFFFF";
    String renderedSecondaryColor = "#000000";
    String transitionStartPrimaryColor = "#FFFFFF";
    String transitionStartSecondaryColor = "#000000";
    bool colorTransitionActive = false;
    unsigned long colorTransitionStartedAt = 0;
    String source = "manual";

    ~Impl() {
        delete strip;
    }

    void ensurePixelBuffer() {
        if (pixelColors.size() != pixelCount) {
            pixelColors.assign(pixelCount, 0);
        }
    }

    void resetPixelBuffer() {
        ensurePixelBuffer();
        std::fill(pixelColors.begin(), pixelColors.end(), 0);
    }

    void deactivatePixelOverride() {
        pixelOverrideActive = false;
        pixelColors.clear();
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
        uint32_t initialColors[] = {
            parseColor(primaryColor),
            parseColor(secondaryColor),
            parseColor(tertiaryColor),
        };
        strip->setSegment(0, 0, pixelCount - 1, FX_MODE_STATIC, initialColors, speedToWs2812fx(effectSpeed));
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

    uint16_t resolveEffectIndex(const String& name, uint16_t fallbackIndex = 0) const {
        if (strip == nullptr) {
            return fallbackIndex;
        }

        if (isNumeric(name)) {
            const uint16_t index = static_cast<uint16_t>(name.toInt());
            return index < strip->getModeCount() ? index : fallbackIndex;
        }

        String wanted = name;
        wanted.trim();
        wanted.toLowerCase();
        for (uint16_t index = 0; index < strip->getModeCount(); ++index) {
            String effect = effectNameForIndex(index);
            effect.toLowerCase();
            if (effect == wanted) {
                return index;
            }
        }

        return fallbackIndex;
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
            strip->start();
        }

        uint8_t brightnessPercent = apIndicatorBrightness();
        if (apIndicatorMode == ApIndicatorMode::PulseThenRestore) {
            const unsigned long elapsed = now - apIndicatorStartedAt;
            const unsigned long cyclePosition = elapsed % AP_MODE_PULSE_PERIOD_MS;
            const float phase = static_cast<float>(cyclePosition) / static_cast<float>(AP_MODE_PULSE_PERIOD_MS);
            const float envelope = phase < 0.5f ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
            brightnessPercent = static_cast<uint8_t>(apIndicatorBrightness() * envelope + 0.5f);
        }

        uint32_t effectColors[] = {
            parseColor(primaryColor),
            parseColor(secondaryColor),
            parseColor(tertiaryColor),
        };
        const uint16_t effectIndex = resolveEffectIndex(apModeEffect, resolveEffectIndex(DEFAULT_WIFI_STATUS_EFFECT, FX_MODE_SCAN));
        strip->setSegment(0, 0, pixelCount - 1, effectIndex, effectColors, speedToWs2812fx(effectSpeed));
        strip->setBrightness(map(brightnessPercent, 0, 100, 0, 255));
        strip->start();
        strip->trigger();
        strip->service();
    }

    void renderPixelOverride() {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        ensurePixelBuffer();
        strip->setMode(FX_MODE_STATIC);
        strip->setBrightness(powerEnabled ? map(effectiveBrightness(), 0, 100, 0, 255) : 0);
        for (uint16_t index = 0; index < pixelCount; ++index) {
            strip->setPixelColor(index, pixelColors[index]);
        }
        strip->show();
    }

    void renderPoweredOff() {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        strip->stop();
        strip->setMode(FX_MODE_STATIC);
        strip->setBrightness(0);
        for (uint16_t index = 0; index < pixelCount; ++index) {
            strip->setPixelColor(index, 0);
        }
        strip->show();
    }

    void renderOtaProgress() {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        const uint16_t filledPixels = otaProgressPercent == 0
            ? 0
            : static_cast<uint16_t>((static_cast<uint32_t>(otaProgressPercent) * pixelCount + 99U) / 100U);
        const uint32_t activeColor = parseColor(OTA_PROGRESS_ACTIVE_COLOR);
        const uint32_t inactiveColor = parseColor(OTA_PROGRESS_INACTIVE_COLOR);

        strip->setMode(FX_MODE_STATIC);
        strip->setBrightness(map(apIndicatorBrightness(), 0, 100, 0, 255));
        for (uint16_t index = 0; index < pixelCount; ++index) {
            strip->setPixelColor(index, index < filledPixels ? activeColor : inactiveColor);
        }
        strip->show();
    }

    void renderEffectMode() {
        rebuildStripIfNeeded();
        if (strip == nullptr) {
            return;
        }

        uint32_t effectColors[] = {
            parseColor(renderedPrimaryColor),
            parseColor(renderedSecondaryColor),
            parseColor(tertiaryColor),
        };
        strip->setSegment(0, 0, pixelCount - 1, effectIndex, effectColors, speedToWs2812fx(effectSpeed));
        strip->setBrightness(powerEnabled ? map(effectiveBrightness(), 0, 100, 0, 255) : 0);
        strip->start();
        strip->trigger();
        strip->service();
    }

    void snapRenderedColorsToTarget() {
        renderedPrimaryColor = primaryColor;
        renderedSecondaryColor = secondaryColor;
        transitionStartPrimaryColor = renderedPrimaryColor;
        transitionStartSecondaryColor = renderedSecondaryColor;
        colorTransitionActive = false;
        colorTransitionStartedAt = 0;
    }

    void beginColorTransition(bool fadeFromBlack) {
        if (!powerEnabled || pixelOverrideActive || otaProgressActive || apIndicatorActive || colorTransitionMs == 0) {
            snapRenderedColorsToTarget();
            return;
        }

        if (fadeFromBlack) {
            renderedPrimaryColor = "#000000";
            renderedSecondaryColor = "#000000";
        }

        if (renderedPrimaryColor == primaryColor && renderedSecondaryColor == secondaryColor) {
            colorTransitionActive = false;
            colorTransitionStartedAt = 0;
            return;
        }

        transitionStartPrimaryColor = renderedPrimaryColor;
        transitionStartSecondaryColor = renderedSecondaryColor;
        colorTransitionActive = true;
        colorTransitionStartedAt = millis();
    }

    bool advanceColorTransition(unsigned long now) {
        if (!colorTransitionActive) {
            return false;
        }

        const unsigned long elapsed = now - colorTransitionStartedAt;
        if (elapsed >= colorTransitionMs) {
            snapRenderedColorsToTarget();
            return true;
        }

        const uint16_t progressScaled = static_cast<uint16_t>((elapsed * 1000UL) / colorTransitionMs);
        const String nextPrimary = interpolateColor(transitionStartPrimaryColor, primaryColor, progressScaled);
        const String nextSecondary = interpolateColor(transitionStartSecondaryColor, secondaryColor, progressScaled);
        const bool changed = nextPrimary != renderedPrimaryColor || nextSecondary != renderedSecondaryColor;
        renderedPrimaryColor = nextPrimary;
        renderedSecondaryColor = nextSecondary;
        return changed;
    }

    void renderCurrentOutput() {
        if (otaProgressActive) {
            renderOtaProgress();
            return;
        }
        if (apIndicatorActive) {
            applyApIndicatorFrame(millis());
            return;
        }
        if (!powerEnabled) {
            renderPoweredOff();
            return;
        }
        if (pixelOverrideActive) {
            renderPixelOverride();
            return;
        }
        renderEffectMode();
    }

    void applyToStrip(bool fadeFromBlack = false) {
        if (pixelOverrideActive) {
            type = MANUAL_PIXELS_TITLE;
            title = powerEnabled ? String(MANUAL_PIXELS_TITLE) : String("Off");
            state = powerEnabled ? "playing" : "idle";
        } else {
            type = effectNameForIndex(effectIndex);
            title = powerEnabled ? type : String("Off");
            state = powerEnabled ? "playing" : "idle";
        }
        beginColorTransition(fadeFromBlack);
        renderCurrentOutput();
    }

    String currentUrlSummary() const {
        if (pixelOverrideActive) {
            size_t litPixels = 0;
            for (uint32_t color : pixelColors) {
                if (color != 0) {
                    ++litPixels;
                }
            }

            String summary = String(MANUAL_PIXELS_TITLE);
            summary += " | ";
            summary += String(litPixels);
            summary += "/";
            summary += String(pixelCount);
            summary += " px lit";
            return summary;
        }

        String summary = effectNameForIndex(effectIndex);
        summary += " | ";
        summary += primaryColor;
        summary += " | ";
        summary += secondaryColor;
        summary += " | ";
        summary += String(pixelCount);
        summary += " px | ";
        summary += String(powerLimiterAmps, 1);
        summary += " A";
        return summary;
    }

    void publish() {
        if (appState != nullptr) {
            appState->setPlayback(state, type, title, currentUrlSummary(), primaryColor, source, volume, colorTransitionMs, powerEnabled);
        }
    }
};

void LightPlayer::begin(uint8_t dataPin, uint16_t pixelCount, uint8_t initialBrightnessPercent, AppState& appState) {
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

void LightPlayer::finishStartup() {
    if (impl_ == nullptr) {
        return;
    }

    impl_->allowInteractiveIndicatorCancel = true;
}

void LightPlayer::loop() {
    if (impl_ == nullptr) {
        return;
    }

    syncStatusIndicators();
    if (impl_->apIndicatorActive) {
        impl_->applyApIndicatorFrame(millis());
    } else if (impl_->otaProgressActive || impl_->pixelOverrideActive) {
        impl_->renderCurrentOutput();
    } else if (impl_->strip != nullptr && impl_->powerEnabled && impl_->advanceColorTransition(millis())) {
        impl_->renderEffectMode();
    } else if (impl_->strip != nullptr && impl_->powerEnabled) {
        impl_->strip->service();
    }
}

bool LightPlayer::play(const String& primaryColor, const String& secondaryColor, const String& effectName, const String& source) {
    if (impl_ == nullptr) {
        return false;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    const bool wasPowerEnabled = impl_->powerEnabled;
    impl_->deactivatePixelOverride();

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
    impl_->applyToStrip(!wasPowerEnabled);
    impl_->publish();
    return true;
}

void LightPlayer::stop() {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    impl_->deactivatePixelOverride();

    impl_->powerEnabled = false;
    impl_->source = "manual";
    impl_->applyToStrip();
    impl_->publish();
}

void LightPlayer::setVolumePercent(uint8_t brightnessPercent) {
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

void LightPlayer::applyLightSettings(const LightSettings& settings) {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    const bool wasPowerEnabled = impl_->powerEnabled;
    impl_->deactivatePixelOverride();

    impl_->pixelCount = settings.pixelCount;
    impl_->dataPin = settings.dataPin;
    impl_->powerLimiterAmps = settings.powerLimiterAmps;
    impl_->colorTransitionMs = settings.colorTransitionMs;
    impl_->apModeEffect = settings.apModeEffect;
    impl_->effectIndex = settings.effectIndex;
    impl_->effectSpeed = settings.effectSpeed;
    impl_->effectIntensity = settings.effectIntensity;
    impl_->primaryColor = normalizeHexColor(settings.primaryColor, impl_->primaryColor);
    impl_->secondaryColor = normalizeHexColor(settings.secondaryColor, impl_->secondaryColor);
    impl_->tertiaryColor = normalizeHexColor(settings.tertiaryColor, impl_->tertiaryColor);
    impl_->powerEnabled = settings.powerEnabled;
    impl_->applyToStrip(!wasPowerEnabled && impl_->powerEnabled);
    impl_->publish();
}

void LightPlayer::setPowerEnabled(bool enabled) {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    const bool wasPowerEnabled = impl_->powerEnabled;
    impl_->powerEnabled = enabled;
    impl_->applyToStrip(!wasPowerEnabled && impl_->powerEnabled);
    impl_->publish();
}

bool LightPlayer::applyPixelCommand(const String& payload, const String& source, String& error) {
    if (impl_ == nullptr) {
        error = "Light engine is not initialized.";
        return false;
    }

    JsonDocument doc;
    const DeserializationError parseError = deserializeJson(doc, payload);
    if (parseError != DeserializationError::Ok) {
        error = "Pixel command must be valid JSON.";
        return false;
    }

    JsonVariantConst root = doc.as<JsonVariantConst>();
    JsonArrayConst updates;
    JsonArrayConst ranges;
    JsonObjectConst command;
    bool clearFirst = false;
    bool modified = false;

    if (root.is<JsonObjectConst>()) {
        command = root.as<JsonObjectConst>();
        if (command["restore"] | false) {
            clearPixelOverride(source);
            return true;
        }

        clearFirst = command["clear"] | false;

        if (!command["brightness"].isNull()) {
            impl_->volume = constrain(command["brightness"].as<int>(), 0, 100);
            modified = true;
        }

        if (!command["fill"].isNull()) {
            bool colorValid = false;
            const String fillColor = normalizeCommandColor(String(static_cast<const char*>(command["fill"] | "")), colorValid);
            if (!colorValid) {
                error = "Fill color must be a #RRGGBB value or 'off'.";
                return false;
            }
            impl_->resetPixelBuffer();
            std::fill(impl_->pixelColors.begin(), impl_->pixelColors.end(), parseColor(fillColor));
            modified = true;
        }

        if (command["ranges"].is<JsonArrayConst>()) {
            ranges = command["ranges"].as<JsonArrayConst>();
        }

        if (command["pixels"].is<JsonArrayConst>()) {
            updates = command["pixels"].as<JsonArrayConst>();
        }
    } else if (root.is<JsonArrayConst>()) {
        updates = root.as<JsonArrayConst>();
    } else {
        error = "Pixel command root must be a JSON object or array.";
        return false;
    }

    if (clearFirst || modified || ranges.size() > 0 || updates.size() > 0) {
        impl_->ensurePixelBuffer();
    }
    if (clearFirst) {
        std::fill(impl_->pixelColors.begin(), impl_->pixelColors.end(), 0);
        modified = true;
    }

    for (JsonVariantConst rangeEntry : ranges) {
        if (!rangeEntry.is<JsonObjectConst>()) {
            continue;
        }

        const JsonObjectConst rangeObject = rangeEntry.as<JsonObjectConst>();
        const int start = rangeObject["start"] | 0;
        const int end = rangeObject["end"] | start;
        bool colorValid = false;
        const String rangeColor = normalizeCommandColor(String(static_cast<const char*>(rangeObject["color"] | "")), colorValid);
        if (!colorValid) {
            error = "Range color must be a #RRGGBB value or 'off'.";
            return false;
        }

        const int first = max(0, min(start, end));
        const int last = min(static_cast<int>(impl_->pixelCount) - 1, max(start, end));
        for (int index = first; index <= last; ++index) {
            impl_->pixelColors[static_cast<size_t>(index)] = parseColor(rangeColor);
        }
        modified = true;
    }

    size_t arrayIndex = 0;
    for (JsonVariantConst update : updates) {
        int index = static_cast<int>(arrayIndex);
        String colorValue;

        if (update.is<JsonObjectConst>()) {
            const JsonObjectConst item = update.as<JsonObjectConst>();
            index = item["index"] | index;
            colorValue = String(static_cast<const char*>(item["color"] | ""));
        } else if (!update.isNull()) {
            colorValue = String(static_cast<const char*>(update.as<const char*>()));
        }

        ++arrayIndex;
        if (colorValue.isEmpty() || index < 0 || index >= static_cast<int>(impl_->pixelCount)) {
            continue;
        }

        bool colorValid = false;
        const String normalizedColor = normalizeCommandColor(colorValue, colorValid);
        if (!colorValid) {
            error = "Pixel colors must be #RRGGBB values or 'off'.";
            return false;
        }

        impl_->pixelColors[static_cast<size_t>(index)] = parseColor(normalizedColor);
        modified = true;
    }

    if (!modified) {
        error = "Pixel command did not contain any changes.";
        return false;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    impl_->source = source.isEmpty() ? String("manual") : source;
    impl_->powerEnabled = true;
    impl_->pixelOverrideActive = true;
    impl_->applyToStrip();
    impl_->publish();
    return true;
}

bool LightPlayer::setPixelColor(uint16_t index, const String& color, const String& source, String& error) {
    if (impl_ == nullptr) {
        error = "Light engine is not initialized.";
        return false;
    }
    if (index >= impl_->pixelCount) {
        error = "Pixel index is out of range.";
        return false;
    }

    bool colorValid = false;
    const String normalizedColor = normalizeCommandColor(color, colorValid);
    if (!colorValid) {
        error = "Pixel color must be a #RRGGBB value or 'off'.";
        return false;
    }

    if (impl_->allowInteractiveIndicatorCancel && impl_->apIndicatorActive) {
        impl_->dismissApIndicator();
    }

    impl_->ensurePixelBuffer();
    impl_->pixelColors[index] = parseColor(normalizedColor);
    impl_->pixelOverrideActive = true;
    impl_->powerEnabled = true;
    impl_->source = source.isEmpty() ? String("manual") : source;
    impl_->applyToStrip();
    impl_->publish();
    return true;
}

void LightPlayer::clearPixelOverride(const String& source) {
    if (impl_ == nullptr || !impl_->pixelOverrideActive) {
        return;
    }

    impl_->source = source.isEmpty() ? String("manual") : source;
    impl_->deactivatePixelOverride();
    impl_->applyToStrip();
    impl_->publish();
}

void LightPlayer::setOtaProgress(uint8_t progressPercent, bool active) {
    if (impl_ == nullptr) {
        return;
    }

    const uint8_t clamped = constrain(progressPercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    if (impl_->otaProgressActive == active && impl_->otaProgressPercent == clamped) {
        return;
    }

    impl_->otaProgressActive = active;
    impl_->otaProgressPercent = clamped;
    impl_->renderCurrentOutput();
}

void LightPlayer::syncStatusIndicators() {
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
    impl_->renderCurrentOutput();
}

uint8_t LightPlayer::volumePercent() const {
    return impl_ == nullptr ? 0 : impl_->volume;
}

String LightPlayer::currentTitle() const {
    return impl_ == nullptr ? String("Off") : impl_->title;
}

String LightPlayer::currentUrl() const {
    return impl_ == nullptr ? String() : impl_->currentUrlSummary();
}

String LightPlayer::currentState() const {
    return impl_ == nullptr ? String("idle") : impl_->state;
}

String LightPlayer::effectName(uint16_t index) const {
    if (impl_ == nullptr || impl_->strip == nullptr || index >= impl_->strip->getModeCount()) {
        return "Static";
    }
    return impl_->effectNameForIndex(index);
}

uint16_t LightPlayer::effectCount() const {
    return (impl_ == nullptr || impl_->strip == nullptr) ? 0 : impl_->strip->getModeCount();
}

uint16_t LightPlayer::findEffectIndex(const String& name) const {
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

void LightPlayer::onStationName(const char* text) { (void)text; }
void LightPlayer::onStreamTitle(const char* text) { (void)text; }
void LightPlayer::onInfo(const char* text) { (void)text; }
void LightPlayer::onEof(const char* text) { (void)text; }