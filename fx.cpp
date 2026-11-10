#include "core.h"
#include <opxy/q31.hpp>

// A three-head tape echo with a spring reverb, after the classic studio tape
// echoes of the 1970s. A loop of tape passes one record head and three
// playback heads at the original's 1 : 1.9 : 2.75 spacing; Mode picks which heads (and whether
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

// Head 1 between 177 ms (Rate 0) and 69 ms (Rate 1), heads 2 and 3 at 1.9 and
// 2.75 times that: close to the original's range and spacing. Speed rises linearly with Rate, as a capstan motor's does.
const u32 kSlowest = 7806;                 // samples: 177 ms
const u32 kSpeedSpan = 51289;              // (177 / 69 - 1) in Q15
const i32 kHead2 = 1932735283;             // head 2 is 1 + 0.9 times head 1's delay
const i32 kHead3 = 1610612736;             // head 3 is 2 + 0.75 times
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
// Feedback. The loop's gain is 3 x the feedback word x the sum of the mode's
// head gains: the repeats are added 4 times, and the record head's soft clip
// (1.5x for small signals) is halved onto the tape. The knob sets the loop
// gain itself, the same in every mode, along a curve: up to 85 % the number of
// repeats before they fade by 60 dB grows exponentially, from 1 to 60, so the
// musical range gets most of the travel; from there the gain rises to 1.2,
// self-oscillating from about 90 %. 17 points of gain / 1.25, interpolated.
const i32 kFeedbackCurve[17] = {0, 10349231, 39088988, 104511535, 216392095, 370809002, 552397307,
                                741912950, 922895991, 1084696277, 1222434932, 1335508669, 1425869086,
                                1496652893, 1619561928, 1840573115, 2061584302};
// Per mode, 1.25 / (3 x its heads' sum), turning a loop gain into the word.
const i32 kOneHead = 894784853, kTwoHeads = 639132038, kThreeHeads = 514244169;
const i32 kModeFeedback[12] = {kOneHead, kOneHead, kOneHead, kTwoHeads, kOneHead, kOneHead,
                               kOneHead, kTwoHeads, kTwoHeads, kTwoHeads, kThreeHeads, 0};
// The spring: four stretched allpasses (dispersion, the spring's chirp) into a
// 42 ms line, damped and fed back. The left hears the line's end, the right
// a point 12 ms earlier, so the tail is wide while the echo stays centred.
const u32 kSpringLength = 1871, kSpringRight = 1327;
const u32 kStretch[kAllpasses] = {3, 5, 7, 11};
// The Shift row shapes the spring. Each row's first choice is the original
// sound, since a new project starts there.
// Decay: the line's feedback for a 60 dB decay from 1 s to 4 s.
const i32 kDecay[16] = {1610612736, 1643943243, 1683180964, 1719770473, 1753822680, 1785456097,
                        1814793969, 1841961878, 1867085755, 1890290281, 1911697605, 1931426351,
                        1949590872, 1966300711, 1981660228, 1995768380};
// Tone: the damping low-pass from about 3 kHz to 12 kHz.
const i32 kTone[16] = {751619277, 803707700, 863341777, 925711118, 990641526, 1057897675,
                       1127178106, 1198111582, 1270255354, 1343095968, 1416053263, 1488488148,
                       1559714694, 1629016816, 1695669589, 1758964773};
// Chirp: the allpasses' coefficient from 0.6 to 0.85, the spring's "boing".
const i32 kChirp[16] = {1288490189, 1324281583, 1360072977, 1395864371, 1431655765, 1467447159,
                        1503238554, 1539029948, 1574821342, 1610612736, 1646404130, 1682195524,
                        1717986918, 1753778313, 1789569707, 1825361101};
// Drive: the spring's input pushed into the soft clip by 1x (off) to 12x, then
// brought back by 1 / (1.5 x drive) so only the crash is louder, not the spring.
const i32 kDriveGain[8] = {134217728, 201326592, 268435456, 402653184,
                           536870912, 805306368, 1073741824, 1610612736};  // Q27
const i32 kDriveBack[8] = {0, 954437177, 715827883, 477218588, 357913941, 238609294, 178956971, 119304647};
// The output limiter holds the return's peaks under about -9 dBFS, reacting
// within a block and recovering over about 50 ms.
const i32 kCeiling = 760000000;            // 0.354: -9 dBFS
const i32 kRecovery = 32;                  // blocks: a 32nd of the way back per block
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
// The span is halved before the multiply so the product fits 32 bits.
i32 delay_for(i32 rate) {
    const u32 speed = 32768u + ((static_cast<u32>(control(rate)) >> 16) * (kSpeedSpan >> 1) >> 14);
    return static_cast<i32>(kSlowest * (0x80000000u / speed));
}

// The Feedback knob's curve, interpolated between its 17 points.
i32 feedback_for(i32 word) {
    const u32 x = static_cast<u32>(control(word));
    const u32 index = x >> 27, part = (x >> 12) & 0x7fff;  // 16 segments, Q15 within one
    if (index >= 16) return kFeedbackCurve[16];
    const i32 low = kFeedbackCurve[index], high = kFeedbackCurve[index + 1];
    return low + multiply(high - low, static_cast<i32>(part << 16));
}

// An enum word's choice among `count`.
u32 choice(i32 word, u32 count) {
    const u32 c = ((static_cast<u32>(control(word)) >> 16) * count) >> 15;
    return c < count ? c : count - 1;
}

