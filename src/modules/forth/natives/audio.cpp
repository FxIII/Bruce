#include "audio.h"
#include "natives.h"
#include <globals.h>

// br.audio.tone ( freq duration -- )
static void fn_tone() {
    DCELL duration = dpop();
    DCELL freq     = dpop();
    log_d("fn_tone: freq=%lld duration=%lld soundEnabled=%d", freq, duration, bruceConfig.soundEnabled);
    if (!bruceConfig.soundEnabled) { log_d("fn_tone: sound disabled, skipping"); return; }
#if defined(BUZZ_PIN)
    log_d("fn_tone: using BUZZ_PIN=%d", BUZZ_PIN);
    tone(BUZZ_PIN, (uint32_t)freq, (uint32_t)duration);
#elif defined(HAS_NS4168_SPKR)
    log_d("fn_tone: using NS4168 serialCli.parse");
    serialCli.parse("tone " + String((uint32_t)freq) + " " + String((uint32_t)duration));
#else
    log_d("fn_tone: no audio output defined");
#endif
}

void forth_register_audio(const char *prefix) {
    char name[64];
    snprintf(name, sizeof(name), "%s.tone", prefix);
    forth_register(name, fn_tone);
}
