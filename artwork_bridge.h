#pragma once

#include <windows.h>
#include <functional>

// Callback type for receiving artwork results from foo_artwork
// Parameters: success (true if artwork found), bitmap (valid HBITMAP if success)
typedef void (*pfn_artwork_result_callback)(bool success, HBITMAP bitmap);

// Function pointer types matching foo_artwork's exported C API
typedef void (*pfn_foo_artwork_search)(const char* artist, const char* title);
typedef HBITMAP (*pfn_foo_artwork_get_bitmap)();
typedef bool (*pfn_foo_artwork_is_loading)();
typedef void (*pfn_foo_artwork_set_callback)(pfn_artwork_result_callback callback);
typedef void (*pfn_foo_artwork_remove_callback)(pfn_artwork_result_callback callback);

// Global function pointers - null if foo_artwork not available
extern pfn_foo_artwork_search g_artwork_search;
extern pfn_foo_artwork_get_bitmap g_artwork_get_bitmap;
extern pfn_foo_artwork_is_loading g_artwork_is_loading;
extern pfn_foo_artwork_set_callback g_artwork_set_callback;
extern pfn_foo_artwork_remove_callback g_artwork_remove_callback;

// Initialize the bridge - call once at component startup
// Returns true if foo_artwork was found and linked
bool init_artwork_bridge();

// Shutdown the bridge - call on component quit
void shutdown_artwork_bridge();

// Check if foo_artwork integration is available
inline bool is_artwork_bridge_available() {
    return g_artwork_search != nullptr;
}

// Request artwork from foo_artwork for given artist/title
// No-op if bridge not available or online artwork disabled
void request_online_artwork(const char* artist, const char* title);

// Check if artwork was received from foo_artwork
bool has_pending_online_artwork();

// Get and clear the pending artwork bitmap.
// Ownership is transferred to the caller, which is responsible for calling
// DeleteObject() on the returned handle when it is no longer needed.
// Returns nullptr if no artwork is pending.
HBITMAP get_pending_online_artwork();
