#include "core.h"
#include <opxy/q31.hpp>

// A three-head tape echo with a spring reverb, after the classic studio tape
// echoes of the 1970s. A loop of tape passes one record head and three
// playback heads at 1 : 2 : 3 spacing; Mode picks which heads (and whether
// the spring) sound, Rate the tape speed, Intensity how much of the heads is
// recorded again. Everything is Q31; the send returns only echo and reverb.
namespace oread {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::q31::add;
using opxy::q31::half;
using opxy::q31::multiply;
const i32 kMaximum = opxy::q31::maximum;

// Head 1 between 190 ms (Rate 0) and 55 ms (Rate 1); heads 2 and 3 at two and
// three times that. Speed rises linearly with Rate, as a capstan motor's does.
const u32 kSlowest = 8379;                 // samples: 190 ms
const u32 kSpeedSpan = 80431;              // (190 / 55 - 1) in Q15
const u32 kMask = kTape - 1;
const i32 kGlide = 256;                    // blocks: tape speed settles in about 0.37 s
// Wow (0.8 Hz), drift (0.13 Hz) and flutter (7 Hz): phase steps per block.
const u32 kWowStep = 77912u * 64u, kDriftStep = 12660u * 64u, kFlutterStep = 681729u * 64u;
const i32 kMostMotion = 8589935;           // 0.4 % speed deviation at Wear 1
// Tape loss: a one-pole low-pass from about 5.6 kHz (Wear 0) to 2 kHz (Wear 1),
// then a one-pole low cut near 85 Hz, so repeats darken and thin as on tape.
const i32 kBrightest = 1181116006;         // 0.55
const i32 kWearDarkening = 644245094;      // 0.30
const i32 kRumble = 25769803;              // 0.012
// Intensity is a loop gain of up to 3.2 after the record head's 0.375: 1.2 at
// the top, so the echo self-oscillates from about 83 % and the saturating head
// bounds it.
const i32 kMostFeedback = 1717986918;      // 0.8, times 4 below
// The spring: four stretched allpasses (dispersion, the spring's chirp) into a
// 42 ms line, damped and fed back. The left hears the line's end, the right
// a point 12 ms earlier, so the tail is wide while the echo stays centred.
const u32 kSpringLength = 1871, kSpringRight = 1327;
const u32 kStretch[kAllpasses] = {3, 5, 7, 11};
const i32 kDispersion = 1288490189;        // allpass coefficient 0.6
const i32 kSpringDamp = 751619277;         // 0.35: about 3 kHz
const i32 kSpringFeedback = 1610612736;    // 0.75: a decay of about a second
// Bass and treble: shelving around 250 Hz and 3 kHz, from cut to +6 dB.
const i32 kBassCorner = 75161928;          // 0.035
const i32 kTrebleCorner = 751619277;       // 0.35
const u32 kClearPerBlock = 8192;           // words zeroed per block after initialize

// Mode: per-head playback gains, summed heads scaled so the sum stays level.
const i32 kOne = kMaximum, kTwo = 1503238554 /* 0.7 */, kThree = 1245540515 /* 0.58 */;
const i32 kModeHeads[12][3] = {
    {kOne, 0, 0}, {0, kOne, 0}, {0, 0, kOne}, {0, kTwo, kTwo},
    {kOne, 0, 0}, {0, kOne, 0}, {0, 0, kOne}, {kTwo, kTwo, 0},
    {0, kTwo, kTwo}, {kTwo, 0, kTwo}, {kThree, kThree, kThree}, {0, 0, 0}};
const bool kModeSpring[12] = {false, false, false, false, true, true, true, true, true, true, true, true};

i32 control(i32 word) { return word < 0 ? 0 : word; }

// Per block, a quarter of the way to a non-negative target: about 5 ms.
i32 smooth(i32 value, i32 target) {
    const i32 difference = target - value, step = difference / 4;
    return value + (step ? step : difference);
}

// The per-sample step that ramps `value` to its smoothed next block value.
i32 ramp(i32 value, i32 target) { return (smooth(value, target) - value) / static_cast<i32>(opxy::block_frames); }

// Cubic soft clip, 1.5x - 0.5x^3: the record head's saturation, at most full scale.
i32 clip(i32 x) {
    const i32 cube = multiply(multiply(x, x), x);
    return add(x, -half(cube - x));  // |x^3 - x| < 0.39 on [-1, 1]
}

// A parabolic sine of a phase, Q31.
i32 sine(u32 phase) {
    const i32 x = static_cast<i32>(phase);
    const i32 magnitude = x < 0 ? -(x + 1) : x;
    const i32 y = multiply(x, kMaximum - magnitude);  // x(1 - |x|), at most 1/4
    return add(add(y, y), add(y, y));
}

// The tape `delay` (Q16 samples) behind the next write, between its nearest samples.
// Tape samples are at most half scale, so their difference cannot overflow.
i32 tap(const i32* tape, u32 write, i32 delay) {
    const u32 whole = static_cast<u32>(delay) >> 16, part = static_cast<u32>(delay) & 0xffff;
    const i32 near = tape[(write - whole) & kMask], far = tape[(write - whole - 1) & kMask];
    return near + multiply(far - near, static_cast<i32>(part << 15));
}

// Head 1's delay for a Rate word, Q16 samples: one division per block.
i32 delay_for(i32 rate) {
    const u32 speed = 32768u + ((static_cast<u32>(control(rate)) >> 16) * kSpeedSpan >> 15);
    return static_cast<i32>(kSlowest * (0x80000000u / speed));
}

// One allpass, (z^-k - a) / (1 - a z^-k), with one multiply:
// y[n] = x[n-k] + a (y[n-k] - x[n]), from its input line and output line.
inline i32 disperse(i32* lines, u32 now, u32 back, i32 x) {
    const i32 y = add(lines[back], multiply(add(lines[kAllpass + back], -x), kDispersion));
    lines[now] = x;
    lines[kAllpass + now] = y;
    return y;
}

i32 eight(i32 x) {  // saturating
    const i32 four = add(add(x, x), add(x, x));
    return add(four, four);
}

u32 mode_for(i32 word) { return ((static_cast<u32>(control(word)) >> 16) * 12) >> 15; }

void silence(opxy::Channel left, opxy::Channel right) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
}
}

