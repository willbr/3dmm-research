/* Live movie lifecycle converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_runtime.h"

void mm_movie_runtime_set_ref(void **slot, void *value, void (*retain)(void *), void (*release)(void *))
{
    if (*slot == value)
        return;
    if (*slot)
        release(*slot);
    *slot = value;
    if (value)
        retain(value);
}

int mm_movie_runtime_open(MMMovieRuntime *runtime, void *client, void *path, MMU32 root, int half,
                          const MMMovieRuntimeServices *s)
{
    const MMMovieSaveServices *save = s->save;
    MMMovieState *movie = save->data;
    void *working = path, *file = NULL, *title_path;
    MMTagList tags;
    int busy = 0, ok = 0;
    if (!s->prepare(s->context, path, &working))
        goto done;
    if (save->is_vmm(path))
    {
        save->set_container_name(save->context, path);
        save->state->containerNamed = 1;
    }
    if (!s->init_undo(s->context) || (!working && !mm_movie_init_actors(movie, s->owner)))
        goto done;
    runtime->world = s->create_world(s->context, client, half);
    if (!runtime->world)
        goto done;
    runtime->sounds = s->create_sounds(s->context);
    if (!runtime->sounds)
        goto done;
    mm_movie_runtime_set_ref(&runtime->client, client, s->retain, s->release);
    if (!working)
        return 1;

    save->long_op(save->context, 1);
    busy = 1;
    if (!mm_movie_load(movie, working, root, s->owner))
        goto done;
    file = s->owner->file(movie->resources);
    s->retain_archive(file);
    if (!mm_movie_save_bind(working, save))
    {
        s->missing_saved_file(s->context);
        goto done;
    }
    s->merge_sources(file, movie->movieNumber);
    if (!mm_movie_save_tags(&tags, save))
        goto done;
    ok = s->cache_tags(s->context, &tags);
    mm_tag_list_dispose(&tags);
    if (ok)
    {
        title_path = save->state->containerNamed ? save->container_name(save->context) : working;
        save->update_title(save->context, title_path);
        if (save->state->containerNamed)
            save->state->readOnly = save->read_only(save->context, title_path);
    }
done:
    if (file)
        s->owner->release_file(file);
    if (!ok)
        s->discard_document(s->context);
    if (busy)
        save->long_op(save->context, 0);
    return ok;
}

void mm_movie_runtime_dispose(MMMovieRuntime *runtime, MMPlaybackState *playback, const MMMovieRuntimeServices *s)
{
    if (runtime->user_paused)
        s->resume_audio(s->context);
    runtime->user_paused = 0;

    mm_movie_release_resources(s->save->data, s->owner);
    mm_movie_save_release(0, s->save);
    if (playback->playing)
    {
        s->stop_playback(runtime->client);
        if (playback->fadeOriginalVolume)
        {
            if (playback->fadeChangedVolume || !s->audio_fades_disabled(s->context))
                s->set_volume(s->context, playback->fadeOriginalVolume);
            playback->fadeOriginalVolume = 0;
        }
    }
    if (runtime->client)
        s->update_roll_call(runtime->client);
    if (s->scenes->state->open)
        mm_movie_scene_close_open(s->scenes);
    mm_movie_clear_actors(s->save->data, s->owner);

    mm_movie_runtime_set_ref(&runtime->client, NULL, s->retain, s->release);
    mm_movie_runtime_set_ref(&runtime->world, NULL, s->retain, s->release);
    mm_movie_runtime_set_ref(&runtime->sounds, NULL, s->retain, s->release);
    mm_movie_runtime_set_ref(&runtime->thumbnail_palette, NULL, s->retain, s->release);
}
