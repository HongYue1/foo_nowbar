#include "pch.h"
#include "control_panel_dui.h"
#include "../preferences.h"
#include "../artwork_bridge.h"

namespace nowbar {

// Window class name
const wchar_t* ControlPanelDUI::get_class_name() {
    return L"foo_nowbar_dui_element";
}

bool ControlPanelDUI::register_class() {
    static bool registered = false;
    if (registered) return true;
    
    // hbrBackground is stored in the WNDCLASSEXW and must outlive the class registration.
    // We allocate it once here and let it live for the process lifetime (leaked intentionally
    // at process exit, identical to what the OS would do). Using a solid brush here prevents
    // a white flash before the first WM_PAINT; WM_ERASEBKGND returns 1 so the OS never
    // actually paints with this brush during normal operation.
    static HBRUSH s_bg_brush = CreateSolidBrush(RGB(24, 24, 24));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = s_bg_brush;
    wc.lpszClassName = get_class_name();
    
    registered = (RegisterClassExW(&wc) != 0);
    return registered;
}

ui_element_config::ptr ControlPanelDUI::g_get_default_configuration() {
    return ui_element_config::g_create_empty(g_get_guid());
}

ControlPanelDUI::ControlPanelDUI(ui_element_config::ptr config, ui_element_instance_callback::ptr callback)
    : m_config(config)
    , m_callback(callback)
{
}

void ControlPanelDUI::release_gdi_cache() {
    if (m_cache_bitmap) {
        SelectObject(m_cache_dc, m_cache_old_bitmap);
        DeleteObject(m_cache_bitmap);
        m_cache_bitmap = nullptr;
        m_cache_old_bitmap = nullptr;
    }
    if (m_cache_dc) {
        DeleteDC(m_cache_dc);
        m_cache_dc = nullptr;
    }
    m_cache_w = m_cache_h = 0;
}

ControlPanelDUI::~ControlPanelDUI() {
    if (m_hwnd) {
        // Zero GWLP_USERDATA first so WindowProc falls through to DefWindowProc
        // during WM_DESTROY — avoiding a second m_core.reset() via handle_message.
        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    // WM_DESTROY is bypassed (self == null by then), so clean up GDI objects here.
    release_gdi_cache();
}

void ControlPanelDUI::initialize_window(HWND parent) {
    if (!register_class()) return;
    
    m_hwnd = CreateWindowExW(
        0,
        get_class_name(),
        L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE,
        0, 0, 100, 100,
        parent,
        nullptr,
        core_api::get_my_instance(),
        this
    );
}

void ControlPanelDUI::set_configuration(ui_element_config::ptr data) {
    m_config = data;
    // Notify the core that settings may have changed, and tell the host to
    // re-query min/max info (button visibility affects minimum width).
    if (m_core) m_core->on_settings_changed();
    if (m_callback.is_valid()) m_callback->on_min_max_info_change();
}

ui_element_config::ptr ControlPanelDUI::get_configuration() {
    return m_config;
}

ui_element_min_max_info ControlPanelDUI::get_min_max_info() {
    ui_element_min_max_info info;
    
    // GetDpiForWindow is a single cheap call (Windows 10+); fall back to 96 if the
    // window doesn't exist yet (e.g. queried before initialize_window is called).
    int dpi = (m_hwnd && IsWindow(m_hwnd)) ? static_cast<int>(GetDpiForWindow(m_hwnd)) : 96;

    // Minimum height: 0.55 inches, scaled by DPI
    // At 96 DPI: 0.55 * 96 = 53 pixels
    info.m_min_height = static_cast<t_uint32>(0.55 * dpi);
    
    // Maximum height: 1.12 inches, scaled by DPI (~38% total reduction from 1.8)
    // At 96 DPI: 1.12 * 96 = 108 pixels
    // Keeps panel compact and horizontal-focused
    info.m_max_height = static_cast<t_uint32>(1.12 * dpi);
    
    // Fixed minimum width that accommodates all elements at any height
    // Including Super button, spectrum visualizer after Repeat.
    // When volume, miniplayer, and all custom buttons are hidden, allow a
    // smaller minimum width since those right-side elements are absent.
    double base_width = 1232.0;
    {
      bool has_any_cbutton = false;
      for (int i = 0; i < 6; i++) {
        if (get_nowbar_cbutton_enabled(i)) { has_any_cbutton = true; break; }
      }
      bool volume_vis = get_nowbar_volume_icon_visible() || get_nowbar_volume_bar_visible();
      if (!volume_vis && !get_nowbar_miniplayer_icon_visible() && !has_any_cbutton)
        base_width = 832.0;
    }
    info.m_min_width = static_cast<t_uint32>(base_width * dpi / 96.0);
    
    return info;
}

void ControlPanelDUI::update_artwork() {
    // All artwork logic lives in ControlPanelCore::update_artwork() to avoid
    // duplication between the CUI and DUI wrappers.
    if (m_core) m_core->update_artwork();
}

LRESULT CALLBACK ControlPanelDUI::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ControlPanelDUI* self = nullptr;
    
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<ControlPanelDUI*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<ControlPanelDUI*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (self) {
        return self->handle_message(msg, wp, lp);
    }
    
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT ControlPanelDUI::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        m_core = std::make_unique<ControlPanelCore>();
        
        // Set callbacks BEFORE initialize() so they're available when on_settings_changed() is called
        m_core->set_artwork_request_callback([this]() {
            update_artwork();
        });
        
        // Signal the DUI host to re-query min/max info whenever settings change
        // (e.g. button visibility toggles affect minimum panel width).
        m_core->set_relayout_callback([this]() {
            if (m_callback.is_valid()) m_callback->on_min_max_info_change();
        });
        
        // Set color query callback for Custom theme mode (DUI color scheme sync)
        m_core->set_color_query_callback([this](COLORREF& bg, COLORREF& text, COLORREF& highlight, COLORREF& selection) -> bool {
            if (!m_callback.is_valid()) return false;
            try {
                bg = m_callback->query_std_color(ui_color_background);
                text = m_callback->query_std_color(ui_color_text);
                highlight = m_callback->query_std_color(ui_color_highlight);
                selection = m_callback->query_std_color(ui_color_selection);
                return true;
            } catch (...) {
                return false;
            }
        });
        
        // Now initialize (which calls on_settings_changed with callbacks available)
        m_core->initialize(m_hwnd);
        
        update_artwork();
        return 0;
        
    case WM_DESTROY:
        m_core.reset();
        release_gdi_cache();
        return 0;
        
    case WM_SIZE: {
        if (m_core) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        RECT rect;
        GetClientRect(m_hwnd, &rect);

        // Recreate cached offscreen bitmap only when window size changes
        if (rect.right != m_cache_w || rect.bottom != m_cache_h || !m_cache_dc) {
            if (m_cache_bitmap) { SelectObject(m_cache_dc, m_cache_old_bitmap); DeleteObject(m_cache_bitmap); m_cache_bitmap = nullptr; }
            if (m_cache_dc) { DeleteDC(m_cache_dc); m_cache_dc = nullptr; }
            m_cache_dc = CreateCompatibleDC(hdc);
            m_cache_bitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
            m_cache_old_bitmap = (HBITMAP)SelectObject(m_cache_dc, m_cache_bitmap);
            m_cache_w = rect.right;
            m_cache_h = rect.bottom;
            if (m_core) m_core->force_full_repaint();
        }

        // Spectrum-only fast path: skip background/artwork/text/buttons redraw
        bool spectrum_fast = m_core && m_core->is_spectrum_animating_only() &&
                             get_nowbar_visualization_mode() == 1;
        // Waveform-only fast path: skip background/artwork/text/buttons redraw
        bool waveform_fast = m_core && m_core->is_waveform_progress_only() &&
                             get_nowbar_visualization_mode() == 2;
        if (spectrum_fast) {
            // Background cache in paint_spectrum_only covers the dirty areas — no clear needed
            m_core->paint_spectrum_only(m_cache_dc, rect);
        } else if (waveform_fast) {
            m_core->clear_waveform_dirty_rects(m_cache_dc, m_core->get_bg_colorref());
            m_core->paint_waveform_only(m_cache_dc, rect);
        } else {
            {
                HBRUSH bgBrush = CreateSolidBrush(m_core ? m_core->get_bg_colorref() : get_nowbar_initial_bg_color());
                FillRect(m_cache_dc, &rect, bgBrush);
                DeleteObject(bgBrush);
            }
            if (m_core) {
                m_core->paint(m_cache_dc, rect);
            }
        }

        BitBlt(hdc, 0, 0, rect.right, rect.bottom, m_cache_dc, 0, 0, SRCCOPY);

        EndPaint(m_hwnd, &ps);
        return 0;
    }
        
    case WM_ERASEBKGND: {
        // All painting is double-buffered (offscreen cache + BitBlt), so erasing
        // the screen DC here is unnecessary.  During resize the system can set the
        // erase flag between spectrum animation frames; if we fill the screen DC
        // and the next WM_PAINT only BitBlts the spectrum area (partial update
        // region), the non-spectrum areas keep the erase fill, producing a visible
        // black box.  Returning 1 without painting avoids this.
        return 1;
    }
        
    case WM_MOUSEMOVE:
        if (!m_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
            TrackMouseEvent(&tme);
            m_tracking_mouse = true;
        }
        if (m_core) {
            m_core->on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_MOUSELEAVE:
        m_tracking_mouse = false;
        if (m_core) {
            m_core->on_mouse_leave();
        }
        return 0;
        
    case WM_LBUTTONDOWN:
        SetCapture(m_hwnd);
        if (m_core) {
            m_core->on_lbutton_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (m_core) {
            m_core->on_lbutton_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_LBUTTONDBLCLK:
        if (m_core) {
            m_core->on_lbutton_dblclk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
        
    case WM_MOUSEWHEEL:
        if (m_core) {
            m_core->on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        }
        return 0;
        
    case WM_SETTINGCHANGE:
        // System settings changed (accent colour, dark/light mode, etc.)
        if (m_core) m_core->on_settings_changed();
        return 0;

    case WM_DPICHANGED: {
        int new_dpi = HIWORD(wp);
        if (m_core) m_core->update_dpi(static_cast<float>(new_dpi) / 96.0f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case ControlPanelCore::WM_NOWBAR_ANIMATE: {
        if (m_core) m_core->on_animation_timer_fired();
        const RECT* dirty = m_core ? m_core->get_animation_dirty_rect() : nullptr;
        InvalidateRect(m_hwnd, dirty, FALSE);
        if (m_core) m_core->clear_animation_dirty();
        UpdateWindow(m_hwnd);
        return 0;
    }

    case WM_TIMER: {
        UINT_PTR timer_id = static_cast<UINT_PTR>(wp);
        if (timer_id == ControlPanelCore::COMMAND_STATE_TIMER_ID) {
            if (m_core) m_core->poll_custom_button_states();
        } else if (timer_id == ControlPanelCore::SHOW_PREFS_TIMER_ID) {
            if (m_core) m_core->do_show_preferences();
        }
        return 0;
    }

    }

    return DefWindowProc(m_hwnd, msg, wp, lp);
}

// DUI factory - simple implementation without WTL
class ControlPanelDUIElement : public ui_element {
public:
    GUID get_guid() override { return ControlPanelDUI::g_get_guid(); }
    GUID get_subclass() override { return ControlPanelDUI::g_get_subclass(); }
    void get_name(pfc::string_base& out) override { ControlPanelDUI::g_get_name(out); }
    
    ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback) override {
        PFC_ASSERT(cfg->get_guid() == get_guid());
        auto item = fb2k::service_new<ControlPanelDUI>(cfg, callback);
        item->initialize_window(parent);
        return item;
    }
    
    ui_element_config::ptr get_default_configuration() override {
        return ControlPanelDUI::g_get_default_configuration();
    }
    
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) override {
        return nullptr;
    }
    
    bool get_description(pfc::string_base& out) override {
        out = ControlPanelDUI::g_get_description();
        return true;
    }
};

static service_factory_single_t<ControlPanelDUIElement> g_dui_factory;

} // namespace nowbar
