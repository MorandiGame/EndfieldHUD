#pragma once
#include "core/data/data_store.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace endfield::app {
// Fork preferences are optional so upstream and older settings remain readable.
struct ResourcePolicy {
    double idleSeconds{60}, wakeIdleSeconds{30}, releaseSeconds{1}, frameRate{60};
    bool desktopResume{true}, pauseTelemetry{true};
    bool observesPresence(bool shown) const { return idleSeconds>0 && (shown||desktopResume); }
    static ResourcePolicy from(const ehud::data::Settings& s) {
        ResourcePolicy p;
        const auto number = [&](const char* key, double fallback, double maximum) {
            const auto& v = s.fields[key];
            return v.isNumber() && std::isfinite(v.number()) ? std::clamp(v.number(), 0., maximum) : fallback;
        };
        p.idleSeconds = number("windowsIdleHideSeconds", 60, 3600);
        p.releaseSeconds = number("windowsGraphicsReleaseSeconds", 1, 600);
        p.frameRate = number("windowsFrameRateLimit", 60, 240);
        const auto& resume = s.fields["windowsDesktopResume"];
        const auto& telemetry = s.fields["windowsPauseHiddenTelemetry"];
        if (resume.isBool()) p.desktopResume = resume.boolean();
        if (telemetry.isBool()) p.pauseTelemetry = telemetry.boolean();
        return p;
    }
};

// One low-frequency host deadline; no input hook, render loop or worker thread.
class DesktopPresence {
public:
    enum class Action { none, hide, reveal };
    void opened(double now) { openedAt_ = now; pause(); }
    void dismissed() { pause(); }
    void pause() { desktopSince_.reset(); wakeArmed_ = false; }
    bool wakeArmed() const { return wakeArmed_; }
    Action sample(const ResourcePolicy& p, double now, double idle, bool moved,
                  bool desktop, bool shown, bool canAct) {
        if (p.idleSeconds <= 0) { dismissed(); return Action::none; }
        // Time spent in another application, a modal or a suspended session
        // never contributes to the desktop's automatic-hide deadline.
        if (!desktop || !canAct) { pause(); return Action::none; }
        if (!desktopSince_) desktopSince_ = now;
        // The away interval must have completed on an earlier sample. A
        // hidden launch or a manual close starts the same waiting sequence;
        // ordinary desktop motion must never summon the HUD immediately.
        if (!shown && p.desktopResume && wakeArmed_ && moved && idle < 2) {
            pause(); return Action::reveal;
        }
        wakeArmed_ = p.desktopResume && idle >= p.wakeIdleSeconds &&
                     now - *desktopSince_ >= p.wakeIdleSeconds;
        if (shown && idle >= p.idleSeconds &&
                   now - std::max(openedAt_, *desktopSince_) >= p.idleSeconds) {
            return Action::hide;
        }
        return Action::none;
    }
private:
    double openedAt_{};
    std::optional<double> desktopSince_;
    bool wakeArmed_{};
};
}
