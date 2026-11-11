#ifndef REVIEW_BACKEND_H
#define REVIEW_BACKEND_H
/* Only the upstream Dear ImGui backend requires C++. Application code is C89. */
#ifdef __cplusplus
extern "C"
{
#endif
    int review_backend_init(void *window, void *renderer);
    unsigned long review_window_flags(int hidden);
    int review_pick_database(char *path, int capacity);
    void review_backend_event(const void *event);
    void review_backend_frame(void);
    void review_backend_render(void *renderer);
    void review_backend_quit(void);
#ifdef __cplusplus
}
#endif
#endif
