#include "pch.h"
#include "nowbar_color_service.h"
#include "core/control_panel_core.h"
#include "preferences.h"  // provides get_nowbar_background_style() and other config accessors
#include <vector>
#include <mutex>

namespace {

class nowbar_color_provider_impl : public nowbar_color_provider {
public:
    void get_resolved_bg_color(uint8_t& r, uint8_t& g, uint8_t& b) override {
        auto* core = get_first_instance();
        if (!core) {
            r = 24; g = 24; b = 24;
            return;
        }

        int bg_style = get_nowbar_background_style();
        bool has_artwork_bg = (bg_style == 1 || bg_style == 2) && core->artwork_colors_valid();

        if (has_artwork_bg) {
            auto primary = core->get_artwork_primary();
            int pr = primary.GetR(), pg = primary.GetG(), pb = primary.GetB();
            BYTE ov = (bg_style == 1) ? (core->get_dark_mode() ? 120 : 80)
                                       : (core->get_dark_mode() ? 140 : 180);
            if (core->get_dark_mode()) {
                // Dark mode: blend artwork color toward black
                r = static_cast<uint8_t>(pr * (255 - ov) / 255);
                g = static_cast<uint8_t>(pg * (255 - ov) / 255);
                b = static_cast<uint8_t>(pb * (255 - ov) / 255);
            } else {
                // Light mode: blend artwork color toward white
                r = static_cast<uint8_t>(pr + (255 - pr) * ov / 255);
                g = static_cast<uint8_t>(pg + (255 - pg) * ov / 255);
                b = static_cast<uint8_t>(pb + (255 - pb) * ov / 255);
            }
        } else {
            COLORREF bg = core->get_bg_colorref();
            r = GetRValue(bg);
            g = GetGValue(bg);
            b = GetBValue(bg);
        }
    }

    void get_artwork_primary_color(uint8_t& r, uint8_t& g, uint8_t& b, bool& valid) override {
        auto* core = get_first_instance();
        if (!core || !core->artwork_colors_valid()) {
            r = 0; g = 0; b = 0;
            valid = false;
            return;
        }
        auto primary = core->get_artwork_primary();
        r = primary.GetR();
        g = primary.GetG();
        b = primary.GetB();
        valid = true;
    }

    bool is_dark_mode() override {
        auto* core = get_first_instance();
        return core ? core->get_dark_mode() : true;
    }

    void register_listener(nowbar_color_listener* listener) override {
        std::lock_guard<std::mutex> lock(s_listener_mutex);
        s_listeners.push_back(listener);
    }

    void unregister_listener(nowbar_color_listener* listener) override {
        std::lock_guard<std::mutex> lock(s_listener_mutex);
        s_listeners.erase(
            std::remove(s_listeners.begin(), s_listeners.end(), listener),
            s_listeners.end());
    }

    static void notify_color_changed() {
        {
            std::lock_guard<std::mutex> lock(s_listener_mutex);
            if (s_pending_notify) return;
            s_pending_notify = true;
        }
        // Lock released before inMainThread2 — if it executes synchronously
        // (already on main thread), the lambda can safely acquire the mutex.
        fb2k::inMainThread2([]{
            std::lock_guard<std::mutex> lock(s_listener_mutex);
            s_pending_notify = false;
            for (auto* listener : s_listeners) {
                listener->on_color_changed();
            }
        });
    }

private:
    static nowbar::ControlPanelCore* get_first_instance() {
        return nowbar::ControlPanelCore::get_first_instance();
    }

    static std::vector<nowbar_color_listener*> s_listeners;
    static std::mutex s_listener_mutex;
    static bool s_pending_notify;
};

std::vector<nowbar_color_listener*> nowbar_color_provider_impl::s_listeners;
std::mutex nowbar_color_provider_impl::s_listener_mutex;
bool nowbar_color_provider_impl::s_pending_notify = false;

static service_factory_single_t<nowbar_color_provider_impl> g_nowbar_color_provider_factory;

} // anonymous namespace

// Free function called from ControlPanelCore notification sites
void nowbar_notify_color_changed() {
    nowbar_color_provider_impl::notify_color_changed();
}
