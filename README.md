# Oread

A send effect for the OP-XY (Open-XY machine ID `oread`): a three-head tape
echo with a spring reverb, in the style of the classic 1970s multi-head tape
echoes (the Roland RE-201 Space Echo is the reference instrument; Oread is not
affiliated with or endorsed by Roland). Machine code and tests live at this
repository's root.

## Sound

- **Tape loop:** one record head and three playback heads at 1 : 2 : 3
  spacing (an approximation of the original's head spacing). Head 1 is 190 ms
  at Rate 0 and 55 ms at Rate 100; heads 2 and 3 are two and three times later.
  Rate is tape speed: a change glides (about 0.37 s), bending the repeats'
  pitch as a capstan motor would.
- **Mode** (12 choices, as on the original's selector): `1`, `2`, `3`, `2+3`
  echo only; `1+R` ... `1+2+3+R` echo with the spring; `R` spring only.
  Several heads are summed at 0.7 or 0.58 each so the level stays even.
- **Intensity** is the record level of the repeats: loop gain up to 1.2, so the
  echo self-oscillates above about 83 %. The record head's cubic soft clip
  bounds it; tape samples never exceed half scale.
- **Tape character:** each playback passes a one-pole low-pass (5.6 kHz at
  Wear 0, 2 kHz at Wear 100) and an 85 Hz low cut, so repeats darken and thin;
  Wear adds wow (0.8 Hz), drift (0.13 Hz) and flutter (7 Hz), up to 0.4 % speed
  deviation.
- **Spring:** four stretched first-order allpasses (dispersion, the chirp) into
  a 42 ms damped feedback line; the right side reads a second point of the
  line, so the reverb is wide while the echo stays centred.
- **Bass / Treble:** shelves near 250 Hz and 3 kHz on the return, from cut
  (0) through flat (50) to +6 dB (100).

Fixed point Q31 throughout, 64-sample blocks at 44.1 kHz. The tape and spring
live in the send slot's declared buffer (139,776 bytes), cleared over the
first five blocks after the effect starts (about 7 ms of silence). Oread
returns only echo and reverb; the source's dry level is left to the stock mixer.

## Controls

Eight parameters on the stock send page, two pages (`"press": "page"`): a press
of any encoder switches all four positions at once.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | Mode | Reverb |
| 2 | Rate | Bass |
| 3 | Intens(ity) | Treble |
| 4 | Echo (level) | Wear |

## Sources and licences

Oread's DSP is written from scratch for the Open-XY fixed-point API; no code
was copied from other projects. These shaped it:

- Välimäki, Parker and Abel, "Parametric spring reverberation effect", JAES
  58(7/8), 2010: spring dispersion with stretched (z^-k) allpass chains.
  Published research; the structure is implemented from the paper.
- Mutable Instruments eurorack firmware (MIT): fixed-point one-pole and
  allpass/delay-line idioms, as in Clouds' and Rings' reverbs.
  https://github.com/pichenettes/eurorack
- Airwindows (MIT), e.g. TapeDelay and ToTape: reference for tape-loop
  darkening and flutter character. https://github.com/airwindows/airwindows
- ChowDSP ChowTape and Jatin Chowdhury's tape and spring-reverb writing
  (GPLv3 for the plugins): read as reference only; no code used, since GPL
  code cannot enter this MIT repository.
- Roland RE-201 owner's manual and published measurements: the 12-position
  mode selector and the head layout that the controls follow.

## Develop

Keep the [SDK](https://github.com/etcetc000/open-xy) beside this repository as
`../open-xy`, on `staging`; adjust the commands if yours is elsewhere. The
[first-machine guide](https://github.com/etcetc000/open-xy/blob/staging/docs/first-machine.md)
covers prerequisites. Read the
[core machines](https://github.com/etcetc000/open-xy-core-machines) as worked
examples and the [machine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md)
for the supported interfaces. Follow the
[UX conventions](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md);
Macro Osc is the two-page reference. Synth starters with five to eight controls
use the [shared header](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md#two-page-header):
keep its SVG block and update the labels/value captions with your controls.
Columns without alternates repeat the same saved parameter on both rows.
Agent instructions are in [AGENTS.md](AGENTS.md).

Effects have no browser preview. Run the host tests below, then audition on your XY
using the hardware workflow linked below.

For synth previews, the workbench's **Graphics** selector compares Colorful and
Grayscale with the same sound and animation code. To control grayscale yourself,
declare SDK 1.5 and `"graphics": {"grayscale": "panel.grayscale.svg.fragment"}`
in machine.json and supply that SVG fragment. Follow the SDK's
[grayscale artwork contract](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md#grayscale-artwork).
Already-grayscale originals remain unchanged; without a variant, the installer
converts colors automatically. Effects retain their stock screens.

```sh
python ../open-xy/sdk.py test --machine . --cxx clang++ --host-cxx g++
```

Extend `tests/` as the machine's behavior grows. The starter is a minimal
implementation, not a finished instrument. Keep generated output outside this
repository. The generated workflow checks source, sound tests and synth previews
on Linux, and compiles the native module on Windows; it never uses a device.
Its `OPEN_XY_REF` starts at `staging`; pin a compatible SDK commit for a release.

## Hardware and publication

Check the machine on the device with [QA.md](QA.md): one pass, top to bottom,
in one project. Host tests can't hear the sound or see the screen. Keep it in
step with the machine: its controls, and sections for its own behavior.

Follow the SDK's [firmware build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md)
and [hardware profiling workflow](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md).
Keep firmware and stock content outside every checkout. Commit the machine,
build with profiling enabled, install when authorized and collect a run
(see its "Collect a run" section). Run `sdk.py profile report` and inspect both
average CPU (`block_percent`) and worst observed block CPU (`peak_percent`).
The peak is the largest measured block across all settings and captures, never
an average of peaks. A clipped peak cannot be published as an exact measurement.
Aim for at most 3.5% of the block budget for both average and peak CPU;
exceeding either recommendation warns but does not reject the submission.
The host estimate printed by `test` cannot substitute for those measurements.

For the builder, follow [catalog submission](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine).
Every new or changed draft/published entry needs a hardware CPU record tied to
its pinned commit, including average and peak CPU. Copy the complete `cpu` member
from the generated `catalog-cpu.json` into the catalog entry before submitting.
Existing captures can be reprocessed offline to add the peak; no new device run
is needed if the exact source and measured package still match.
Hardware validation may be performed by a maintainer if you
do not own an XY. Publication remains pending until that evidence is available.

The generated starter is covered by [LICENSE](LICENSE). Preserve notices for
code you reuse and include the licences of any dependencies or ports.
