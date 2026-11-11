# Testing Oread on an OP-XY

The checks only Oread can fail, for one pass on a device with a firmware that includes it, from top
to bottom in one project (about 10 minutes). How the OP-XY sequences, locks and saves a send effect's
values is the same for every effect and isn't repeated here. Host tests
(`python ../open-xy/sdk.py test --machine .`) can't hear the sound, so they don't replace this.

Report a failure with the section, the line and what you heard or saw. Note each full pass in
[Passes](#passes). Keep this file in step with the effect: a change a person would notice on the
device changes its line here.

## Setup

- Create a new project, **qa** (not one you care about).
- Track 1: a stock synth playing one short note on step 1, so each bar has a single hit to hear the
  repeats of.
- FX I: Oread (Shift + the FX I track key, then choose it). Turn track 1's send to FX I up.
- Press play.

## Choosing it

- [ ] Oread is in the effect list of FX I and of FX II, and sounds on each
- [ ] Choosing it is silent for a moment (about 26 ms), then it works, with no burst of noise
- [ ] A new project with Oread starts at its defaults: Mode 1+R, Feedback around 40, Echo Vol 70

## Controls

- [ ] Page 1 shows Mode, Rate, Feedback, Echo Vol; pressing an encoder shows Rv Vol, Bass, Treble, Wear
      on all four; another press goes back, and every value is kept
- [ ] Holding Shift shows Rv Decay, Rv Tone, Rv Chirp, Rv Drive, at 0 / off in a new project

## Echo

- [ ] Mode steps through 1, 2, 3, 2+3, 1+R, 2+R, 3+R, 1+2+R, 2+3+R, 1+3+R, 1+2+3+R, R; switching while
      repeats ring makes no click; R is reverb only, with no repeats
- [ ] Feedback at 0, modes 1, 2 and 3: one repeat each, head 2 about 1.9 and head 3 about 2.75 times as
      late as head 1
- [ ] Rate 0: head 1 about 177 ms; Rate 100: about 69 ms. Turning Rate while repeats ring glides their
      pitch like a tape motor, with no clicks
- [ ] Feedback around 40: the repeats fade quickly. Up to about 85: longer and longer tails, darker each
      time, and about the same at the same position in mode 1 and in 1+2+3+R
- [ ] Feedback above about 86: it runs away and gets grittier towards 100; turning it down lets it die away
- [ ] Feedback at 100 with Rate swept: loud but capped (the limiter), never louder, no crackle
- [ ] Echo Vol changes only the repeats' level
- [ ] Wear at 0: steady pitch; turning it up adds wow and flutter and darkens the repeats

## Spring

- [ ] In a mode with R, Rv Vol changes the spring's level; the spring sounds wider than the echo
- [ ] Bass and Treble: flat at 50, cut at 0, boost at 100, on the echo and the spring, left and right
- [ ] Rv Decay: a longer tail. Rv Tone: brighter. Rv Chirp: more "boing". Rv Drive: a crunchier
      spring, still bounded at 7

## Saving

- [ ] Change every control and the Shift row, save **qa**, switch the OP-XY off and on, open **qa**:
      all of Oread's values are back

## Load

- [ ] In a busy project (all tracks playing), with Feedback high and Rv Drive up: no dropouts or
      crackles. If there are, note the project and the settings

## Known limits

Not failures:

- About 26 ms of silence the first time Oread starts on a send, while its buffer is cleared.
- CPU at its defaults, measured on the device for v1.0.0: 4.47 % average, 6.95 % worst block, above the
  SDK's recommended 3.5 %.
- Above about 86 % Feedback it's loud: a runaway sits at the limiter's ceiling all the time.

## Passes

| Date | Oread commit | Firmware | SDK commit | Result |
| --- | --- | --- | --- | --- |
| | | | | |
