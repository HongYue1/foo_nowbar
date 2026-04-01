#include "pch.h"
#include "preferences.h"
#include <shellapi.h>
#include <shlobj.h>

// GUIDs for the Now Bar mainmenu group and commands
// {D6A5E8F0-1234-5678-ABCD-000000000001} - Now Bar menu group
static const GUID guid_nowbar_menu_group = 
    { 0xD6A5E8F0, 0x1234, 0x5678, { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

// Command GUIDs for Custom Buttons 1-12
// Each GUID was independently generated to guarantee uniqueness.
static const GUID guid_cbutton_commands[] = {
    // Button 1  {3A7F2C1E-84B6-4D59-A031-F5E82C6B1047}
    { 0x3a7f2c1e, 0x84b6, 0x4d59, { 0xa0, 0x31, 0xf5, 0xe8, 0x2c, 0x6b, 0x10, 0x47 } },
    // Button 2  {61D4A83F-29C7-4E1B-B752-0A3F96D28E54}
    { 0x61d4a83f, 0x29c7, 0x4e1b, { 0xb7, 0x52, 0x0a, 0x3f, 0x96, 0xd2, 0x8e, 0x54 } },
    // Button 3  {B2E53C70-5F1A-4823-C946-17D08B4E3F62}
    { 0xb2e53c70, 0x5f1a, 0x4823, { 0xc9, 0x46, 0x17, 0xd0, 0x8b, 0x4e, 0x3f, 0x62 } },
    // Button 4  {F04812DA-6E3B-47C5-A817-2C5F0D7B9E31}
    { 0xf04812da, 0x6e3b, 0x47c5, { 0xa8, 0x17, 0x2c, 0x5f, 0x0d, 0x7b, 0x9e, 0x31 } },
    // Button 5  {48C937EB-1D52-4F76-B023-E8A64C915D07}
    { 0x48c937eb, 0x1d52, 0x4f76, { 0xb0, 0x23, 0xe8, 0xa6, 0x4c, 0x91, 0x5d, 0x07 } },
    // Button 6  {9E271F3C-A047-4B82-D534-63B17E0C8A29}
    { 0x9e271f3c, 0xa047, 0x4b82, { 0xd5, 0x34, 0x63, 0xb1, 0x7e, 0x0c, 0x8a, 0x29 } },
    // Button 7  {C5B06D4A-3E18-4C97-8F62-D04A2B7E5C83}
    { 0xc5b06d4a, 0x3e18, 0x4c97, { 0x8f, 0x62, 0xd0, 0x4a, 0x2b, 0x7e, 0x5c, 0x83 } },
    // Button 8  {72A349BC-8D25-4E03-A175-50F6C3D18B46}
    { 0x72a349bc, 0x8d25, 0x4e03, { 0xa1, 0x75, 0x50, 0xf6, 0xc3, 0xd1, 0x8b, 0x46 } },
    // Button 9  {E3F57082-4C6A-4B1D-9E28-A73D0F2C6154}
    { 0xe3f57082, 0x4c6a, 0x4b1d, { 0x9e, 0x28, 0xa7, 0x3d, 0x0f, 0x2c, 0x61, 0x54 } },
    // Button 10 {15D86C9E-7F43-4A05-B340-82E1C57D0A67}
    { 0x15d86c9e, 0x7f43, 0x4a05, { 0xb3, 0x40, 0x82, 0xe1, 0xc5, 0x7d, 0x0a, 0x67 } },
    // Button 11 {8B4E01F7-5C2D-4693-A056-2F9E7C3B1D48}
    { 0x8b4e01f7, 0x5c2d, 0x4693, { 0xa0, 0x56, 0x2f, 0x9e, 0x7c, 0x3b, 0x1d, 0x48 } },
    // Button 12 {D60923AE-1B74-4F58-C361-E7A02D8F5B90}
    { 0xd60923ae, 0x1b74, 0x4f58, { 0xc3, 0x61, 0xe7, 0xa0, 0x2d, 0x8f, 0x5b, 0x90 } },
};

// Execute custom button action (shared implementation)
// Supports buttons 0-11 (1-12 in UI)
static void execute_cbutton_action(int button_index) {
    if (button_index < 0 || button_index >= 12) return;
    
    int action;
    pfc::string8 path;
    
    if (button_index < 6) {
        // Buttons 1-6: Use preferences (visible buttons)
        if (!get_nowbar_cbutton_enabled(button_index)) return;
        action = get_nowbar_cbutton_action(button_index);
        path = get_nowbar_cbutton_path(button_index);
    } else {
        // Buttons 7-12: Use config file (hidden buttons)
        action = get_config_button_action(button_index);
        path = get_config_button_path(button_index);
    }
    
    if (action == 1 && !path.is_empty()) {
        // Open URL with title formatting support - use selected track from playlist
        pfc::string8 evaluated_url;
        
        // Check if path contains title formatting (has % character)
        if (path.find_first('%') != pfc::infinite_size) {
            // Has title formatting - evaluate it using selected track
            metadb_handle_ptr track;
            bool has_track = false;
            
            // Get the focused/selected track from the active playlist
            auto pm = playlist_manager::get();
            t_size active_playlist = pm->get_active_playlist();
            if (active_playlist != pfc_infinite) {
                t_size focus = pm->playlist_get_focus_item(active_playlist);
                if (focus != pfc_infinite) {
                    has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
                }
            }
            
            service_ptr_t<titleformat_object> script;
            titleformat_compiler::get()->compile_safe(script, path);
            
            if (has_track && track.is_valid() && script.is_valid()) {
                track->format_title(nullptr, evaluated_url, script, nullptr);
            } else if (script.is_valid()) {
                // Fallback to playing track if no selection
                auto pc = playback_control::get();
                pc->playback_format_title(nullptr, evaluated_url, script, nullptr, playback_control::display_level_all);
            }
        } else {
            // No title formatting - use path directly as URL
            evaluated_url = path;
        }
        
        if (!evaluated_url.is_empty()) {
            pfc::stringcvt::string_wide_from_utf8 wideUrl(evaluated_url);
            ShellExecuteW(nullptr, L"open", wideUrl, nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (action == 2 && !path.is_empty()) {
        // Run Executable with file path argument - use selected track from playlist
        pfc::string8 exe_path;
        
        // Get the focused/selected track from the active playlist first
        auto pm = playlist_manager::get();
        metadb_handle_ptr track;
        bool has_track = false;
        
        t_size active_playlist = pm->get_active_playlist();
        if (active_playlist != pfc_infinite) {
            t_size focus = pm->playlist_get_focus_item(active_playlist);
            if (focus != pfc_infinite) {
                has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
            }
        }
        
        // Check if path contains title formatting (has % character)
        if (path.find_first('%') != pfc::infinite_size) {
            // Has title formatting - evaluate it using selected track
            service_ptr_t<titleformat_object> script;
            titleformat_compiler::get()->compile_safe(script, path);
            
            if (has_track && track.is_valid() && script.is_valid()) {
                track->format_title(nullptr, exe_path, script, nullptr);
            } else if (script.is_valid()) {
                // Fallback to playing track if no selection
                auto pc = playback_control::get();
                pc->playback_format_title(nullptr, exe_path, script, nullptr, playback_control::display_level_all);
            }
        } else {
            // No title formatting - use path directly as executable
            exe_path = path;
        }
        
        if (!exe_path.is_empty()) {
            if (has_track && track.is_valid()) {
                const char* file_path = track->get_path();
                if (file_path) {
                    pfc::string8 physical_path;
                    filesystem::g_get_display_path(file_path, physical_path);
                    pfc::string8 quoted_path;
                    quoted_path << "\"" << physical_path << "\"";
                    
                    pfc::stringcvt::string_wide_from_utf8 wideExe(exe_path);
                    pfc::stringcvt::string_wide_from_utf8 wideArgs(quoted_path);
                    ShellExecuteW(nullptr, L"open", wideExe, wideArgs, nullptr, SW_SHOWNORMAL);
                } else {
                    pfc::stringcvt::string_wide_from_utf8 wideExe(exe_path);
                    ShellExecuteW(nullptr, L"open", wideExe, nullptr, nullptr, SW_SHOWNORMAL);
                }
            } else {
                // No track selected - just run the executable without arguments
                pfc::stringcvt::string_wide_from_utf8 wideExe(exe_path);
                ShellExecuteW(nullptr, L"open", wideExe, nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    } else if (action == 3 && !path.is_empty()) {
        // Foobar2k Action
        execute_fb2k_action_by_path(path.c_str());
    } else if (action == 4) {
        // Open Folder - open the directory containing the currently playing track
        metadb_handle_ptr track;
        bool has_track = false;

        // Try the focused/selected track from the active playlist first
        auto pm = playlist_manager::get();
        t_size active_playlist = pm->get_active_playlist();
        if (active_playlist != pfc_infinite) {
            t_size focus = pm->playlist_get_focus_item(active_playlist);
            if (focus != pfc_infinite) {
                has_track = pm->playlist_get_item_handle(track, active_playlist, focus);
            }
        }

        // Fallback to the currently playing track
        if (!has_track || !track.is_valid()) {
            auto pc = playback_control::get();
            has_track = pc->get_now_playing(track);
        }

        if (has_track && track.is_valid()) {
            const char* file_path = track->get_path();
            if (file_path) {
                pfc::string8 display_path;
                filesystem::g_get_display_path(file_path, display_path);

                // Open folder and select the file (opens in new tab on Windows 11)
                pfc::stringcvt::string_wide_from_utf8 widePath(display_path);
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(SHParseDisplayName(widePath, nullptr, &pidl, 0, nullptr))) {
                    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                    CoTaskMemFree(pidl);
                }
            }
        }
    }
}

// Register the Now Bar menu group under File (hidden by default, shown with Shift)
static mainmenu_group_popup_factory g_nowbar_menu_group(
    guid_nowbar_menu_group,
    mainmenu_groups::file,
    mainmenu_commands::sort_priority_base + 1000,  // Low priority to appear near bottom
    "Now Bar"
);

// Main menu commands implementation
class nowbar_mainmenu_commands : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {
        return 12;  // Custom buttons 1-12
    }
    
    GUID get_command(t_uint32 p_index) override {
        if (p_index < 12) {
            return guid_cbutton_commands[p_index];
        }
        return pfc::guid_null;
    }
    
    void get_name(t_uint32 p_index, pfc::string_base& p_out) override {
        if (p_index < 12) {
            p_out.reset();
            p_out << "Custom Button " << (p_index + 1);
        }
    }
    
    bool get_description(t_uint32 p_index, pfc::string_base& p_out) override {
        if (p_index < 12) {
            p_out.reset();
            int action;
            pfc::string8 path;
            bool is_enabled = true;
            
            if (p_index < 6) {
                // Visible buttons - check enabled state
                is_enabled = get_nowbar_cbutton_enabled(p_index);
                action = get_nowbar_cbutton_action(p_index);
                path = get_nowbar_cbutton_path(p_index);
            } else {
                // Hidden buttons - always enabled, read from config
                action = get_config_button_action(p_index);
                path = get_config_button_path(p_index);
            }
            
            if (is_enabled) {
                switch (action) {
                    case 1:
                        p_out << "Open URL: " << (path.is_empty() ? "(not configured)" : path.c_str());
                        break;
                    case 2:
                        p_out << "Run program: " << (path.is_empty() ? "(not configured)" : path.c_str());
                        break;
                    case 3:
                        p_out << "foobar2000 action: " << (path.is_empty() ? "(not configured)" : path.c_str());
                        break;
                    case 4:
                        p_out << "Open folder of playing track";
                        break;
                    default:
                        p_out << "No action configured";
                        break;
                }
            } else {
                p_out << "Button disabled";
            }
            return true;
        }
        return false;
    }
    
    GUID get_parent() override {
        return guid_nowbar_menu_group;
    }
    
    t_uint32 get_sort_priority() override {
        return mainmenu_commands::sort_priority_base;
    }
    
    bool get_display(t_uint32 p_index, pfc::string_base& p_text, t_uint32& p_flags) override {
        if (p_index >= 12) return false;
        
        get_name(p_index, p_text);
        
        // Hidden by default, shown when Shift is held (flag_defaulthidden)
        // All 12 buttons are visible once the Now Bar submenu is accessed
        p_flags = flag_defaulthidden;
        
        if (p_index < 6) {
            // Visible buttons - check enabled state
            if (!get_nowbar_cbutton_enabled(p_index)) {
                p_flags |= flag_disabled;
            } else {
                int action = get_nowbar_cbutton_action(p_index);
                if (action == 0) {
                    p_flags |= flag_disabled;  // No action
                }
            }
        } else {
            // Hidden buttons - check action from config
            int action = get_config_button_action(p_index);
            if (action == 0) {
                p_flags |= flag_disabled;  // No action
            }
        }
        
        return true;
    }
    
    void execute(t_uint32 p_index, service_ptr ctx) override {
        (void)ctx;
        if (p_index < 12) {
            execute_cbutton_action(p_index);
        }
    }
};

// Register the mainmenu commands
static mainmenu_commands_factory_t<nowbar_mainmenu_commands> g_nowbar_mainmenu_commands;
