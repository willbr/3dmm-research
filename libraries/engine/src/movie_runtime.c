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
    void *working = path, *title_path;
    MMTagList tags;
    int bundled = save->is_vmm(path), busy = 0, ok = 0;
    if (bundled && (!s->prepare_container || !s->container_working_path))
    {
        save->bad_file(save->context);
        goto done;
    }
    if (s->prepare_container &&
        !s->prepare_container(s->context, path, s->container_working_path, bundled, &save->state->container))
        goto done;
    if (bundled)
    {
        if (!save->state->container.document)
        {
            save->bad_file(save->context);
            goto done;
        }
        working = s->container_working_path;
        save->set_container_name(save->context, path);
        save->state->containerNamed = 1;
    }
    if (!s->init_undo(s->context) || (!working && !mm_movie_init_actors(movie, s->owner)))
        goto done;
    runtime->world = s->create_world(s->context, client, half);
    if (!runtime->world)
        goto done;
    mm_frame_sound_queue_prepare(&runtime->sounds);
    if (!mm_frame_sound_queue_open_owners(&runtime->sounds, s->sound_allocator, s->sound_trash, &s->sound_output))
    {
        mm_frame_sound_queue_close(&runtime->sounds);
        goto done;
    }
    mm_movie_runtime_set_ref(&runtime->client, client, s->retain, s->release);
    if (!working)
        return 1;

    save->long_op(save->context, 1);
    busy = 1;
    if (!mm_movie_load(movie, working, root, s->owner))
        goto done;
    if (!mm_movie_save_bind(working, save))
    {
        s->missing_saved_file(s->context);
        goto done;
    }
    s->merge_sources(movie->resources, movie->movieNumber);
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
    if (!ok)
        s->discard_document(s->context);
    if (busy)
        save->long_op(save->context, 0);
    return ok;
}

void mm_movie_runtime_dispose(MMMovieRuntime *runtime, MMPlaybackState *playback, const MMMovieRuntimeServices *s)
{
    MMMovieContainer *container = &s->save->state->container;
    if (runtime->transport.paused)
        s->resume_audio(s->context);
    runtime->transport.paused = 0;

    mm_movie_release_resources(s->save->data, s->owner);
    mm_movie_save_release(0, s->save);
    if (playback->playing)
    {
        s->stop_playback(runtime->client);
        mm_playback_restore_volume(playback, s->context, s->audio_fades_disabled, s->set_volume);
    }
    if (runtime->client)
        s->update_roll_call(runtime->client);
    if (s->scenes->state->open)
        mm_movie_scene_close_open(s->scenes);
    mm_movie_clear_actors(s->save->data, s->owner);

    mm_movie_runtime_set_ref(&runtime->client, NULL, s->retain, s->release);
    mm_movie_runtime_set_ref(&runtime->world, NULL, s->retain, s->release);
    mm_frame_sound_queue_close(&runtime->sounds);
    mm_movie_runtime_set_ref(&runtime->thumbnail_palette, NULL, s->retain, s->release);
    if (container->document)
    {
        void *document = container->document;
        const MMMovieContainerServices *services = container->services;
        container->document = NULL;
        container->services = NULL;
        services->release(document);
    }
}
