#include "preview_draft.h"
#include "asset_resources.h"
#include "body_renderer.h"
#include "mm/chunk_copy.h"
#include "mm/preview_actor.h"
#include "mm/text_resource.h"

#define FONT_TYPE 0x54444620UL

typedef struct ActorDraft
{
    const MMNativePathChar *path;
    const MMPrivateArchiveProfile *profile;
    MMU32 creator;
    MMPrivateArchive *archive;
} ActorDraft;

static int create_track_archive(void *p, MMArchiveWriter *writer)
{
    ActorDraft *draft = p;
    draft->archive = mm_native_private_archive_open(draft->path, MM_PRIVATE_ARCHIVE_CREATE, draft->profile);
    if (!draft->archive)
        return 0;
    *writer = mm_private_archive_writer(draft->archive);
    return 1;
}

static int save_track_archive(void *p)
{
    ActorDraft *draft = p;
    return mm_private_archive_save(draft->archive, draft->creator);
}

int mm_preview_store_actor_tracks(const MMNativePathChar *path, const MMPrivateArchiveProfile *profile, MMU32 creator,
                                  const MMActorTrack *track, int with_route)
{
    ActorDraft draft = {NULL, NULL, 0, NULL};
    MMActorTrackArchive archive = {NULL, create_track_archive, save_track_archive};
    int ok;
    draft.path = path;
    draft.profile = profile;
    draft.creator = creator;
    archive.context = &draft;
    ok = mm_actor_store_edit_tracks(track, with_route, &archive);
    mm_private_archive_close(draft.archive);
    return ok;
}

static int text_template(MMTemplateOwner *tmpl)
{
    return tmpl && (mm_template_owner_state(tmpl)->flags & MM_TEMPLATE_TEXT);
}

static MMResourceCache *resolve(const MMPreviewDraftServices *services, MMI32 sid, MMU32 type, MMU32 number)
{
    MMTag tag;
    tag.sid = sid;
    tag.type = type;
    tag.number = number;
    tag.cache = NULL;
    return services->resolve(services->context, &tag);
}

static MMMaterialOwner *text_material(const MMPreviewLive *live, const MMPreviewMaterialSource *source,
                                      const MMPreviewDraftServices *services)
{
    const MMPreviewActor *actor;
    MMMaterialOwner *material;
    int custom;
    if (source->preset)
    {
        MMResourceCache *cache = resolve(services, source->source, MM_MATERIAL_CHUNK, source->number);
        return cache ? mm_brender_material_fetch(cache, MM_MATERIAL_CHUNK, source->number) : NULL;
    }
    if (source->object < 0 || (MMU32)source->object >= live->actors.count)
        return mm_brender_material_new(-1, -1);
    actor = mm_preview_live_actor_view(live, (MMU32)source->object);
    if (!actor || !actor->body || !text_template(actor->tmpl))
        return NULL;
    material = mm_brender_body_material(mm_kauai_body_renderer(), actor->body, 0, &custom);
    if (!material || custom)
        return NULL;
    mm_material_owner_retain(material);
    return material;
}

int mm_preview_read_text_material(const MMPreviewLive *live, const MMPreviewMaterialSource *source,
                                  MMMaterialAppearance *appearance, const MMPreviewDraftServices *services)
{
    MMMaterialOwner *material = text_material(live, source, services);
    if (!material)
        return MM_PREVIEW_DRAFT_MATERIAL_READ;
    mm_material_owner_read_view(material, appearance);
    mm_material_owner_release(material);
    return MM_PREVIEW_DRAFT_OK;
}

int mm_preview_store_text_material(const MMPreviewLive *live, const MMPreviewMaterialSource *source,
                                   const MMMaterialAppearance *appearance, const MMPreviewDraftServices *services)
{
    MMMaterialOwner *material = text_material(live, source, services);
    const MMNativePathChar *path;
    MMPrivateArchive *archive;
    int status;
    if (!material)
        return MM_PREVIEW_DRAFT_MATERIAL_FETCH;
    path = services->path(services->context);
    if (!path)
        status = MM_PREVIEW_DRAFT_MATERIAL_PATH;
    else
    {
        archive = mm_native_private_archive_open(path, MM_PRIVATE_ARCHIVE_EDIT, services->profile);
        status = MM_PREVIEW_DRAFT_MATERIAL_WRITE;
        if (archive)
        {
            MMArchiveWriter writer = mm_private_archive_writer(archive);
            MMU32 chunk = 0;
            if (mm_material_owner_write_view(material, &writer, MM_MATERIAL_CHUNK, &chunk, appearance) && chunk == 0)
                status = mm_private_archive_save(archive, services->creator) ? MM_PREVIEW_DRAFT_OK :
                    MM_PREVIEW_DRAFT_MATERIAL_SAVE;
            mm_private_archive_close(archive);
        }
    }
    mm_material_owner_release(material);
    return status;
}

int mm_preview_bind_text_font(MMI32 source, MMU32 font, const MMPreviewDraftServices *services)
{
    MMResourceCache *cache = resolve(services, source, FONT_TYPE, font);
    const MMNativePathChar *path;
    MMPrivateArchive *archive;
    MMResourceChunkCopy binding;
    MMChunkEndpoint chunks;
    int status;
    if (!cache || !(path = services->path(services->context)))
        return MM_PREVIEW_DRAFT_FONT_SOURCE;
    archive = mm_native_private_archive_open(path, MM_PRIVATE_ARCHIVE_EDIT, services->profile);
    if (!archive)
        return MM_PREVIEW_DRAFT_FONT_WRITE;
    chunks = mm_resource_cache_copy_source(cache, &binding);
    status = mm_private_archive_bind_font(archive, &chunks, 0, font);
    if (status == MM_CHUNK_OK)
        status = mm_private_archive_save(archive, services->creator) ? MM_PREVIEW_DRAFT_OK :
            MM_PREVIEW_DRAFT_FONT_SAVE;
    else
        status = status == MM_CHUNK_NOT_FOUND ? MM_PREVIEW_DRAFT_FONT_MISSING : MM_PREVIEW_DRAFT_FONT_WRITE;
    mm_private_archive_close(archive);
    return status;
}

int mm_preview_store_text_preview(const MMPreviewLive *live, const MMPreviewMaterialSource *source,
                                  const MMMaterialAppearance *appearance, MMI32 font_source, MMU32 font,
                                  const MMPreviewDraftServices *services)
{
    MMMaterialAppearance current;
    int status;
    if (!appearance)
    {
        status = mm_preview_read_text_material(live, source, &current, services);
        if (status != MM_PREVIEW_DRAFT_OK)
            return status;
        appearance = &current;
    }
    if (!mm_material_appearance_valid(appearance))
        return MM_PREVIEW_DRAFT_MATERIAL_INVALID;
    status = mm_preview_store_text_material(live, source, appearance, services);
    return status == MM_PREVIEW_DRAFT_OK ? mm_preview_bind_text_font(font_source, font, services) : status;
}

int mm_preview_use_text_material(const MMPreviewAssets *assets, MMResourceCache *cache)
{
    MMMaterialOwner *material;
    if (!assets->body || !text_template(assets->tmpl))
        return MM_PREVIEW_DRAFT_NOT_TEXT;
    material = mm_brender_material_fetch(cache, MM_MATERIAL_CHUNK, 0);
    if (!material)
        return MM_PREVIEW_DRAFT_MATERIAL_LOAD;
    mm_body_set_generic(assets->body, 0, material, &mm_kauai_body_renderer()->edit.runtime);
    mm_material_owner_release(material);
    return MM_PREVIEW_DRAFT_OK;
}
