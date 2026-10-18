/* SPDX-License-Identifier: GPL-3.0-only */
/* ORBIT: original four-colour event-tape view for the FM-1. */
#include "orbit_tape.h"
static void orbit_knob(uint32_t k, int32_t delta)
{
    uint32_t len = trk_len(TSEL);
    if (orbit_tape.cursor >= len) orbit_tape.cursor = (uint8_t)(len-1);
    if (orbit_tape.last >= len) orbit_tape.last = (uint8_t)(len-1);
    if (orbit_tape.first > orbit_tape.last) orbit_tape.first = orbit_tape.last;
    if (k == 0) orbit_tape.cursor = (uint8_t)clamp(orbit_tape.cursor+delta, 0, len-1);
    if (k == 1) orbit_tape.first = (uint8_t)clamp(orbit_tape.first+delta, 0, orbit_tape.last);
    if (k == 2) orbit_tape.last = (uint8_t)clamp(orbit_tape.last+delta, orbit_tape.first, len-1);
    if (k == 3) orbit_tape.lift = (uint8_t)clamp(orbit_tape.lift+delta, 0, 1);
    if (k < 4) ui.force = 1;
}
static void orbit_edit(int drop)
{
    orbit_knob(4, 0); /* normalise after track or pattern length changes */
    if (drop) {
        if (orbit_drop(song.sel, orbit_tape.cursor)) ui_message("EVENTS DROPPED");
        else ui_message("EMPTY / WRONG TYPE");
    } else {
        if (orbit_tape.lift && !is_drum(TSEL)) undo_mark(TSEL, (undo_sess += 4u) | 3u);
        orbit_capture(song.sel, orbit_tape.first, orbit_tape.last, orbit_tape.lift);
        ui_message(orbit_tape.lift ? "LIFTED" : "COPIED");
    }
    sync_reload = 1;
    ui.pl_t = 0;
    ui.force = 1;
}
/* Circular graphics use the existing fixed-point sine table, without a framebuffer. */
static void orbit_ring(int32_t cx,int32_t cy,int32_t r,uint16_t color)
{
#if CV_AA
    cv_ring_q4(cx*16+8,cy*16+8,r*16,20,color);
#else
    uint32_t a;
    for(a=0;a<1024;a+=16) {
        uint32_t next=(a+16)&1023u;
        cv_line(cx+((SINE[(a+256)&1023u]*r)>>15),cy+((SINE[a]*r)>>15),
                cx+((SINE[(next+256)&1023u]*r)>>15),cy+((SINE[next]*r)>>15),color);
    }
#endif
}
/* a reel: its flange in the track colour, the tape wound on it (pack: its radius, the more tape the
 * bigger), three spokes turned to phase, the hub */
static void orbit_reel(int32_t cx,int32_t cy,int32_t r,int32_t pack,uint32_t phase,uint16_t color)
{
    uint32_t j;
    orbit_ring(cx,cy,r,color);
    te_disc(cx,cy,pack,TE_G1); orbit_ring(cx,cy,pack,TE_G2);
    orbit_ring(cx,cy,8,color);
    for(j=0;j<3;j++) {
        uint32_t a=(phase+j*341u)&1023u;
        cv_line(cx,cy,cx+((SINE[(a+256)&1023u]*(r-6))>>15),cy+((SINE[a]*(r-6))>>15),color);
    }
    te_disc(cx,cy,3,C_WHITE);
}
static void graph_browse(void);   /* ui_draw.c: the seven-row preset list */
/* TAPE (HOME): the reels are the transport. The tape runs from the left reel to the right one through
 * the selected track's loop (stopped: to HEAD, so KNOB 1 winds it), the spokes turn a step at a time,
 * the head glows red while recording and the tape snaps tight on each bar's first beat. Under them the
 * four tracks as lanes: IN..OUT shaded, HEAD marked, the play heads. The knobs: HEAD IN OUT, COPY/LIFT;
 * SEL held shows what OCT- / OCT+ do here (ui_layers.c). */
