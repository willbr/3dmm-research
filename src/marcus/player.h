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
    } MarcusInfo;

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
    void marcus_destroy(MarcusPlayer *player);
    const char *marcus_error(const MarcusPlayer *player);
    /* resources is 3dmovie.chk (a built utest.chk also works). content_directory
     * supplies the four original .3cn archives. Unknown expansion sources report
     * their missing source ID, rather than silently replacing original content. */
    int marcus_open(MarcusPlayer *player, const char *movie, const char *resources, const char *content_directory);
    int marcus_scene(const MarcusPlayer *player, int index, MarcusScene *scene);
    int marcus_select_scene(MarcusPlayer *player, int index);
    /* Playback scene changes keep Orbit framing and Fly's clipping reference. */
    int marcus_select_scene_keep_view(MarcusPlayer *player, int index);
    int marcus_info(const MarcusPlayer *player, MarcusInfo *info);
    int marcus_object(MarcusPlayer *player, int index, MarcusObject *object);
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
