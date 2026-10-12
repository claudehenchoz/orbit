# CLUSTER in ORBIT 0.7.0

CLUSTER connects a C translation of the original OP-1 #246 cluster oscillator
to ORBIT. It is separate from the independently written SWARM, PULSE, FM4
and CLOUD engines. The sixteen factory rows retain their original names and
signed16 raw knobs. Pitch, amplitude envelope, velocity, mixer and effects
are ORBIT's: this is not a complete original-patch or hardware reproduction.
[Asset provenance and rights](../assets/op1-cluster/README.md).

## Play it as a part, not an eight-voice guarantee

ORBIT has **three independently voiced synth parts and one drum track**.
The synths share **eight sounding voices globally**, not eight per part.
The CLUSTER engine's POLY/UNISON cap is **four voices per part**. MONO and
LEGATO intentionally play one voice; UNISON allocates multiple voices to
one note. All sixteen CLUSTER factory presets select POLY.

Other parts, release tails and new-note allocation compete for the global
budget. The audio overload path can fade extra voices when CPU load is high.
Four CLUSTER voices therefore depend on available allocation and CPU time;
the cap is not a promise of four voices under every FM-1 workload. A thinner
chord may reflect voice mode, voice stealing or overload shedding rather
than a one-voice engine implementation.

Default ID **15** remains unchanged; optional SLICE shifts CLUSTER to 16.
CLOUD follows it in the registry. Existing factory order and preset IDs are
preserved. ORBIT 0.7.0 writes ORP7/OPB2 and retains legacy ORBIT import; it
does not retain FUN5 as its write format or accept raw SLOOP FUN6. Editor
protocol **12** preserves signed16 values and sound macro maps.

## Four raw controls

| Control | Descriptor range |
| --- | --- |
| COUNT | 2048..18431 |
| WAVE | -32768..32767 |
| CURVE | 0..24575 |
| SPREAD | -32768..32767 |

COUNT and CURVE stay within the original table indices. The other four
engine slots are reserved, not additional active synthesis controls.
These descriptors are raw values, not a claim that every signed16 value has
equal perceptual spacing. Absolute seven-bit MIDI CC can cover a range but
not every individual value; panel/editor editing is finer.

## Core, adapter and optimization

The original kernel renders **128 samples** at a time. ORBIT uses **32-sample**
control blocks, so each voice caches 128 samples. Pitch/knob updates reach
the core on the next refill boundary while ORBIT amplitude ramps apply at
each control block. Synchronized chord voices can refill together; average
host cost alone cannot establish the peak target ISR cost.

State/cache use the existing per-part engine arena shared with GRAIN, FM6,
PHYS and CLOUD. The static size assertions keep CLUSTER within that arena.
Tables are const flash data, not copied into a new full-size RAM table.
Each part has independent state and a seed-1 PRNG initialized on arena
ownership; voices within that part share its PRNG/call order. This is not
the original OP-1's whole-system call order.

The 0.7.0 arithmetic path replaces the old generalized fractional multiply
with bounded integer operations preserving **ties-to-even**, saturation and
signed-bit interpretation. The existing phase/control multiplication remains specialized:
the control's low half is zero, so zero cross terms are removed while signed
32-bit saturating extraction is retained. Lookup, phase wrap, PCM/state and
R0 semantics remain the core contract. This is a bit-identical core
optimization, not fewer oscillator lanes or a change to the factory data.

The port's look-ahead master limiter separately changes output headroom.
It is outside the recovered oscillator and must not be described as a
bit-identical full ORBIT audio path. ORBIT ADSR/release, pitch bend, LFO and
FX remain ORBIT adaptations; FLT/SHP are not arbitrarily mapped onto unknown
original parameters.

## Current host evidence

The lead's integrated health probe evaluated **624 conditions**: 576 full-mix
and 48 isolated-track cases. Every tested POLY full-mix case retained its
requested one, two or four voices. Full-mix PCM and raw oscillator endpoint
clipping counts were zero. With four notes at velocity 100, LEVEL 104,
full master and default FX, the sixteen presets had PCM peak 17994 and RMS
5577.143..7557.125 in that probe. These are specified host conditions, not a
hardware loudness or subjective-quality judgment.

Focused mixed-engine tests cover concurrent CLOUD/GRAIN/CLUSTER state
isolation and release. The lead also records a passing divide-by-zero
sanitizer sweep including cluster_math_test. The original asset record
contains the finite strict core oracle scope. Final integrated CPU/static
budgets and package measurements belong in [VALIDATION.md](VALIDATION.md).
This document does not substitute old measurements for the 0.7.0 package.

The reported physical one-voice/crackle symptom was **not reproduced** by
these host renders. Hardware ISR deadlines, DMA underruns, worst-case
shedding and subjective listening remain unverified. Do not read the
optimization or limiter changes as proof that physical crackle is fixed.

## Historical 0.6.0 work

On 2026-10-10 the original product core/const-table verification compared
729 cases, 5103 blocks/R0 words, 653184 samples and serialized state against
the strict oracle. This is historical finite-matrix evidence, not full
silicon equivalence or a new 0.7.0 execution claim. Reproduction instructions
and bounds remain in the [asset record](../assets/op1-cluster/README.md).

The 0.6.0 release also replaced the FLUTE/SCRCH recordings with small
build-time synthesized harmonic flute loops and noise/sweep scratch
one-shots, stored as ADPCM through SAMPLE/GRAIN. Existing zones, roots,
factory/USR IDs remain. They are different source sounds, not a new
real-time flute engine. That change recovered flash, not general runtime
CPU or RAM.

A physical FM-1 installation of **0.6.0 on 2026-10-10** confirmed reboot,
INFO/PING, sixteen raw-preset transfers, fourteen preserved backup objects
and a short non-silent 48 kHz USB recording without full-scale clipping.
It did not verify worst-case audio timing, subjective quality, long-term
stability or power-loss recovery, and is not evidence of a 0.7.0 hardware
install. See [the 0.6.0 notes](releases/0.6.0.md) and
[validation record](VALIDATION.md).

## Rights

The translated core/adapter code retains GPL-3.0-only SPDX notices. The
extracted official OP-1 tables, waveforms and factory JSON remain **Teenage
Engineering assets**, not CC0 or newly GPL-licensed data. Extraction alone
does not establish permission for separate distribution; the Felucca asset
permission is not claimed to cover them. Original URLs, immutable hashes,
addresses and full-patch JSON remain in
[assets/op1-cluster](../assets/op1-cluster/README.md). See
[LICENSING.md](../LICENSING.md) for SLOOP/Felucca and other asset notices.
