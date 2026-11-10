// Oread's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../fx.cpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

static opxy::i32 words[oread::kBufferWords];
static const opxy::Buffer buffer = {words, oread::kBufferWords};
static const opxy::i32 kFull = 0x7fffffff;

static opxy::i32 mode(unsigned index) { return static_cast<opxy::i32>((index * 2 + 1) * (0x80000000u / 24)); }

struct Run {
    oread::State state;
    opxy::i32 p[12];
    std::vector<double> left, right;
    Run(unsigned m, opxy::i32 rate, opxy::i32 intensity) {
        // A dirty buffer, as the slot's previous effect leaves it.
        for (opxy::u32 i = 0; i < oread::kBufferWords; ++i) words[i] = static_cast<opxy::i32>(i * 2654435761u);
        oread::initialize(state);
        const opxy::i32 q[12] = {mode(m), rate, intensity, kFull, kFull / 2, kFull / 2 + 1, kFull / 2 + 1, 0, 0, 0, 0, 0};
        for (unsigned i = 0; i < 12; ++i) p[i] = q[i];
    }
    template <class Input> void blocks(unsigned count, Input input) {
        opxy::i32 l[opxy::block_frames], r[opxy::block_frames];
        for (unsigned b = 0; b < count; ++b) {
            for (unsigned i = 0; i < opxy::block_frames; ++i) l[i] = r[i] = input(left.size() + i);
            oread::process(state, buffer, l, r, p);
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                left.push_back(l[i] / 2147483648.0);
                right.push_back(r[i] / 2147483648.0);
            }
        }
    }
};

static double peak(const std::vector<double>& x, size_t from, size_t to) {
    double m = 0;
    for (size_t i = from; i < to && i < x.size(); ++i) m = std::fmax(m, std::fabs(x[i]));
    return m;
}
static size_t loudest(const std::vector<double>& x, size_t from, size_t to) {
    size_t at = from;
    for (size_t i = from; i < to && i < x.size(); ++i) if (std::fabs(x[i]) > std::fabs(x[at])) at = i;
    return at;
}
static opxy::i32 zero(size_t) { return 0; }

