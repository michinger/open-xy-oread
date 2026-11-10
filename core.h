#pragma once
#include <opxy/effect.hpp>

namespace spool {
// The send slot's buffer, in Q31 words: the tape loop, then the spring's
// delay line and its four allpasses' short input and output lines.
const opxy::u32 kTape = 32768;            // 0.74 s, a power of two
const opxy::u32 kSpring = 2048;           // the spring's delay line
const opxy::u32 kAllpass = 16;            // each allpass input or output line
const opxy::u32 kAllpasses = 4;
const opxy::u32 kSpringBase = kTape, kAllpassBase = kSpringBase + kSpring;
const opxy::u32 kBufferWords = kAllpassBase + 2 * kAllpasses * kAllpass;  // definition.json: bufferBytes 139776

struct State {
    opxy::u32 cleared;              // buffer words zeroed since initialize
    opxy::u32 write;                // tape and spring write position
    opxy::i32 delay;                // head 1's delay, Q16 samples, gliding like a tape motor
    opxy::i32 motion;               // wow and flutter: relative speed offset, Q31
    opxy::u32 wow, flutter, drift;  // LFO phases
    opxy::i32 head[3], send, echo, reverb;     // gains ramped per sample, Q31
    opxy::i32 feedback, bass, treble, damping; // controls smoothed per block
    opxy::i32 low_loop, rumble;     // the tape loop's high-frequency loss and low cut
    opxy::i32 spring_damp;          // the spring's damping filter
    opxy::i32 tone_low, tone_high;  // bass and treble filters
};
void initialize(State& state);
void process(State& state, opxy::Buffer buffer, opxy::Channel left, opxy::Channel right,
             opxy::Parameters parameters);
}
