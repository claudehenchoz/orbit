/* SPDX-License-Identifier: GPL-3.0-only */
/* Graphic-first sound pages. Included after ui_draw.c graph helpers. */
/* 그래픽 우선 사운드 페이지: T1 EDIT(엔진 1/2) · T2 ENV · T3 FX(첫 페이지) · T4 LFO. VOICE · FILTER · SLICER 등 나머지
 * 서브페이지는 기존 열 레이아웃으로 그린다(같은 키를 다시 누르면 family의 다음 페이지) */
static int orbit_sound_page(void)
{
    const page_t *pg=cur_page();
    return !is_drum(TSEL) && (pg->scope==SC_ENGINE || pg->fam==FAM_ENV || pg->fam==FAM_LFO || pg->graph==GR_FX);
}
/* 현재 페이지가 속한 오리지널 OP-1 모듈: 0 T1 engine(EDIT), 1 T2 envelope(ENV), 2 T3 effect(FX), 3 T4 LFO(LFO) */
static uint32_t orbit_module_of(const page_t *pg)
{
    return pg->scope==SC_ENGINE ? 0u : pg->fam==FAM_ENV ? 1u : pg->fam==FAM_FX ? 2u : 3u;
}
/* the sound pages (EDIT, ENV, LFO, FX): the header names the page; the body is the picture of the sound
 * (the envelope, the LFO, the sends, the engine's rings) or, where an engine has no picture of its own,
 * the sound itself, live (the output scope: you see what the knobs do); the knob strip under it, with a
 * dial only where the body does not already draw the value and the value is an amount (not an enum) */
static void orbit_sound_draw(void)
{
    static uint32_t head,cache,dials;
    const page_t *pg=cur_page();
    const engine_t *engine=ENGINES[TSEL->eng_req%NENGINES];
    int lfo_off = !TSEL->p[P_LD_PIT] && !TSEL->p[P_LD_FLT] && !TSEL->p[P_LD_SHP] && !TSEL->p[P_LD_AMP];
    uint32_t eng=TSEL->eng_req, rings=pg->scope==SC_ENGINE && (eng==1 || eng==2 || eng==6);
    uint32_t scope=!rings && (pg->scope==SC_ENGINE || (pg->graph==GR_NONE && pg->fam==FAM_ENV));
    const param_desc_t *desc[4];
    char value[4][16], label[4][12];   /* (te_dials reads them now: the stack) */
    const char *lab[4]={label[0],label[1],label[2],label[3]}, *val[4]={value[0],value[1],value[2],value[3]};
    char pos[8];
    int32_t ratio[4], r[4]; uint32_t k,sig=ui.page+eng*71u+song.sel*65537u;
    for(k=0;k<4;k++) {
        int16_t *vp; const char *unit;
        desc[k]=page_desc(pg,k,&vp); ratio[k]=NO_DIAL; r[k]=0; value[k][0]=label[k][0]=0;
        if(desc[k] && desc[k]->label && desc[k]->label[0]!='-') {
            uint32_t drawn=pg->graph==GR_ADSR || pg->graph==GR_FX || rings || (pg->graph==GR_LFO && k>=2u);
            str_cpy(label[k],desc[k]->label,sizeof label[k]);
            param_format(desc[k],*vp,value[k],&unit);
            r[k]=clamp(RATIO(desc[k],*vp),0,1000);  /* (the pictures read it, dial or not) */
            if(!drawn && desc[k]->fmt!=F_ENUM) ratio[k]=r[k];
            sig=sig*31u+(uint32_t)*vp;
        }
    }
    page_pos(pos);
    ui_head(pg->scope==SC_ENGINE ? engine->name : pg->title,C_WHITE,
            pg->graph==GR_LFO && lfo_off ? "NO DEPTH" : pos,0,&head);
    if(scope) sig=sig*31u+scope_w;                  /* (live: every frame) */
    if(pg->fam==FAM_LFO) sig=sig*31u+(uint32_t)lfo_off;
    if(ui.force || sig!=cache) {
        cache=sig;
        if(ui.force) { lcd_fill(0,24,240,18,C_BLACK); lcd_fill(0,166,240,18,C_BLACK); }
        cv_begin(240,124,C_BLACK);
        if(pg->graph==GR_ADSR) {
            int32_t a=16+r[0]*45/1000,d=a+16+r[1]*45/1000,y=108-r[2]*86/1000;
            cv_line(12,110,a,12,TE_COL[0]); cv_line(a,12,d,y,TE_COL[1]);
            cv_line(d,y,180,y,TE_COL[2]); cv_line(180,y,200+r[3]*28/1000,110,TE_COL[3]);
            te_disc(a,12,4,TE_COL[0]); te_disc(d,y,4,TE_COL[1]); te_disc(180,y,4,TE_COL[2]);
        } else if(pg->fam==FAM_LFO) {
            graph_lfo(TSEL,TE_COL[0]);
            orbit_ring(192,34,17+r[2]*12/1000,TE_COL[2]);
            orbit_ring(192,34,8+r[3]*8/1000,TE_COL[3]);
        } else if(rings) {
            /* Parameter-driven resonator/orbit diagram; not an audio measurement. */
            for(k=0;k<4;k++) {
                int32_t cx=68+(int32_t)k*34,cy=60+(k%2 ? 10 : -10),rr=18+r[k]*22/1000;
                uint32_t angle=(uint32_t)r[k]*1023/1000;
                orbit_ring(cx,cy,rr,TE_COL[k]);
                cv_line(cx,cy,cx+((SINE[(angle+256)&1023]*rr)>>15),cy+((SINE[angle]*rr)>>15),TE_COL[k]);
                te_disc(cx,cy,3,C_WHITE);
            }
        } else if(pg->graph==GR_FX) {
            /* T3 effect: 네 send(DST CHO DLY REV)를 encoder 색의 얇은 세로 게이지로 그린다. 파라미터 값이며 오디오 측정이 아니다 */
            for(k=0;k<4;k++) {
                int32_t gx=30+(int32_t)k*60,h=r[k]*88/1000;
                cv_rect(gx,16,1,92,TE_G2);
                if(h) cv_rect(gx,108-h,1,h,TE_COL[k]);
                cv_rect(gx-8,108-h,17,1,TE_COL[k]);
            }
            cv_line(12,110,228,110,TE_G2);
        } else
            graph_scope(TE_COL[song.sel&3u],62,52);  /* the sound, live */
        cv_blit(0,42);
    }
    te_dials(184,lab,val,ratio,sig,&dials);
}

