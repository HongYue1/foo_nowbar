#include "pch.h"
#include "control_panel_cui.h"
#include "../preferences.h"
#include "../artwork_bridge.h"

namespace nowbar {

// Register with Columns UI
uie::window_factory<ControlPanelCUI> g_cui_factory;

void ControlPanelCUI::initialize_core(HWND wnd) {
    if (!m_core) {
        m_core = std::make_unique<ControlPanelCore>();
        
        // Set callbacks BEFORE initialize() so they're available when on_settings_changed() is called
        
        // Set artwork request callback
        m_core->set_artwork_request_callback([this]() {
            update_artwork();
        });
        
        // Set color query callback for Custom theme mode (CUI global color scheme sync)
        m_core->set_color_query_callback([](COLORREF& bg, COLORREF& text, COLORREF& highlight, COLORREF& selection) -> bool {
            try {
                cui::colours::helper colour_helper;
                bg         = colour_helper.get_colour(cui::colours::colour_background);
                text       = colour_helper.get_colour(cui::colours::colour_text);
                highlight  = colour_helper.get_colour(cui::colours::colour_active_item_frame);
                selection  = colour_helper.get_colour(cui::colours::colour_selection_background);
                return true;
            } catch (...) {
                return false;
            }
        });
        
        // Register for CUI colour change notifications (colour/font palette changes)
        m_colour_callback = std::make_unique<ColourCallback>(this);
        if (fb2k::std_api_try_get(m_colour_manager)) {
            m_colour_manager->register_common_callback(m_colour_callback.get());
        }

        // v8.0.0: use dark_mode_notifier for SetWindowTheme — cleaner than common_callback
        m_dark_notifier = std::make_unique<cui::colours::dark_mode_notifier>([wnd, this] {
            const bool is_dark = cui::colours::is_dark_mode_active();
            SetWindowTheme(wnd, is_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            if (m_core) m_core->on_settings_changed();
        });
        // Apply the correct theme immediately on creation
        {
            const bool is_dark = cui::colours::is_dark_mode_active();
            SetWindowTheme(wnd, is_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        }
        
        // Now initialize (which calls on_settings_changed with callbacks available)
        m_core->initialize(wnd);
        
        // Load artwork for current track
        update_artwork();
    }
}


void ControlPanelCUI::update_artwork() {
    // All artwork logic lives in ControlPanelCore::update_artwork() to avoid
    // duplication between the CUI and DUI wrappers.
    if (m_core) m_core->update_artwork();
}

LRESULT ControlPanelCUI::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        initialize_core(wnd);
        return 0;
        
    case WM_DESTROY:
        // Tear down dark-mode notifier before anything else
        m_dark_notifier.reset();
        // Unregister colour callback before destroying
        if (m_colour_manager.is_valid() && m_colour_callback) {
            m_colour_manager->deregister_common_callback(m_colour_callback.get());
        }
        m_colour_callback.reset();
        m_colour_manager.release();
        m_core.reset();
        // Release cached offscreen bitmap
        if (m_cache_bitmap) { SelectObject(m_cache_dc, m_cache_old_bitmap); DeleteObject(m_cache_bitmap); m_cache_bitmap = nullptr; }
        if (m_cache_dc) { DeleteDC(m_cache_dc); m_cache_dc = nullptr; }
        m_cache_w = m_cache_h = 0;
        return 0;
        
    case WM_SIZE: {
        if (m_core) {
            InvalidateRect(wnd, nullptr, FALSE);
        }
        return 0;
    }
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(wnd, &ps);

        RECT rect;
        GetClientRect(wnd, &rect);

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

        EndPaint(wnd, &ps);
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
        
    case WM_MOUSEMOVE: {
        if (!m_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme);
            m_tracking_mouse = true;
        }
        if (m_core) {
            m_core->on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        }
        return 0;
    }
        
    case WM_MOUSELEAVE:
        m_tracking_mouse = false;
        if (m_core) {
            m_core->on_mouse_leave();
        }
        return 0;
        
    case WM_LBUTTONDOWN:
        SetCapture(wnd);
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
        // System settings changed - trigger theme update
        if (m_core) {
            m_core->on_settings_changed();
        }
        return 0;

    case WM_DPICHANGED: {
        int new_dpi = HIWORD(wp);
        if (m_core) m_core->update_dpi(static_cast<float>(new_dpi) / 96.0f);
        InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    }

    case ControlPanelCore::WM_NOWBAR_ANIMATE: {
        // Thread-pool timer fired — release the one-shot handle, invalidate,
        // and force an immediate paint so it isn't delayed by low-priority
        // WM_PAINT scheduling.
        if (m_core) m_core->on_animation_timer_fired();
        const RECT* dirty = m_core ? m_core->get_animation_dirty_rect() : nullptr;
        InvalidateRect(wnd, dirty, FALSE);
        if (m_core) m_core->clear_animation_dirty();
        UpdateWindow(wnd);
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

    return DefWindowProc(wnd, msg, wp, lp);
}

} // namespace nowbar
