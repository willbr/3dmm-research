#ifndef MARCUS_PLAYER_H
#define MARCUS_PLAYER_H

/* C89 boundary. No engine, C++, SDL or platform types escape this interface.
 * All calls, including destruction, run on the same thread. Only one player
 * may own the renderer. Strings in returned records are copied and terminated.
 * Image bytes are borrowed until the next render, open, or destruction. */
#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct MarcusPlayer MarcusPlayer;
    typedef struct MarcusMovieInput MarcusMovieInput;
    typedef struct MarcusMovieEntry
    {
        char name[1024];
        double bytes;
        int zipped;
    } MarcusMovieEntry;

    typedef struct MarcusScene
    {
        char name[192];
        unsigned long chunk;
        long first_frame, last_frame;
    } MarcusScene;

    typedef struct MarcusObject
    {
        char name[192];
        unsigned long chunk, template_chunk;
        long actor_id, source_id;
        long first_frame, last_frame, action, cel;
        int prop, text, visible, has_lifetime;
        float x, y, z;
    } MarcusObject;

    typedef struct MarcusInfo
    {
        int scene, scene_count, object_count;
        long first_frame, last_frame, frame;
        unsigned long camera, camera_count;
        int sound_count, sound_track_count;
    } MarcusInfo;

    enum MarcusSoundStyle
    {
        MARCUS_SOUND_EFFECT = 2,
        MARCUS_SOUND_SPEECH = 3,
        MARCUS_SOUND_MUSIC = 4
    };
    typedef struct MarcusSoundTrack
    {
        char name[192];
        int cue_count;
    } MarcusSoundTrack;
    typedef struct MarcusSound
    {
        char name[192], owner[192];
        long frame, actor_id, source_id, style, volume, motion_cel;
        unsigned long chunk, child;
        int track, actor_sound, loop, queue, silent, motion;
        int origin_scene, carried; /* Zero-based source scene; carried into this sheet. */
        double origin_frame; /* One-based frame in the source scene. */
        double timeline_frame, end_frame; /* Scheduled extent in this scene's frame coordinates. */
    } MarcusSound;
    enum MarcusWaveformKind
    {
        MARCUS_WAVEFORM_SAMPLED = 1,
        MARCUS_WAVEFORM_MIDI,
        MARCUS_WAVEFORM_SILENT,
        MARCUS_WAVEFORM_UNAVAILABLE
    };
    typedef struct MarcusWaveformColumn
    {
        float minimum, maximum; /* Both channels, relative to full scale. */
    } MarcusWaveformColumn;
    typedef struct MarcusWaveformInfo
    {
        double seconds; /* One clip iteration, independent of cue volume. */
        int kind;
        char message[192];
        unsigned long resolved_type, resolved_chunk;
        long resolved_source;
    } MarcusWaveformInfo;

    typedef struct MarcusCamera
    {
        float x, y, z, yaw, pitch, move_speed;
    } MarcusCamera;

    typedef struct MarcusProjection
    {
        int valid;
        float transform[12]; /* Camera-to-world right/up/backward/position rows. */
        float vertical_fov, aspect, near_clip, far_clip;
        float display_depth; /* Camera marker length, independent of clip planes. */
    } MarcusProjection;

    typedef struct MarcusImage
    {
        int width, height, pitch;
        const unsigned char *rgba;
        MarcusCamera camera;
        MarcusProjection projection, scene_camera;
    } MarcusImage;
    typedef struct MarcusPlaybackState
    {
        int playing, changed, scene_changed;
        int wait; /* 0 none, 1 click, 2 non-looping sound, 3 authored duration. */
        int scrolling, transition;
    } MarcusPlaybackState;
    typedef struct MarcusAudioStats
    {
        unsigned long wave_sounds, midi_sounds, rendered_frames, audible_frames;
    } MarcusAudioStats;

    MarcusPlayer *marcus_create(void);
    enum MarcusRenderer
    {
        MARCUS_RENDERER_BRENDER,
        MARCUS_RENDERER_BRENDER_COMPUTE,
        MARCUS_RENDERER_SDL_GPU,
        MARCUS_RENDERER_COUNT
    };
    const char *marcus_renderer_name(int renderer);
    const char *marcus_renderer_id(int renderer);
    int marcus_renderer_from_id(const char *id);
    int marcus_get_renderer(const MarcusPlayer *player);
    /* A failed switch retains the current renderer and loaded movie. */
    int marcus_set_renderer(MarcusPlayer *player, int renderer);
    void marcus_destroy(MarcusPlayer *player);
    const char *marcus_error(const MarcusPlayer *player);
    /* resources is 3dmovie.chk (a built utest.chk also works). content_directory
     * supplies the four original .3cn archives. Unknown expansion sources report
     * their missing source ID, rather than silently replacing original content. */
    int marcus_open(MarcusPlayer *player, const char *movie, const char *resources, const char *content_directory);
    /* Listing never extracts a movie. A single entry opens automatically via
     * marcus_open; multi-movie ZIPs require an explicit zero-based selection.
     * The loaded player retains the extracted file after input is destroyed. */
    MarcusMovieInput *marcus_input_create(void);
    void marcus_input_destroy(MarcusMovieInput *input);
    int marcus_input_open(MarcusMovieInput *input, const char *path);
    const char *marcus_input_error(const MarcusMovieInput *input);
    int marcus_input_count(const MarcusMovieInput *input);
    int marcus_input_entry(const MarcusMovieInput *input, int index, MarcusMovieEntry *entry);
    int marcus_open_input(MarcusPlayer *player, MarcusMovieInput *input, int index, const char *resources,
                          const char *content_directory);
    int marcus_scene(const MarcusPlayer *player, int index, MarcusScene *scene);
    int marcus_select_scene(MarcusPlayer *player, int index);
    /* Playback scene changes keep Orbit framing and Fly's clipping reference. */
    int marcus_select_scene_keep_view(MarcusPlayer *player, int index);
    int marcus_info(const MarcusPlayer *player, MarcusInfo *info);
    int marcus_object(MarcusPlayer *player, int index, MarcusObject *object);
    /* Authored cue markers, not waveform durations. Motion cues mark bindings;
     * queued cues share their authored start. Reading never seeks or plays. */
    int marcus_sound_track(const MarcusPlayer *player, int index, MarcusSoundTrack *track);
    int marcus_sound(const MarcusPlayer *player, int index, MarcusSound *sound);
    /* Lazy, cached, silent decoding. Sampled peaks cover successive clip-relative
     * windows; loops repeat and other out-of-clip columns are zero. MIDI,
     * silence and unavailable clips have zero columns and a kind/message.
     * count is 1..4096; first_seconds
     * must be nonnegative and seconds_per_column positive, both finite. */
    int marcus_sound_waveform(MarcusPlayer *player, int index, double first_seconds, double seconds_per_column,
                              int count, MarcusWaveformColumn *columns, MarcusWaveformInfo *info);
    /* Timeline-aligned peaks, including scrolling and authored frame holds. */
    int marcus_sound_waveform_frames(MarcusPlayer *player, int index, double first_frame, double frames_per_column,
                                     int count, MarcusWaveformColumn *columns, MarcusWaveformInfo *info);
    int marcus_seek(MarcusPlayer *player, long frame);
    /* Call once per host update, with elapsed milliseconds. Playback preserves
     * authored waits/scrolling; a click/Space continues a click wait. Manual
     * seeks are silent and reset presentation timing. No SDL types cross here. */
    int marcus_update(MarcusPlayer *player, int playing, int loop, double elapsed_ms, int continue_click,
                      MarcusPlaybackState *state);
    void marcus_stop(MarcusPlayer *player);
    int marcus_audio_stats(MarcusPlayer *player, MarcusAudioStats *stats);
    void marcus_set_muted(MarcusPlayer *player, int muted);
    /* Refit Orbit on its next render. */
    void marcus_reset_framing(MarcusPlayer *player);
    /* Pan the cached orbit center by fractions of viewport width/right and
     * height/up. Angles and zoom match the current orbit render. */
    int marcus_pan_orbit(MarcusPlayer *player, float right, float up, float yaw, float pitch, float zoom);
    int marcus_render(MarcusPlayer *player, int width, int height, int authored_camera, float yaw, float pitch,
                      float zoom, MarcusImage *image);
    /* Explicit camera position in original world units, independent of frame
     * evaluation. Position is limited to +/-32700 by the fixed-point renderer. */
    int marcus_render_fly(MarcusPlayer *player, int width, int height, const MarcusCamera *camera, MarcusImage *image);

#ifdef __cplusplus
}
#endif
#endif