/* MIXER (GLO on HOME), as the OP-1's: the four faders are the levels (PAN: the markers are the pans), so
 * the knob strip names the tracks and gives the numbers, no dials. SELECT: LEVEL -> PAN -> TRACK, the
 * selected track's own swing / level / steps / pan (its name and its pattern above them) */
static void orbit_mixer_draw(void)
{
    static uint32_t head,cache,footer;
    uint32_t k,kind=mixer_kind(),pan=kind==MX_PAN,sig=song.sel+song.solo*31u+kind*7u;
    char b[16];
    for(k=0;k<NTRK;k++) sig=sig*31u+trk[k].p[P_LEVEL]+trk[k].p[P_MUTE]*521u+trk[k].p[P_PAN]*17u+trk[k].eng_req*97u;
    sig=sig*31u+song.g[G_DRLVL];
    if(kind==MX_TRACK) for(k=0;k<NSTEP;k++) sig=sig*3u+(uint32_t)trk_step_on(TSEL,k);
    if(pl_shown()) { uint32_t total; sig=sig*31u+preset_pos(&total)+1u+TSEL->user*257u+up_gen*7919u; }
    ui_head(kind==MX_TRACK?"TRACK":pan?"PAN":"MIXER",C_WHITE,song.rec?"NO AUTO":0,0,&head);
    if(ui.force || sig!=cache) {
        cache=sig;
        if(ui.force) { lcd_fill(0,24,240,12,C_BLACK); lcd_fill(0,160,240,24,C_BLACK); }
        cv_begin(240,124,C_BLACK);
        if(pl_shown()) { cv_oy=0; graph_browse(); }   /* PRESETS turned: the sounds around the one just loaded */
        else if(kind==MX_TRACK) {                   /* the selected track: its name, its pattern */
            uint32_t len=(uint32_t)clamp(TSEL->p[P_SLEN],1,64),j;
            if(is_drum(TSEL)) str_cpy(b,DRUM_KIT_NAMES[drum_kit()],sizeof b); else trk_short_name(song.sel,b);
            cv_text(8,14,&FONT_L,b,TE_COL[song.sel&3u]);
            for(j=0;j<16u;j++) {
                uint32_t a=j*len/16u,z=(j+1u)*len/16u,q,on=0;
                if(z==a) z=a+1u;
                for(q=a;q<z && q<NSTEP;q++) if(trk_step_on(TSEL,q)) on=1;
                cv_rect(8+(int32_t)j*14,68,10,24,on?TE_COL[song.sel&3u]:TE_G1);
            }
        } else for(k=0;k<NTRK;k++) {
            int32_t cx=30+(int32_t)k*60,level=(int32_t)trk_level(k),j;
            uint16_t col=trk_silent(&trk[k])?TE_DIM[k]:TE_COL[k];
            if(k==song.sel) te_disc(cx,12,3,C_WHITE);   /* the selected track */
            if(pan) {
                /* pan: L..R 눈금과 가운데 표시 위에 트랙 색 마커 */
                int32_t px=cx-18+(trk[k].p[P_PAN]+64)*36/127;
                cv_line(cx-18,64,cx+18,64,TE_G2); cv_rect(cx-18,60,1,9,TE_G2); cv_rect(cx+18,60,1,9,TE_G2);
                cv_rect(cx,58,1,13,TE_G3); cv_rect(px-1,52,3,25,col);
            } else {
                int32_t y=108-level*82/127;
                for(j=0;j<6;j++) cv_line(cx-18,26+j*16,cx+18,26+j*16,TE_G1);
                cv_rect(cx-2,25,4,86,TE_G2); cv_rect(cx-2,y,4,109-y,col);
                cv_rect(cx-17,y-3,34,8,col); cv_line(cx-12,y,cx+12,y,C_BLACK);
            }
            if(trk_silent(&trk[k])) cv_line(cx-8,113,cx+8,113,TE_G3);
        }
        cv_blit(0,36);
    }
    {   /* the knobs: LEVEL / PAN of tracks 1..4 (named by their engines; mute: the first turn unmutes), or
         * on TRACK the selected track's swing / level / steps / pan */
        static const char *const LAB_TRK[4]={"SWING","LEVEL","STEPS","PAN"};
        char names[4][10], v[4][8];
        const char *lab[4]={names[0],names[1],names[2],names[3]}, *val[4]={v[0],v[1],v[2],v[3]};
        int32_t ratio[4];
        if(kind==MX_TRACK) {
            const track_t *t=TSEL; int32_t level=(int32_t)trk_level(song.sel);
            swing_str(v[0],song.g[G_SWING]); ratio[0]=song.g[G_SWING]*10;
            if(t->p[P_MUTE]) { str_cpy(v[1],"MUTE",sizeof v[1]); ratio[1]=0; }
            else { fmt_int(v[1],level*100/127); ratio[1]=level*1000/127; }
            fmt_int(v[2],t->p[P_SLEN]); ratio[2]=(t->p[P_SLEN]-1)*1000/63;
            fmt_int(v[3],t->p[P_PAN]); ratio[3]=(t->p[P_PAN]+64)*1000/127;
            te_dials(184,LAB_TRK,val,ratio,sig,&footer);
            return;
        }
        for(k=0;k<NTRK;k++) {
            int32_t level=(int32_t)trk_level(k);
            str_cpy(names[k],is_drum(&trk[k])?"DRUM":ENGINES[trk[k].eng_req%NENGINES]->name,sizeof names[k]);
            ratio[k]=NO_DIAL;                       /* (the fader / the marker is the picture) */
            if(pan) fmt_int(v[k],trk[k].p[P_PAN]);
            else if(trk[k].p[P_MUTE]) str_cpy(v[k],"MUTE",sizeof v[k]);
            else fmt_int(v[k],level*100/127);
        }
        te_dials(184,lab,val,ratio,sig,&footer);
    }
}
