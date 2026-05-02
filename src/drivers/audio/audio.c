/* YamKernel - audio mixer/status foundation.
 * This is intentionally conservative: it exposes system audio state to the
 * desktop, while reporting no output device until a real HDA/virtio-audio
 * driver binds to hardware.
 */
#include "audio.h"
#include "../../lib/kprintf.h"

static audio_status_t g_audio_status = {
    .initialized = false,
    .output_available = false,
    .muted = false,
    .volume_percent = 50,
    .device_name = "No audio output"
};

void audio_init(void) {
    g_audio_status.initialized = true;
    kprintf("[AUDIO] mixer ready: device=none output=unavailable volume=%u muted=%u\n",
            g_audio_status.volume_percent, g_audio_status.muted ? 1 : 0);
}

const audio_status_t *audio_get_status(void) {
    return &g_audio_status;
}

bool audio_output_available(void) {
    return g_audio_status.initialized && g_audio_status.output_available;
}

bool audio_is_muted(void) {
    return g_audio_status.muted;
}

u8 audio_volume_percent(void) {
    return g_audio_status.volume_percent;
}

void audio_set_volume_percent(u8 volume) {
    if (volume > 100) volume = 100;
    g_audio_status.volume_percent = volume;
    if (volume > 0) g_audio_status.muted = false;
}

void audio_set_muted(bool muted) {
    g_audio_status.muted = muted;
}