void initialize(State& state) {
    // An engine reset runs this again: the buffer is cleared again, a block at a time.
    state = State();
}

void process(State& state, opxy::Buffer buffer, opxy::Channel left, opxy::Channel right,
             opxy::Parameters parameters) {
    if (!buffer.words || buffer.count < kBufferWords) {
        silence(left, right);
        return;
    }
    i32* const words = buffer.words;
    // The buffer holds whatever was there before: clear it first (about 7 ms).
    if (__builtin_expect(state.cleared < kBufferWords, 0)) {
        const u32 end = state.cleared + kClearPerBlock < kBufferWords ? state.cleared + kClearPerBlock : kBufferWords;
        for (u32 i = state.cleared; i < end; ++i) words[i] = 0;
        state.cleared = end;
        silence(left, right);
        return;
    }
    i32* const tape = words;

    // Per block: targets, the mode's gains and the tape motion.
    const u32 mode = mode_for(parameters[0]);
    const i32 target_delay = delay_for(parameters[1]);
    if (!state.delay) state.delay = target_delay;  // start at the set speed
    // The tape motor glides to a new speed, a ramp each block.
    const i32 delay_step = (target_delay - state.delay) / kGlide / static_cast<i32>(opxy::block_frames);
    const i32* const heads = kModeHeads[mode];
    state.feedback = smooth(state.feedback, multiply(control(parameters[2]), kMostFeedback));
    state.bass = smooth(state.bass, control(parameters[5]));
    state.treble = smooth(state.treble, control(parameters[6]));
    const i32 wear = control(parameters[7]);
    state.damping = smooth(state.damping, kBrightest - multiply(wear, kWearDarkening));
    const i32 head_step[3] = {ramp(state.head[0], heads[0]), ramp(state.head[1], heads[1]), ramp(state.head[2], heads[2])};
    // The spring hears a quarter, for headroom in its allpasses.
    const i32 send_step = ramp(state.send, kModeSpring[mode] ? kMaximum / 4 : 0);
    const i32 echo_step = ramp(state.echo, control(parameters[3]));
    const i32 reverb_step = ramp(state.reverb, control(parameters[4]));

    state.wow += kWowStep;
    state.drift += kDriftStep;
    state.flutter += kFlutterStep;
    const i32 wobble = add(add(multiply(sine(state.wow), 1395864371 /* 0.65 */),
                               multiply(sine(state.drift), 579820585 /* 0.27 */)),
                           multiply(sine(state.flutter), 171798692 /* 0.08 */));
    const i32 motion_step = (multiply(wobble, multiply(wear, kMostMotion)) - state.motion) /
                            static_cast<i32>(opxy::block_frames);
    // Bass and treble: each shelf from cut (-1) to +6 dB (+1).
    const i32 bass_gain = (state.bass - 0x40000000) * 2, treble_gain = (state.treble - 0x40000000) * 2;
    const i32 feedback = state.feedback, damping = state.damping;

    // The sample loop works on locals, written back after.
    i32* const line = words + kSpringBase;
    i32* const allpass = words + kAllpassBase;
    u32 write = state.write;
    i32 delay = state.delay, motion = state.motion, h0 = state.head[0], h1 = state.head[1], h2 = state.head[2];
    i32 send = state.send, echo = state.echo, reverb = state.reverb;
    i32 low_loop = state.low_loop, rumble = state.rumble, spring_damp = state.spring_damp;
    i32 tone_low = state.tone_low, tone_high = state.tone_high;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        // Wow and flutter wobble the tape speed.
        delay += delay_step;
        motion += motion_step;
        h0 += head_step[0];
        h1 += head_step[1];
        h2 += head_step[2];
        send += send_step;
        echo += echo_step;
        reverb += reverb_step;
        const i32 d1 = delay + multiply(delay, motion);

        // Playback: three heads (tape samples are at most half scale, so their
        // weighted sum stays below full scale), then the tape's
        // high-frequency loss and low cut.
        const i32 heard = multiply(tap(tape, write, d1), h0) + multiply(tap(tape, write, d1 * 2), h1) +
                          multiply(tap(tape, write, d1 * 3), h2);
        low_loop = add(low_loop, multiply(add(heard, -low_loop), damping));
        rumble += multiply(add(low_loop, -rumble), kRumble);
        const i32 played = add(low_loop, -rumble);

        // Record: the input plus the repeats, saturated by the record head.
        const i32 input = half(left[i]) + half(right[i]);
        const i32 again = multiply(played, feedback);
        tape[write] = half(clip(add(input, add(add(again, again), add(again, again)))));

        // The spring hears the input and the echo.
        const i32 out = line[(write - kSpringLength) & (kSpring - 1)];
        const i32 wide = line[(write - kSpringRight) & (kSpring - 1)];
        spring_damp += multiply(add(out, -spring_damp), kSpringDamp);
        i32 v = add(multiply(add(half(input), played), send), multiply(spring_damp, kSpringFeedback));
        const u32 now = write & (kAllpass - 1);
        v = disperse(allpass, now, (write - kStretch[0]) & (kAllpass - 1), v);
        v = disperse(allpass + 2 * kAllpass, now, (write - kStretch[1]) & (kAllpass - 1), v);
        v = disperse(allpass + 4 * kAllpass, now, (write - kStretch[2]) & (kAllpass - 1), v);
        v = disperse(allpass + 6 * kAllpass, now, (write - kStretch[3]) & (kAllpass - 1), v);
        line[write & (kSpring - 1)] = v;

        // The return: echo and spring, through bass and treble, with the
        // spring's second point widening the right side.
        // The spring returns eight times its quarter-level send.
        const i32 spring = multiply(out, reverb), right_spring = multiply(wide, reverb);
        const i32 x = add(multiply(add(played, half(played)), echo), eight(spring));
        tone_low = add(tone_low, multiply(add(x, -tone_low), kBassCorner));
        const i32 high = add(x, -tone_high);
        tone_high = add(tone_high, multiply(high, kTrebleCorner));
        const i32 toned = add(x, add(multiply(tone_low, bass_gain), multiply(high, treble_gain)));
        left[i] = toned;
        const i32 side = add(right_spring, -spring);
        right[i] = add(toned, eight(side));
        write = (write + 1) & kMask;
    }
    state.write = write;
    state.delay = delay;
    state.motion = motion;
    state.head[0] = h0;
    state.head[1] = h1;
    state.head[2] = h2;
    state.send = send;
    state.echo = echo;
    state.reverb = reverb;
    state.low_loop = low_loop;
    state.rumble = rumble;
    state.spring_damp = spring_damp;
    state.tone_low = tone_low;
    state.tone_high = tone_high;
}
}
