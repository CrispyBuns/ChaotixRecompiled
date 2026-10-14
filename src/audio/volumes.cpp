// See volumes.h.
#include "audio/volumes.h"
#include "runtime/system.h"

namespace chaotix::audio {

namespace {
constexpr uint32_t kTracks = 0x1C40;
constexpr uint32_t kTrackSize = 0x30;
constexpr int kFirstEffectTrack = 13;
constexpr int kEffectTracks = 5;
} // namespace

void set_music_and_effect_levels(Machine& m, float music, float effects) {
    float fm[6] = {music, music, music, music, music, music};
    float psg[4] = {music, music, music, music};
    for (int t = kFirstEffectTrack; t < kFirstEffectTrack + kEffectTracks; ++t) {
        const uint32_t a = kTracks + uint32_t(t) * kTrackSize;
        if (!(m.zram[a] & 0x80)) continue;
        const uint8_t voice = m.zram[a + 1];
        if (voice < 0x08 && voice != 0x03) {
            fm[voice < 3 ? voice : voice - 1] = effects;  // 00-02, 04-06
        } else if (voice >= 0x80) {
            const int tone = (voice >> 5) & 3;  // 80, A0, C0, E0
            psg[tone] = effects;
            if (tone == 2) psg[3] = effects;  // PSG3 also clocks the noise
        }
    }
    for (int c = 0; c < 6; ++c) m.ym.gain[c] = fm[c];
    for (int c = 0; c < 4; ++c) m.psg.gain[c] = psg[c];
    m.pwm_gain = music;
}

} // namespace chaotix::audio
