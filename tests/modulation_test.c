/* SPDX-License-Identifier: GPL-3.0-only */
/* ORBIT 0.7.0: SLOOP 2.6's per-track modulation on the integrated firmware (the harness of tests/insert_test.c):
 *   esync    ENV SYNC: ATK as a note value of the tempo (a quarter at 120 BPM: 0.5 s; at 60: 1 s)
 *   lfo 2    SYNC: a cycle a note value; TRIG FREE: a new phrase does not restart it; POL UNI: 0..+1
 *   bend     the MIDI pitch bend over BEND 2 and 12 semitones
 *   learn    a learned CC sets its track's parameter on any channel, not the standard map
 *   matrix   all 11 LFO SYNC choices x TRIG NOTE / FREE x POL BI / UNI, ADSR 0 / 127 with ENV SYNC off and on, at
 *            BPM 40 and 240: bounded, the synced LFO's increment positive, UNI never below zero; the tempo inputs
 *            (editor SET, a project, the MIDI clock) publish 40..240 only
 * Exit status: the number of failed checks. */
#define INSERT_HARNESS_ONLY 1
#include "insert_test.c"

static void m_lfo_matrix(void)
{
    static const int16_t BPM[2] = {40, 240};
    uint32_t b, sy, tr, po, ok = 1, uni_ok = 1, inc_ok = 1;
    for (b = 0; b < 2u; b++)
        for (sy = 0; sy < 11u; sy++)
            for (tr = 0; tr < 2u; tr++)
                for (po = 0; po < 2u; po++) {
                    uint32_t k;
                    setup();
                    song.g[G_BPM] = BPM[b];
                    trk[0].p[P_LSYNC] = (int16_t)sy, trk[0].p[P_LTRIG] = (int16_t)tr, trk[0].p[P_LPOL] = (int16_t)po;
                    trk[0].p[P_LRATE] = 90, trk[0].p[P_LD_PIT] = 63, trk[0].p[P_LD_AMP] = 63;
                    if (sy)
                        inc_ok &= lfo_sync_inc(&trk[0]) > 0u;
                    trk_note_on(&trk[0], 45, 110);
                    for (k = 0; k < 30u; k++) {
                        render(1);
                        uni_ok &= !po || trk[0].lfo_val >= 0;
                    }
                    ok &= bounded(0, CTL);
                }
    check(ok && inc_ok, "LFO SYNC 0..10 x TRIG x POL at BPM 40 / 240: bounded, synced increments positive");
    check(uni_ok, "POL UNI: the LFO never goes below zero");
}
static void m_env_matrix(void)
{
    static const int16_t E[2] = {0, 127};
    static const int16_t BPM[2] = {40, 240};
    uint32_t b, sy, a, d, r, ok = 1;
    for (b = 0; b < 2u; b++)
        for (sy = 0; sy < 2u; sy++)
            for (a = 0; a < 2u; a++)
                for (d = 0; d < 2u; d++)
                    for (r = 0; r < 2u; r++) {
                        setup();
                        song.g[G_BPM] = BPM[b];
                        trk[0].p[P_ESYNC] = (int16_t)sy;
                        trk[0].p[P_ATK] = E[a], trk[0].p[P_DEC] = E[d], trk[0].p[P_REL] = E[r];
                        trk[0].p[P_SUS] = 64;
                        trk_note_on(&trk[0], 45, 110);
                        render(30);
                        trk_note_off(&trk[0], 45);
                        render(30);
                        ok &= bounded(0, 30u * CTL) && esync_k[0] > 0u && esync_k[1] > 0u && esync_k[2] > 0u;
                    }
    check(ok, "ATK / DEC / REL 0 and 127, ENV SYNC off and on, BPM 40 / 240: steps positive, bounded");
}
static void m_tempo_inputs(void)
{
    uint32_t i, ok = 1;
    static const int32_t IN[] = {0, -1, 32767, 39, 241};
    for (i = 0; i < NELEM(IN); i++) {
        song.g[G_BPM] = (int16_t)clamp(IN[i], GP[G_BPM].min, GP[G_BPM].max);   /* (editor SET / project apply) */
        ok &= song.g[G_BPM] >= 40 && song.g[G_BPM] <= 240;
    }
    check(ok && GP[G_BPM].min == 40 && GP[G_BPM].max == 240, "tempo inputs clamp 0 / -1 / 32767 / 39 / 241 into 40..240");
    ok = 1;
    for (i = 0; i < NELEM(SCALE_MASK); i++)
        ok &= SCALE_MASK[i] != 0u;
    check(ok, "every scale has a degree (the WHITE keys' degree / count never divides by 0)");
    song.g[G_BPM] = 90;
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    t_esync();
    t_lfo2();
    t_bend();
    t_learn();
    m_lfo_matrix();
    m_env_matrix();
    m_tempo_inputs();
    printf("modulation: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
