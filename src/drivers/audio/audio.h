#ifndef _DRIVERS_AUDIO_AUDIO_H
#define _DRIVERS_AUDIO_AUDIO_H

#include <nexus/types.h>

typedef struct {
    bool initialized;
    bool output_available;
    bool muted;
    u8 volume_percent;
    const char *device_name;
} audio_status_t;

void audio_init(void);
const audio_status_t *audio_get_status(void);
bool audio_output_available(void);
bool audio_is_muted(void);
u8 audio_volume_percent(void);
void audio_set_volume_percent(u8 volume);
void audio_set_muted(bool muted);

#endif
