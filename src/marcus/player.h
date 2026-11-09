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

    typedef struct MarcusImage
    {
        int width, height, pitch;
        const unsigned char *rgba;
        MarcusCamera camera;
    } MarcusImage;

    MarcusPlayer *marcus_create(void);
    void marcus_destroy(MarcusPlayer *player);
    const char *marcus_error(const MarcusPlayer *player);
    /* resources is 3dmovie.chk (a built utest.chk also works). content_directory
     * supplies the four original .3cn archives. Unknown expansion sources report
     * their missing source ID, rather than silently replacing original content. */
    int marcus_open(MarcusPlayer *player, const char *movie, const char *resources, const char *content_directory);
    int marcus_scene(const MarcusPlayer *player, int index, MarcusScene *scene);
    int marcus_select_scene(MarcusPlayer *player, int index);
    int marcus_info(const MarcusPlayer *player, MarcusInfo *info);
    int marcus_object(MarcusPlayer *player, int index, MarcusObject *object);
    int marcus_seek(MarcusPlayer *player, long frame);
    int marcus_render(MarcusPlayer *player, int width, int height, int authored_camera, float yaw, float pitch,
                      float zoom, MarcusImage *image);
    /* Explicit camera position in original world units, independent of frame
     * evaluation. Position is limited to +/-32700 by the fixed-point renderer. */
    int marcus_render_fly(MarcusPlayer *player, int width, int height, const MarcusCamera *camera, MarcusImage *image);

#ifdef __cplusplus
}
#endif
#endif
