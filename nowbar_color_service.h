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

// {A7E3B4C1-8F2D-4A6E-B5C9-1D3F7E8A2B4C}
FOOGUIDDECL const GUID nowbar_color_provider::class_guid =
    { 0xa7e3b4c1, 0x8f2d, 0x4a6e, { 0xb5, 0xc9, 0x1d, 0x3f, 0x7e, 0x8a, 0x2b, 0x4c } };

// Called by ControlPanelCore when colors change
void nowbar_notify_color_changed();