// Tone: bass and treble shelves on one channel.
inline i32 tone(i32 x, i32& low, i32& high, i32 bass_gain, i32 treble_gain) {
    low = add(low, multiply(add(x, -low), kBassCorner));
    const i32 above = add(x, -high);
    high = add(high, multiply(above, kTrebleCorner));
    return add(x, add(multiply(low, bass_gain), multiply(above, treble_gain)));
}

// One allpass, (z^-k - a) / (1 - a z^-k), with one multiply:
// y[n] = x[n-k] + a (y[n-k] - x[n]), from its input line and output line.
inline i32 disperse(i32* lines, u32 now, u32 back, i32 x, i32 coefficient) {
    const i32 y = add(lines[back], multiply(add(lines[kAllpass + back], -x), coefficient));
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
    state.feedback = smooth(state.feedback, multiply(feedback_for(parameters[2]), kModeFeedback[mode]));
    // The Shift row: decay, tone, chirp and drive of the spring.
    if (!state.limit) {  // first block after initialize: start at the set values
        state.spring_feedback = kDecay[choice(parameters[8], 16)];
        state.spring_tone = kTone[choice(parameters[9], 16)];
        state.chirp = kChirp[choice(parameters[10], 16)];
        state.limit = kMaximum;
    }
    state.spring_feedback = smooth(state.spring_feedback, kDecay[choice(parameters[8], 16)]);
    state.spring_tone = smooth(state.spring_tone, kTone[choice(parameters[9], 16)]);
    state.chirp = smooth(state.chirp, kChirp[choice(parameters[10], 16)]);
    const u32 drive = choice(parameters[11], 8);
    const i32 drive_gain = kDriveGain[drive], drive_back = kDriveBack[drive];
    const i32 spring_feedback = state.spring_feedback, spring_tone = state.spring_tone, chirp = state.chirp;
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
    i32 tone_low_r = state.tone_low_r, tone_high_r = state.tone_high_r;
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
        const i32 d2 = d1 + multiply(d1, kHead2), d3 = d1 + d1 + multiply(d1, kHead3);
        const i32 heard = multiply(tap(tape, write, d1), h0) + multiply(tap(tape, write, d2), h1) +
                          multiply(tap(tape, write, d3), h2);
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
        spring_damp += multiply(add(out, -spring_damp), spring_tone);
        i32 into = multiply(add(half(input), played), send);
        if (drive) {  // Drive: push the spring's input into the soft clip
            const i32 pushed = multiply(into, drive_gain);  // into x gain / 16
            const i32 twice = add(pushed, pushed);
            into = multiply(clip(eight(twice)), drive_back);
        }
        i32 v = add(into, multiply(spring_damp, spring_feedback));
        const u32 now = write & (kAllpass - 1);
        v = disperse(allpass, now, (write - kStretch[0]) & (kAllpass - 1), v, chirp);
        v = disperse(allpass + 2 * kAllpass, now, (write - kStretch[1]) & (kAllpass - 1), v, chirp);
        v = disperse(allpass + 4 * kAllpass, now, (write - kStretch[2]) & (kAllpass - 1), v, chirp);
        v = disperse(allpass + 6 * kAllpass, now, (write - kStretch[3]) & (kAllpass - 1), v, chirp);
        line[write & (kSpring - 1)] = v;

        // The return: echo and spring, each side through its own bass and
        // treble, the right side hearing the spring's second point for width.
        // The spring returns eight times its quarter-level send.
        const i32 repeats = multiply(add(played, half(played)), echo);
        const i32 x_left = add(repeats, eight(multiply(out, reverb)));
        const i32 x_right = add(repeats, eight(multiply(wide, reverb)));
        left[i] = tone(x_left, tone_low, tone_high, bass_gain, treble_gain);
        right[i] = tone(x_right, tone_low_r, tone_high_r, bass_gain, treble_gain);
        write = (write + 1) & kMask;
    }

    // The limiter: one gain per block from the block's peak, ramped from the
    // last block's gain, so the return never runs away however hard the
    // feedback is pushed.
    u32 peak = 0;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const u32 l = left[i] < 0 ? static_cast<u32>(-(left[i] + 1)) : static_cast<u32>(left[i]);
        const u32 r = right[i] < 0 ? static_cast<u32>(-(right[i] + 1)) : static_cast<u32>(right[i]);
        peak = l > peak ? l : peak;
        peak = r > peak ? r : peak;
    }
    i32 target = state.limit + (kMaximum - state.limit) / kRecovery;  // recover
    if (peak > static_cast<u32>(kCeiling)) {
        // ceiling / peak, in Q15 then Q31: one division per block.
        const i32 hold = static_cast<i32>(((static_cast<u32>(kCeiling) >> 16) << 15) / (peak >> 16)) << 16;
        target = hold < target ? hold : target;
    }
    const i32 limit_step = (target - state.limit) / static_cast<i32>(opxy::block_frames);
    i32 limit = state.limit;
    if (limit != kMaximum || target != kMaximum) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            limit += limit_step;
            left[i] = multiply(left[i], limit);
            right[i] = multiply(right[i], limit);
        }
    }
    state.limit = target;
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
    state.tone_low_r = tone_low_r;
    state.tone_high_r = tone_high_r;
}
}
