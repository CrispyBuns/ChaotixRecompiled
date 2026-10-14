// Separate music and sound effect volumes (issue #24).
//
// The Z80 sound driver keeps a 0x30-byte record per track from Z80 RAM $1C40:
// byte 0 is its playback control (bit 7: playing; bit 2: the channel has been
// taken by a sound effect), byte 1 the channel it drives (FM 00-02 and 04-06,
// PSG 80/A0/C0, PWM 18-1E). Tracks 0-12 play the music; tracks 13-17 are the
// sound effect slots. Measured over every sound effect the driver plays
// ($2A-$DF), those slots drive FM4, FM5, FM6, PSG2 and PSG3 and never PWM.
//
// So each frame a channel belongs to a sound effect while a playing sound
// effect slot drives it, and to the music otherwise; its level is set from
// that. PWM carries only music in the driver and follows the music volume.
// The levels are the host's: the game does not see them, and at 100% each
// the output is bit-identical to having no volume control at all.
#pragma once

namespace chaotix {
class Machine;
}

namespace chaotix::audio {

// Before each frame; levels are 0-1. Master volume is applied elsewhere.
void set_music_and_effect_levels(Machine& m, float music, float effects);

} // namespace chaotix::audio
