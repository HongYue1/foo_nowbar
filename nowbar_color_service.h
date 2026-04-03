#pragma once
#include <SDK/foobar2000.h>
#include <cstdint>

// Listener interface — implemented by consumers (e.g., foo_playhistory)
class NOVTABLE nowbar_color_listener {
public:
    virtual void on_color_changed() = 0;
protected:
    ~nowbar_color_listener() = default;
};

// Provider interface — implemented by foo_nowbar, queried by consumers
class NOVTABLE nowbar_color_provider : public service_base {
    FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT(nowbar_color_provider)
public:
    // Returns the composited background color the user actually sees.
    virtual void get_resolved_bg_color(uint8_t& r, uint8_t& g, uint8_t& b) = 0;

    // Returns the artwork's dominant color for accent derivation.
    // Sets valid=false when no artwork is loaded.
    virtual void get_artwork_primary_color(uint8_t& r, uint8_t& g, uint8_t& b, bool& valid) = 0;

    // Whether nowbar is currently using dark foreground mode.
    virtual bool is_dark_mode() = 0;

    virtual void register_listener(nowbar_color_listener* listener) = 0;
    virtual void unregister_listener(nowbar_color_listener* listener) = 0;
};

// Authoritative GUID definition lives in nowbar_color_service_impl.cpp.
// Defining it here would produce a duplicate-symbol error (ODR violation)
// because this header is included by more than one translation unit.

// Called by ControlPanelCore when colors change
void nowbar_notify_color_changed();