int main() {
    const size_t start = 640;  // the buffer is cleared within the first blocks
    const size_t kImpulse = start + 100;
    auto impulse = [&](size_t n) { return n == kImpulse ? 0x40000000 : 0; };

    // Head timing: heads at 1, 2 and 3 times head 1's 190 ms (Rate 0) and 27.5 ms (Rate 1).
    const double slowest = 8379;
    for (unsigned head = 0; head < 3; ++head) {
        Run run(head, 0, 0);
        run.blocks(760, impulse);
        const double expected = slowest * (head + 1);
        const size_t at = loudest(run.left, kImpulse + 100, run.left.size());
        check("head lands at its spacing (Rate 0)", std::fabs(double(at - kImpulse) - expected) <= 3);
        check("silence before the head", peak(run.left, start, kImpulse + size_t(expected) - 10) < 1e-6);
    }
    {
        Run run(2, kFull, 0);
        run.blocks(300, impulse);
        const size_t at = loudest(run.left, kImpulse + 100, run.left.size());
        check("head 3 at Rate 1 lands near 82.5 ms", std::fabs(double(at - kImpulse) - 3 * 1212.7) <= 4);
    }
    // Two heads together, with a repeat of the earlier one's level.
    {
        Run run(3, 0, 0);
        run.blocks(760, impulse);
        check("mode 2+3 sounds head 2", peak(run.left, kImpulse + 2 * 8379 - 4, kImpulse + 2 * 8379 + 4) > 0.1);
        check("mode 2+3 sounds head 3", peak(run.left, kImpulse + 3 * 8379 - 4, kImpulse + 3 * 8379 + 4) > 0.1);
        check("mode 2+3 skips head 1", peak(run.left, kImpulse + 8379 - 50, kImpulse + 8379 + 50) < 1e-4);
    }
    // Reverb only: no echo at head 1, but a spring tail right away.
    {
        Run run(11, 0, 0);
        run.blocks(400, impulse);
        check("reverb mode rings", peak(run.left, kImpulse, kImpulse + 4000) > 0.01);
        check("reverb mode tail decays", peak(run.left, kImpulse + 22050 + 4000, run.left.size()) < 0.01);
        check("the spring is wider than the echo", run.left[kImpulse + 1500] != run.right[kImpulse + 1500]);
    }
    // Feedback at full intensity with all heads, the spring and full wear:
    // the echo self-oscillates but stays bounded, with no wrapped samples.
    {
        Run run(10, kFull / 3, kFull);
        run.p[7] = kFull;
        run.p[5] = run.p[6] = kFull;  // bass and treble at +6 dB
        unsigned n = 0;
        run.blocks(700, [&](size_t i) { n = n * 1664525u + 1013904223u; return i > start && i < start + 4410 ? static_cast<opxy::i32>(n) / 4 : 0; });
        run.blocks(3000, zero);  // about 4 s of silence in
        const size_t end = run.left.size();
        double worst = 0;
        bool finite = true;
        for (size_t i = 0; i < end; ++i) { worst = std::fmax(worst, std::fabs(run.left[i])); finite = finite && std::isfinite(run.left[i]); }
        check("self-oscillation keeps sounding", peak(run.left, end - 44100, end) > 0.05);
        check("self-oscillation stays within full scale", worst <= 1.0 && finite);
        bool bounded = true;
        for (opxy::u32 i = 0; i < oread::kTape; ++i) bounded = bounded && words[i] <= 0x40000000 && words[i] >= -0x40000000;
        check("the tape holds at most half scale", bounded);
        double jump = 0;
        for (size_t i = end - 44100; i < end; ++i) jump = std::fmax(jump, std::fabs(run.left[i] - run.left[i - 1]));
        check("no wrapped samples (jumps) at full intensity", jump < 1.5);
        double settled = 0;
        for (size_t i = end - 44100; i < end; ++i) settled = std::fmax(settled, std::fmax(std::fabs(run.left[i]), std::fabs(run.right[i])));
        check("the limiter holds full feedback under -8 dBFS", settled < 0.4);
    }
    // The Feedback knob's curve rises steadily and self-oscillates only near the top.
    {
        bool rising = true;
        opxy::i32 last = -1;
        for (unsigned k = 0; k <= 256; ++k) {
            const opxy::i32 f = oread::feedback_for(static_cast<opxy::i32>(k == 256 ? 0x7fffffffu : k * 0x800000u));
            rising = rising && f >= last;
            last = f;
        }
        check("the feedback curve never falls", rising);
        // The curve holds the loop gain / 1.25: below 1 until about 90 %.
        check("no self-oscillation at 85 %", 1.25 * oread::feedback_for(static_cast<opxy::i32>(0.85 * 2147483647.0)) / 2147483648.0 < 1.0);
        check("self-oscillation at 95 %", 1.25 * oread::feedback_for(static_cast<opxy::i32>(0.95 * 2147483647.0)) / 2147483648.0 > 1.0);
    }
    // The knob means the same in every mode: at 85 % even all three heads die away,
    // and at 40 % the repeats fade fast.
    for (unsigned m : {0u, 3u, 10u}) {
        for (double knob : {0.4, 0.85}) {
            Run run(m, kFull / 2, static_cast<opxy::i32>(knob * 2147483647.0));
            unsigned n = 3;
            run.blocks(700, [&](size_t i) { n = n * 1664525u + 1013904223u; return i > start && i < start + 4410 ? static_cast<opxy::i32>(n) / 4 : 0; });
            run.blocks(2100, zero);  // 3 s of silence in
            const double rest = peak(run.left, run.left.size() - 4410, run.left.size());
            char what[64];
            std::snprintf(what, sizeof what, "mode %u at %.0f %% dies away (%.4f)", m, knob * 100, rest);
            check(what, rest < (knob < 0.5 ? 1e-4 : 0.02));
        }
    }
    // Bass reaches the spring on both sides: cut against boost changes the right's tail too.
    {
        auto tail = [&](opxy::i32 bass, bool right) {
            Run run(11, 0, 0);
            run.p[5] = bass;
            unsigned n = 7;
            run.blocks(300, [&](size_t i) { n = n * 1664525u + 1013904223u; return i > start && i < start + 2205 ? static_cast<opxy::i32>(n) / 8 : 0; });
            const std::vector<double>& x = right ? run.right : run.left;
            double low = 0, sum = 0;  // a crude low-pass's energy over the tail
            for (size_t i = start + 2205; i < x.size(); ++i) { low += (x[i] - low) * 0.02; sum += low * low; }
            return sum;
        };
        check("bass shapes the left spring", tail(kFull, false) > 2 * tail(0, false));
        check("bass shapes the right spring", tail(kFull, true) > 2 * tail(0, true));
    }
    // The Shift row: a longer decay rings longer, and drive keeps the spring bounded.
    {
        auto ring = [&](opxy::i32 decay, opxy::i32 drive) {
            Run run(11, 0, 0);
            run.p[8] = decay;
            run.p[11] = drive;
            run.blocks(1400, impulse);  // about 2 s
            return peak(run.left, kImpulse + 44100, run.left.size());
        };
        check("decay 15 rings longer than decay 0", ring(kFull, 0) > 4 * ring(0, 0));
        check("full drive stays bounded", ring(0, kFull) < 1.0);
    }
    // No intensity and no spring: one repeat, then silence; silence in, silence out.
    {
        Run run(0, kFull / 2, 0);
        run.blocks(400, impulse);
        check("one repeat, then silence", peak(run.left, run.left.size() - 4410, run.left.size()) < 1e-5);
        Run quiet(10, kFull / 2, kFull / 2);
        quiet.blocks(400, zero);
        check("silence in, silence out (over a dirty buffer)", peak(quiet.left, 0, quiet.left.size()) == 0);
    }
    // Mode switching while a tone plays: gains glide, no clicks.
    {
        Run run(0, kFull / 2, kFull / 2);
        auto tone = [](size_t n) { return static_cast<opxy::i32>(0x30000000 * std::sin(n * 2 * M_PI * 220 / 44100)); };
        run.blocks(700, tone);
        auto jumps = [&](size_t from, size_t to) {
            double m = 0;
            for (size_t i = from; i < to; ++i) m = std::fmax(m, std::fabs(run.left[i] - run.left[i - 1]));
            return m;
        };
        bool clean = true;
        for (unsigned m = 1; m < 12; ++m) {
            const size_t from = run.left.size();
            const double before = jumps(from - 2000, from);
            run.p[0] = mode(m);
            run.blocks(140, tone);
            const double during = jumps(from, from + 640), after = jumps(run.left.size() - 2000, run.left.size());
            if (during > 1.5 * std::fmax(before, after) + 0.005) { std::printf("mode %u: %f %f %f\n", m, before, during, after); clean = false; }
        }
        check("mode changes do not click", clean);
    }
    // Speed glide: Rate 0 to 1 moves the tape smoothly, with pitch, not in a jump.
    {
        Run run(0, 0, 0);
        run.blocks(20, zero);
        const opxy::i32 before = run.state.delay;
        run.p[1] = kFull;
        const opxy::i32 target = oread::delay_for(kFull);
        bool smooth = true;
        opxy::i32 previous = before;
        for (unsigned b = 0; b < 2100; ++b) {
            run.blocks(1, zero);
            smooth = smooth && run.state.delay <= previous && previous - run.state.delay <= 64 * 65536 / 2;
            previous = run.state.delay;
            if (b == 10) check("the glide is still under way after 15 ms", run.state.delay > target + (before - target) / 2);
        }
        check("the glide moves less than half a sample per sample", smooth);
        check("the glide arrives within 3 s", std::abs(run.state.delay - target) < 4 * 65536);
    }
    // Outside a send there is no buffer: silence.
    {
        oread::State state;
        oread::initialize(state);
        opxy::i32 l[opxy::block_frames], r[opxy::block_frames], p[12] = {};
        for (unsigned i = 0; i < opxy::block_frames; ++i) l[i] = r[i] = 0x10000000;
        oread::process(state, opxy::Buffer{nullptr, 0}, l, r, p);
        bool quiet = true;
        for (unsigned i = 0; i < opxy::block_frames; ++i) quiet = quiet && !l[i] && !r[i];
        check("no buffer, silence", quiet);
    }
    if (!failures) std::puts("oread: all checks pass");
    return failures ? 1 : 0;
}
