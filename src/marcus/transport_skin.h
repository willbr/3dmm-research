#ifndef MARCUS_TRANSPORT_SKIN_H
#define MARCUS_TRANSPORT_SKIN_H
/* Retail artwork on standard cimgui buttons. ImGui owns every interaction. */
void transport_skin_init(void);
int transport_navigation(const char *id, float x, float y, int action, int enabled, const char *hint);
int transport_readout(const char *id, float x, float y, int scene, const char *value, int enabled, const char *hint);
int transport_play(const char *id, float x, float y, int playing, int waiting, int enabled, const char *hint);
#endif
