#include "pch.h"
#include "playback_state.h"
#include "../preferences.h"

namespace nowbar {

// Raw-pointer singleton — both constructor and destructor are intentionally
// private, which makes std::unique_ptr<> (whose default_delete needs public
// dtor access) unsuitable here without a custom deleter or friend declaration.
// Explicit new/delete in get()/shutdown() keeps access within this TU.
static PlaybackStateManager* g_instance = nullptr;
static bool g_shutdown_called = false;

PlaybackStateManager& PlaybackStateManager::get() {
    // Main-thread-only: no mutex guards g_instance. All callers must be on the main thread.
    core_api::ensure_main_thread();
    if (!g_instance && !g_shutdown_called) {
        g_instance = new PlaybackStateManager();
    }
    // Guard against calls after shutdown() — dereferencing null is UB.
    PFC_ASSERT(g_instance != nullptr);
    return *g_instance;
}

void PlaybackStateManager::shutdown() {
    g_shutdown_called = true;
    delete g_instance;
    g_instance = nullptr;
}

bool PlaybackStateManager::is_available() {
    return g_instance != nullptr && !g_shutdown_called;
}

PlaybackStateManager::~PlaybackStateManager() {
    // Clear metadb_handle_ptr while services are still available
    m_state.current_track.release();
    // Base class destructor will unregister from play_callback_manager
}

PlaybackStateManager::PlaybackStateManager() 
    : play_callback_impl_base(flag_on_playback_all | flag_on_volume_change)
{
    // Initialize current state from playback control
    auto pc = playback_control::get();
    m_state.is_playing = pc->is_playing();
    m_state.is_paused = pc->is_paused();
    m_state.volume_db = pc->get_volume();
    m_state.playback_order = playlist_manager::get()->playback_order_get_active();
    
    if (m_state.is_playing) {
        m_state.playback_time = pc->playback_get_position();
        m_state.track_length = pc->playback_get_length();
        m_state.can_seek = pc->playback_can_seek();
        
        metadb_handle_ptr track;
        if (pc->get_now_playing(track)) {
            m_state.current_track = track;  // Store the track handle
            update_track_info(track);
        }
    }
}

void PlaybackStateManager::register_callback(IPlaybackStateCallback* cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.push_back(cb);
    m_callbacks_snapshot.store(std::make_shared<std::vector<IPlaybackStateCallback*>>(m_callbacks));
}

void PlaybackStateManager::unregister_callback(IPlaybackStateCallback* cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.erase(
        std::remove(m_callbacks.begin(), m_callbacks.end(), cb),
        m_callbacks.end()
    );
    m_callbacks_snapshot.store(std::make_shared<std::vector<IPlaybackStateCallback*>>(m_callbacks));
}

void PlaybackStateManager::on_playback_starting(play_control::t_track_command p_command, bool p_paused) noexcept {
    try {
        m_state.is_playing = true;
        m_state.is_paused = p_paused;
        notify_state_changed();
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_nowbar: on_playback_starting exception: " << e.what();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_new_track(metadb_handle_ptr p_track) noexcept {
    try {
    auto pc = playback_control::get();
    m_state.is_playing = true;
    m_state.is_paused = pc->is_paused();
    m_state.playback_time = 0.0;
    m_state.track_length = pc->playback_get_length();
    m_state.can_seek = pc->playback_can_seek();
    m_state.current_track = p_track;  // Store the track handle
    m_state.playback_order = playlist_manager::get()->playback_order_get_active();  // Refresh playback order

    // Reset preview skip flag for new track
    m_preview_skip_triggered = false;

    // Check if we should skip this track due to low rating
    if (check_and_skip_low_rating(p_track)) {
        return;  // Track is being skipped, don't update UI
    }

    // Reset consecutive skip counter on successful playback
    m_consecutive_rating_skips = 0;

    update_track_info(p_track);
    notify_track_changed();
    // notify_state_changed() intentionally omitted here:
    // on_playback_starting() already fired a state change before this callback,
    // and on_track_changed() handles all track-specific updates (theme, formats,
    // artwork, invalidation).  The redundant state notification was causing
    // evaluate_title_formats() and invalidate() to run a third time per track change.
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_nowbar: on_playback_new_track exception: " << e.what();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_stop(play_control::t_stop_reason p_reason) noexcept {
    try {
    // When starting another track (manual next/prev), don't notify UI of "stopped" state
    // The on_playback_new_track() callback will fire immediately after with correct state.
    // Notifying "stopped" here would cause the UI to clear artwork/caches unnecessarily,
    // causing a visual flash during track transitions.
    if (p_reason == play_control::stop_reason_starting_another) {
        return;
    }

    // Handle infinite playback before clearing state.
    // Deferred via fb2k::inMainThread so this callback returns first;
    // the library scan and filtering run off-thread via fb2k::splitTask.
    if (p_reason == play_control::stop_reason_eof && get_nowbar_infinite_playback_enabled()) {
        fb2k::inMainThread([this] {
            if (is_available()) handle_infinite_playback();
        });
    }

    m_state.is_playing = false;
    m_state.is_paused = false;
    m_state.playback_time = 0.0;
    m_state.track_length = 0.0;
    m_state.can_seek = false;
    m_state.current_track.release();  // Clear the track handle

    // Clear track info to reset to initial state
    m_state.track_title.reset();
    m_state.track_artist.reset();
    m_state.track_album.reset();

    notify_state_changed();
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_nowbar: on_playback_stop exception: " << e.what();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_seek(double p_time) noexcept {
    try {
        m_state.playback_time = p_time;
        notify_time_changed(p_time);
    } catch (...) {}
}

void PlaybackStateManager::on_playback_pause(bool p_state) noexcept {
    try {
        m_state.is_paused = p_state;
        notify_state_changed();
    } catch (...) {}
}

void PlaybackStateManager::on_playback_time(double p_time) noexcept {
    // on_playback_time fires on the audio decoder thread at ~20 Hz.
    // Always store the latest position first.  The coalescing guard below
    // ensures at most one lambda is queued at any moment; the lambda reads
    // back m_latest_playback_time so it always dispatches the newest value
    // rather than the (potentially stale) value captured at enqueue time.
    m_latest_playback_time.store(p_time, std::memory_order_relaxed);
    if (m_time_update_pending.exchange(true)) return;
    fb2k::inMainThread([]() {
        if (!is_available()) return;
        auto& mgr = get();
        mgr.m_time_update_pending.store(false, std::memory_order_relaxed);
        double t = mgr.m_latest_playback_time.load(std::memory_order_relaxed);
        try {
            mgr.m_state.playback_time = t;
            mgr.notify_time_changed(t);
            mgr.check_preview_skip(t);
        } catch (...) {}
    });
}

void PlaybackStateManager::on_volume_change(float p_new_val) noexcept {
    try {
        m_state.volume_db = p_new_val;
        notify_volume_changed(p_new_val);
    } catch (...) {}
}

void PlaybackStateManager::on_playback_dynamic_info_track(const file_info& p_info) noexcept {
    try {
    // Extract metadata from dynamic info (for streaming sources like internet radio)
    const char* title = nullptr;
    const char* artist = nullptr;
    
    // Standard metadata
    if (p_info.meta_exists("TITLE")) {
        title = p_info.meta_get("TITLE", 0);
    }
    if (p_info.meta_exists("ARTIST")) {
        artist = p_info.meta_get("ARTIST", 0);
    }
    
    // ICY metadata (common in internet radio)
    if (!title && p_info.meta_exists("STREAMTITLE")) {
        title = p_info.meta_get("STREAMTITLE", 0);
    }
    if (!title && p_info.meta_exists("ICY_TITLE")) {
        title = p_info.meta_get("ICY_TITLE", 0);
    }
    
    // Alternative artist fields
    if (!artist && p_info.meta_exists("ALBUMARTIST")) {
        artist = p_info.meta_get("ALBUMARTIST", 0);
    }
    if (!artist && p_info.meta_exists("PERFORMER")) {
        artist = p_info.meta_get("PERFORMER", 0);
    }
    
    // Update state if we found meaningful metadata
    bool changed = false;
    if (title && strlen(title) > 0) {
        m_state.track_title = title;
        changed = true;
    }
    if (artist && strlen(artist) > 0) {
        m_state.track_artist = artist;
        changed = true;
    }
    
    if (changed) {
        notify_track_changed();
        notify_state_changed();
    }
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_nowbar: on_playback_dynamic_info_track exception: " << e.what();
    } catch (...) {}
}

void PlaybackStateManager::update_track_info(metadb_handle_ptr p_track) {
    if (!p_track.is_valid()) return;
    
    // Get track info using new API
    metadb_info_container::ptr info_container = p_track->get_info_ref();
    if (info_container.is_valid()) {
        const file_info& info = info_container->info();
        
        // Title
        const char* title = info.meta_get("TITLE", 0);
        m_state.track_title = title ? title : pfc::string_filename_ext(p_track->get_path()).get_ptr();
        
        // Artist
        const char* artist = info.meta_get("ARTIST", 0);
        m_state.track_artist = artist ? artist : "";
        
        // Album
        const char* album = info.meta_get("ALBUM", 0);
        m_state.track_album = album ? album : "";
    }
}

void PlaybackStateManager::notify_state_changed() {
    PlaybackState state_snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        state_snapshot = m_state;
    }
    auto snap = m_callbacks_snapshot.load();
    if (!snap) return;
    for (auto* cb : *snap) {
        cb->on_playback_state_changed(state_snapshot);
    }
}

void PlaybackStateManager::notify_time_changed(double time) {
    auto snap = m_callbacks_snapshot.load();
    if (!snap) return;
    for (auto* cb : *snap) {
        cb->on_playback_time_changed(time);
    }
}

void PlaybackStateManager::notify_volume_changed(float volume) {
    auto snap = m_callbacks_snapshot.load();
    if (!snap) return;
    for (auto* cb : *snap) {
        cb->on_volume_changed(volume);
    }
}

void PlaybackStateManager::notify_track_changed() {
    auto snap = m_callbacks_snapshot.load();
    if (!snap) return;
    for (auto* cb : *snap) {
        cb->on_track_changed();
    }
}

// Callback to start playback on main thread
class InfinitePlaybackCallback : public main_thread_callback {
public:
    InfinitePlaybackCallback(t_size start_index) : m_start_index(start_index) {}

    void callback_run() override {
        auto pm = playlist_manager::get();
        t_size playlist = pm->get_active_playlist();
        if (playlist != pfc_infinite) {
            pm->playlist_set_focus_item(playlist, m_start_index);
            playback_control::get()->start();
        }
    }

private:
    t_size m_start_index;
};

void PlaybackStateManager::handle_infinite_playback() {
    auto pm = playlist_manager::get();
    auto pc = playback_control::get();

    // Debounce: if infinite playback was triggered less than 10 seconds ago, skip.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_infinite_playback_time).count();
    if (m_last_infinite_playback_time.time_since_epoch().count() != 0 && elapsed < 10) {
        return;
    }
    m_last_infinite_playback_time = now;

    if (pc->get_stop_after_current()) return;

    t_size active_playlist = pm->get_active_playlist();
    if (active_playlist == pfc_infinite) return;

    try {
        auto apm = autoplaylist_manager::get();
        if (apm->is_client_present(active_playlist)) return;
    } catch (...) {}

    // ── Main-thread-only reads ────────────────────────────────────────────────
    // library_manager::get_all_items(), playlist_get_item_handle() must stay here.
    t_size playlist_count   = pm->playlist_get_item_count(active_playlist);
    t_size insert_position  = playlist_count;

    std::set<pfc::string8> genres;
    t_size scan_count = std::min(playlist_count, (t_size)10);
    for (t_size i = 0; i < scan_count; i++) {
        t_size idx = playlist_count - 1 - i;
        metadb_handle_ptr track;
        if (pm->playlist_get_item_handle(track, active_playlist, idx) && track.is_valid()) {
            metadb_info_container::ptr info = track->get_info_ref();
            if (info.is_valid()) {
                const file_info& fi = info->info();
                for (t_size g = 0; g < fi.meta_get_count_by_name("genre"); g++) {
                    const char* genre = fi.meta_get("genre", g);
                    if (genre && strlen(genre) > 0)
                        genres.insert(pfc::string8(genre));
                }
            }
        }
    }

    std::set<pfc::string8> playlist_paths;
    for (t_size i = 0; i < playlist_count; i++) {
        metadb_handle_ptr track;
        if (pm->playlist_get_item_handle(track, active_playlist, i) && track.is_valid())
            playlist_paths.insert(pfc::string8(track->get_path()));
    }

    pfc::list_t<metadb_handle_ptr> library_items;
    library_manager::get()->get_all_items(library_items);
    if (library_items.get_count() == 0) return;

    // ── Worker task: O(n) filtering + shuffling off the main thread ───────────
    fb2k::splitTask([
        library_items  = std::move(library_items),
        playlist_paths = std::move(playlist_paths),
        genres         = std::move(genres),
        active_playlist,
        insert_position,
        rng = m_rng   // snapshot RNG state; m_rng lives on main thread
    ]() mutable {
        pfc::list_t<metadb_handle_ptr> matching_tracks;
        pfc::list_t<metadb_handle_ptr> fallback_tracks;

        for (t_size i = 0; i < library_items.get_count(); i++) {
            metadb_handle_ptr track = library_items[i];
            if (!track.is_valid()) continue;
            if (playlist_paths.count(pfc::string8(track->get_path())) > 0) continue;

            metadb_info_container::ptr info = track->get_info_ref();
            if (!info.is_valid()) continue;
            const file_info& fi = info->info();

            bool matches_genre = false;
            if (!genres.empty()) {
                for (t_size g = 0; g < fi.meta_get_count_by_name("genre"); g++) {
                    const char* genre = fi.meta_get("genre", g);
                    if (genre && genres.count(pfc::string8(genre)) > 0) {
                        matches_genre = true;
                        break;
                    }
                }
            }
            if (matches_genre) matching_tracks.add_item(track);
            else               fallback_tracks.add_item(track);
        }

        // Shuffle helper (Fisher-Yates)
        auto shuffle = [&rng](pfc::list_t<metadb_handle_ptr>& list) mutable {
            for (t_size i = list.get_count() - 1; i > 0; i--) {
                std::uniform_int_distribution<t_size> dist(0, i);
                t_size j = dist(rng);
                if (i != j) {
                    metadb_handle_ptr tmp = list[i];
                    list.replace_item(i, list[j]);
                    list.replace_item(j, tmp);
                }
            }
        };

        pfc::list_t<metadb_handle_ptr> tracks_to_add;
        const int tracks_to_select = 15;

        if (matching_tracks.get_count() > 0) {
            shuffle(matching_tracks);
            for (t_size i = 0; i < matching_tracks.get_count() && tracks_to_add.get_count() < tracks_to_select; i++)
                tracks_to_add.add_item(matching_tracks[i]);
        }
        if (tracks_to_add.get_count() < tracks_to_select && fallback_tracks.get_count() > 0) {
            shuffle(fallback_tracks);
            for (t_size i = 0; i < fallback_tracks.get_count() && tracks_to_add.get_count() < tracks_to_select; i++)
                tracks_to_add.add_item(fallback_tracks[i]);
        }

        if (tracks_to_add.get_count() == 0) return;

        // ── Marshal back to main thread for playlist mutation ─────────────────
        fb2k::inMainThread([tracks_to_add, active_playlist, insert_position]() mutable {
            auto pm2 = playlist_manager::get();
            if (active_playlist >= pm2->get_playlist_count()) return;
            bit_array_false selection;
            pm2->playlist_insert_items(active_playlist, insert_position, tracks_to_add, selection);
            auto mtcm = main_thread_callback_manager::get();
            mtcm->add_callback(fb2k::service_new<InfinitePlaybackCallback>(insert_position));
        });
    });
}

// Generic reusable callback that runs an arbitrary lambda on the main thread.
// Replaces the several near-identical main_thread_callback subclasses that all
// called playback_control::get()->next().
class PlaybackCommandCallback : public main_thread_callback {
public:
    explicit PlaybackCommandCallback(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void callback_run() override { m_fn(); }
private:
    std::function<void()> m_fn;
};

void PlaybackStateManager::check_preview_skip(double current_time) {
    // Skip if already triggered for this track
    if (m_preview_skip_triggered) return;

    // Get preview mode setting: 0=Off, 1=35%, 2=50%, 3=60sec
    int preview_mode = get_nowbar_preview_mode();
    if (preview_mode == 0) return;  // Preview disabled

    // Need valid track length for percentage modes
    double track_length = m_state.track_length;
    if (track_length <= 0.0 && preview_mode != 3) return;

    // Calculate preview threshold
    double preview_threshold = 0.0;
    switch (preview_mode) {
        case 1:  // 35% of track
            preview_threshold = track_length * 0.35;
            break;
        case 2:  // 50% of track
            preview_threshold = track_length * 0.50;
            break;
        case 3:  // 60 seconds
            preview_threshold = 60.0;
            break;
        default:
            return;
    }

    // For fixed duration, don't skip if track is shorter than the threshold
    // (let it play to the end naturally)
    if (preview_mode == 3 && track_length > 0.0 && track_length <= preview_threshold) {
        return;
    }

    // Check if we've reached the preview threshold
    if (current_time >= preview_threshold) {
        m_preview_skip_triggered = true;

        // Skip to next track using main thread callback
        auto mtcm = main_thread_callback_manager::get();
        mtcm->add_callback(fb2k::service_new<PlaybackCommandCallback>(
            [] { playback_control::get()->next(); }));
    }
}

bool PlaybackStateManager::check_and_skip_low_rating(metadb_handle_ptr p_track) {
    // Check if skip low rating is enabled
    if (!get_nowbar_skip_low_rating_enabled()) {
        return false;
    }

    // Check if we've hit the consecutive skip limit (prevent infinite loops)
    const int MAX_CONSECUTIVE_SKIPS = 10;
    if (m_consecutive_rating_skips >= MAX_CONSECUTIVE_SKIPS) {
        // Reset counter and allow this track to play
        m_consecutive_rating_skips = 0;
        return false;
    }

    // Validate track handle
    if (!p_track.is_valid()) {
        return false;
    }

    // Get the rating threshold
    int threshold = get_nowbar_skip_low_rating_threshold();

    // Evaluate %rating% using title formatting
    // foo_playcount exposes rating through title formatting
    try {
        // Compile once and cache the titleformat object
        if (!m_rating_format.is_valid()) {
            titleformat_compiler::get()->compile_safe(m_rating_format, "%rating%");
        }
        if (m_rating_format.is_valid()) {
            pfc::string8 rating_str;
            p_track->format_title(nullptr, rating_str, m_rating_format, nullptr);

            // rating_str will be "1", "2", "3", "4", "5", or empty (no rating)
            // Only skip if there's a valid rating that's at or below threshold
            if (!rating_str.is_empty()) {
                int rating = atoi(rating_str.c_str());
                if (rating >= 1 && rating <= threshold) {
                    // Increment consecutive skip counter
                    m_consecutive_rating_skips++;

                    // Skip to next track using main thread callback
                    auto mtcm = main_thread_callback_manager::get();
                    mtcm->add_callback(fb2k::service_new<PlaybackCommandCallback>(
                        [] { playback_control::get()->next(); }));
                    return true;
                }
            }
        }
    } catch (...) {
        // If title formatting fails, don't skip
    }

    return false;
}

} // namespace nowbar