static void orbit_screen_draw(void)
{
    static uint32_t head,cache,dials;
    uint32_t i,j,total,pl=pl_shown() ? preset_pos(&total)+1u : 0u,len=trk_len(TSEL);
    uint32_t pos=(song.playing ? TSEL->seq_idx : orbit_tape.cursor)%(len?len:1u), rec=song.rec!=0u;
    uint32_t snap=song.playing && clk_beat%4u==0u && clk_pos<BEAT_U/4u, sig;
    char b[16];
    orbit_knob(4,0);
    b[0]=0;
    if(pl) {
        fmt_int(b,(int32_t)pl); str_cpy(b+str_len(b),"/",sizeof b-str_len(b));
        fmt_int(b+str_len(b),(int32_t)total);
    }
    ui_head("TAPE",C_WHITE,b,HEAD_BPM|(pl?HEAD_NOMSG:0u),&head);   /* (the list names the sound: no message) */
    sig=pl*7919u+orbit_tape.revision+song.sel*17u+rec*97u+song.playing+snap*5u+pos*131u;
    for(i=0;i<NTRK;i++) {
        sig=sig*31u+trk[i].seq_idx+trk[i].p[P_MUTE]+trk[i].p[P_SLEN]*71u;
        for(j=0;j<NSTEP;j++) sig=sig*31u+trk_step_on(&trk[i],j);
    }
    sig=sig*31u+orbit_tape.cursor+orbit_tape.first*67u+orbit_tape.last*4099u;
    if(ui.force || cache!=sig) {
        cache=sig;
        if(pl) {
            /* PRESETS turned: the sounds around the one just loaded replace reels and lanes for a moment */
            lcd_fill(0,24,240,8,C_BLACK);
            cv_begin(240,124,C_BLACK); cv_oy=0; graph_browse(); cv_blit(0,32);
            lcd_fill(0,156,240,28,C_BLACK);
        } else {
            int32_t f=(int32_t)(pos*1000u/(len?len:1u)), pl_l=13+(1000-f)*17/1000, pl_r=13+f*17/1000, hy=snap?84:87;
            uint32_t phase=(pos*89u)&1023u;
            cv_begin(240,100,C_BLACK);
            orbit_reel(56,46,35,pl_l,phase,TE_COL[0]); orbit_reel(184,46,35,pl_r,phase,TE_COL[1]);
            /* the tape: off the bottom of each pack, round the head */
            cv_line(56,46+pl_l,107,hy,TE_G4); cv_line(107,hy,133,hy,TE_G4); cv_line(133,hy,184,46+pl_r,TE_G4);
            te_box(110,hy-14,20,16,rec?TE_RED:TE_G2,1);
            cv_rect(118,hy-14,4,16,rec?C_WHITE:TE_COL[2]);
            cv_blit(0,24);
            cv_begin(240,60,C_BLACK);
            for(i=0;i<NTRK;i++) {
                int32_t y=(int32_t)i*15;
                if(i==song.sel) cv_rect(0,y,240,14,TE_G1);
                fmt_int(b,(int32_t)i+1); cv_text(6,y-1,&FONT_S,b,TE_COL[i]);
                for(j=0;j<NSTEP;j++) {
                    int32_t x=30+(int32_t)j*3;
                    if(i==song.sel && j>=orbit_tape.first && j<=orbit_tape.last) cv_rect(x,y+1,3,12,TE_DIM[i]);
                    if(j<trk_len(&trk[i])) cv_rect(x,y+6,1,2,TE_G2);
                    if(j<trk_len(&trk[i]) && trk_step_on(&trk[i],j)) cv_rect(x,y+3,3,8,trk[i].p[P_MUTE]?TE_DIM[i]:TE_COL[i]);
                    if(song.playing && j==trk[i].seq_idx) cv_line(x,y+1,x,y+12,C_WHITE);
                    if(i==song.sel && j==orbit_tape.cursor) cv_rect(x,y+11,3,2,TE_COL[2]);
                }
            }
            cv_blit(0,124);
        }
    }
    {   /* the knobs (the lanes show where HEAD, IN and OUT are: no dials) */
        static const char *const lab[4]={"HEAD","IN","OUT","EDIT"};
        static char v[3][6];
        const char *val[4]={v[0],v[1],v[2],orbit_tape.lift?"LIFT":"COPY"};
        static const int32_t ratio[4]={NO_DIAL,NO_DIAL,NO_DIAL,NO_DIAL};
        fmt_int(v[0],orbit_tape.cursor+1); fmt_int(v[1],orbit_tape.first+1); fmt_int(v[2],orbit_tape.last+1);
        te_dials(184,lab,val,ratio,orbit_tape.lift,&dials);
    }
}
