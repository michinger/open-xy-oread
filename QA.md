# Testing Oread on an OP-XY

Checks to run on a device with a firmware that includes Oread, from top to bottom, in one project:
each section leaves the project ready for the next. To run only some sections, set up what their first
line says. Host tests (`python ../open-xy/sdk.py test --machine .`) don't replace these: they can't hear
the sound or see the screen.

Report a failure with the section, the line and what you saw. A photo of the screen helps.

**This checklist is an example to rewrite for your machine**: it starts with the checks
every effect needs. Adapt them as the machine grows: name its controls where a line says "each control",
remove lines that can't apply, and add the machine's own sections before
[Saving and loading](#saving-and-loading). Note the SDK commit and firmware each full pass used.

AI agents working on the machine's code keep this file up to date as they go: each change that adds,
removes or alters something a person would check on the device updates its lines here.

## Setup

- Build a firmware with the machine ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine))
  and install it.
- Create a new project, **plain**, without Oread: give track 1 a stock sound of its own and save it.
  Later steps load it as a project that never had the machine.
- Create another new project, **qa**, for the pass: don't use a project you care about.
- Track 1: a stock synth, with one note on step 1 lasting a whole bar, so it sounds all the time while
  the sequencer plays. Track 15 (a send): Oread (Shift + track 15's key, then choose it). Turn track 1's
  send to track 15 up.
- Press play, and leave it playing unless a step says otherwise.

## Firmware basics

- [ ] The OP-XY boots, and stock engines, effects, the sequencer and project save/load work
- [ ] Oread is in the effect list of tracks 15 and 16 (Shift + their track keys), under its name, and in
      no other track's list

## Its page and controls

- [ ] Each control through its whole range, both directions: the sound follows, with no clicks, jumps or
      silence at the ends; the value shown reads in its unit (Hz, ms, %, a name)
- [ ] Choice controls move one choice per detent, both directions, and wrap or stop as the page says
- [ ] Fast turns: the value keeps up, no flicker or slowdown
- [ ] Controls with an alternate (an encoder press, a second page): pressing switches, the page shows
      which is active, and each keeps its own value
- [ ] The page while playing and while stopped, and after leaving it and coming back: nothing stale,
      missing or drawn twice
- [ ] With track 1's send at 0 the effect is silent; raising it brings the effect in smoothly
- [ ] Page 1 shows mode, rate, feedback, echo vol; a press of any encoder shows page 2 (rv vol, bass, treble,
      wear) on all four at once, and another press returns. Each control keeps its value across pages
- [ ] Mode steps one choice per detent through 1, 2, 3, 2+3, 1+R, 2+R, 3+R, 1+2+R, 2+3+R, 1+3+R, 1+2+3+R, R
- [ ] Track 1's own sound stays at its level as the send rises (Oread has no Dry control)
- [ ] Stop the sequencer while a note rings: the effect's tail ends naturally, with no click
- [ ] The same effect on track 16, at the same time as on track 15: each keeps its own values

## Parameter locks

- [ ] Lock each control on a step (hold the step, turn the knob): it plays on that step, and the page
      shows the lock while the step is held
- [ ] Several locks on one step, and locks on neighbouring steps: each plays as written
- [ ] Clear the locks: the control plays its own value again

## Patterns, scenes and songs

- [ ] Give the track a second pattern with different values: each pattern keeps its own
- [ ] Copy a pattern: the copy holds the same values, and changing one leaves the other alone
- [ ] Switch between the patterns while playing and while stopped: the sound and the page follow at once
- [ ] Make scenes 1 and 2 with the two patterns, and a song of scene 1 then scene 2, looping: each scene
      plays its own values from its first bar, on every pass

Then play scene 1.

## Mute

- [ ] Mute the track (hold aux and press the track's key): it's silent, with no click or tail left behind;
      unmute: it plays as before
- [ ] The same from the mixer
- [ ] Muted in scene 2 only: switching between scenes 1 and 2 mutes and unmutes it

Unmute it everywhere.

## Tape echo and spring

- [ ] Mode 1, 2 and 3 with Feedback at 0 and a short hit on track 1: one repeat each, head 2 about 1.9 and head 3
      about 2.75 times as late as head 1; 2+3 gives two repeats
- [ ] Rate at 0 then 100: repeats go from slow (head 1 about 177 ms) to fast (about 69 ms); turning Rate
      while repeats ring glides their pitch, like a tape motor, with no clicks
- [ ] Feedback from 0 up, in mode 1 and in 1+2+3+R: repeats last longer at the same knob position in both;
      at 40 they fade quickly; only above about 86 does it self-oscillate, getting grittier towards 100, and turning it down lets it die away
- [ ] Self-oscillation at Feedback 100 with Rate swept: never louder than about -9 dBFS on the return (the
      limiter); no crackle when the limiter works
- [ ] Modes with R add a spring reverb (wider than the echo); R alone is reverb with no repeats. Switching
      modes while repeats ring: no clicks, and the spring tail rings out
- [ ] Rv Vol, Bass and Treble: Bass and Treble cut at 0, flat at 50, boost at 100, on the spring's left and
      right alike; Rv Vol sets the spring level, Echo Vol the repeats' level
- [ ] Shift shows Rv Decay, Rv Tone, Rv Chirp, Rv Drive at 0/off in a new project; each changes only the
      spring: longer, brighter, more chirp, driven crash; Rv Drive at 7 stays bounded
- [ ] Wear from 0 to 100: wow and flutter (slow and fast pitch wobble) grow and the repeats darken; at 0 the
      pitch is steady
- [ ] Put Oread on a send for the first time, or reset the engine: it is silent for about 26 ms, then works,
      with no burst of old buffer contents

## Saving and loading

- [ ] Save **qa**, power off and on, load it: the machine is there with every value as you left it, on
      each pattern
- [ ] Change a value, wait a few seconds and power off without saving: the OP-XY starts normally, and
      **qa** loads as last saved
- [ ] Load **plain**: track 1 has its own sound, nothing of **qa** shows. Load **qa** again: all back
- [ ] While playing, load **plain** and then **qa** again: nothing freezes, no stray values
- [ ] Create a new project: put Oread on a track; it starts at its defaults
- [ ] On a firmware without Oread, open **qa**: it loads, and the rest of the project plays

## CPU

- [ ] Follow [hardware profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md):
      a profiling build from committed source, every required workload collected, and the readout
      kept with the commit it measured
- [ ] Run `sdk.py profile report`: inspect both average CPU (`block_percent`) and worst observed block
      CPU (`peak_percent`), and the workload that produced the spike; a heaviest-setting average is not a peak
- [ ] Before submitting, copy the complete generated `cpu` member into the catalog entry; resolve clipped
      or invalid peaks rather than omitting them. Hardware evidence still pending means submission is not ready

## In the builder

Once the machine's catalog entry is merged and released (the beta site from `staging`, open-xy.com from
`main`; see [submitting a machine](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)):

- [ ] The machine list shows Oread as Community, with its name, maintainer and description
- [ ] Filtering by its kind (effects) lists it, and filtering by the other kinds doesn't
- [ ] Searching its name, and each of its keywords, finds it; its details panel lists its keywords, and
      clicking one lists it among that keyword's machines
- [ ] Choosing it builds a firmware that includes it, and that firmware passes
      [Firmware basics](#firmware-basics)

## Known limits

Not failures, but worth confirming they haven't changed. List what the machine can't do yet, and stock
behaviour that looks like a bug but isn't.
