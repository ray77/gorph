/* modplay.c - ProTracker-MOD-Replay in C89 (siehe modplay.h).
 *
 * Timing wie ProTracker: Tick = 2.5/Tempo Sekunden, Speed Ticks je Row.
 * Tonhoehe aus der Amiga-Periode: f = 7093789.2 / (2 * Periode) (PAL).
 * Mischung: 4 Kanaele linear interpoliert, Summe * 1/4.
 */
#include "modplay.h"
#include <string.h>
#include <math.h>

#define MOD_CH 4
#define PAL_CLOCK 7093789.2

typedef struct {
    const signed char *d;
    long len, lstart, llen;
    int  vol, fine;
} Sample;

typedef struct {
    int    smp;                 /* 1..31, 0 = keiner */
    double pos, step;
    int    period, tperiod;     /* aktuell / Ziel (Tonportamento) */
    int    vol;
    int    arp;                 /* Effektparameter Arpeggio */
    int    portaspd, tportaspd; /* Effektspeicher */
    int    vibpos, vibspd, vibdep;
    int    vslide;
    int    eff, param;          /* Effekt der laufenden Row */
    int    delaynote, delaycnt; /* ED */
    int    retrig;              /* E9 */
    long   offset;              /* 9xx-Speicher */
    int    playing;
} Chan;

static Sample smp[32];
static const unsigned char *patdata;
static int  npat, songlen, restart;
static unsigned char orders[128];
static Chan ch[MOD_CH];
static int  speed, tempo, tick, row, order, ticklen, tickpos;
static int  active, outrate;
static int  breakrow, jumporder, patloop_row, patloop_cnt, patdelay;

/* ProTracker-Sinus fuer Vibrato */
static const unsigned char vibtab[32] = {
      0, 24, 49, 74, 97,120,141,161,180,197,212,224,235,244,250,253,
    255,253,250,244,235,224,212,197,180,161,141,120, 97, 74, 49, 24 };

static double arpmul[16];

int mod_load(const unsigned char *data, unsigned long len, int rate)
{
    unsigned long o, i, maxpat = 0;
    if (len < 1084) return -1;
    outrate = rate;
    for (i = 0; i < 16; ++i) arpmul[i] = pow(2.0, -(double)i / 12.0);
    memset(smp, 0, sizeof(smp));
    for (i = 0; i < 31; ++i) {
        o = 20 + i * 30;
        smp[i + 1].len    = 2L * ((data[o + 22] << 8) | data[o + 23]);
        smp[i + 1].fine   = data[o + 24] & 15;
        if (smp[i + 1].fine > 7) smp[i + 1].fine -= 16;
        smp[i + 1].vol    = data[o + 25] > 64 ? 64 : data[o + 25];
        smp[i + 1].lstart = 2L * ((data[o + 26] << 8) | data[o + 27]);
        smp[i + 1].llen   = 2L * ((data[o + 28] << 8) | data[o + 29]);
    }
    songlen = data[950];
    restart = data[951];
    memcpy(orders, data + 952, 128);
    for (i = 0; i < 128; ++i) if (orders[i] > maxpat) maxpat = orders[i];
    npat = (int)maxpat + 1;
    patdata = data + 1084;
    o = 1084 + (unsigned long)npat * 64 * MOD_CH * 4;
    for (i = 1; i <= 31; ++i) {
        if (o + (unsigned long)smp[i].len > len) smp[i].len = 0;
        smp[i].d = (const signed char *)(data + o);
        o += (unsigned long)smp[i].len;
        if (smp[i].lstart + smp[i].llen > smp[i].len)
            smp[i].llen = smp[i].len - smp[i].lstart;
    }
    active = 0;
    return 0;
}

static void set_tempo(int t)
{
    tempo = t;
    ticklen = (int)(outrate * 2.5 / tempo);
}

void mod_start(void)
{
    memset(ch, 0, sizeof(ch));
    speed = 6; set_tempo(125);
    tick = 0; row = -1; order = 0; tickpos = 0;   /* advance() zaehlt auf Row 0 */
    breakrow = -1; jumporder = -1; patloop_row = 0; patloop_cnt = 0;
    patdelay = 0;
    active = (patdata != 0);
    tick = speed;                       /* erster Aufruf verarbeitet Row 0 */
    tickpos = ticklen;
}

void mod_stop(void)  { active = 0; }

static void advance(void);

/* Zur Zeitposition springen: neu starten und die Ticks OHNE Audio
 * durchlaufen (Pattern-/Effektzustand stimmt dann; laufende Samples
 * beginnen an ihrem Anfang - bei Chiptune-Loops unhoerbar) */
void mod_seek(double seconds)
{
    long target = (long)(seconds * outrate), done = 0;
    if (!patdata) return;
    mod_start();
    while (done < target) { advance(); done += ticklen; }
    tickpos = 0;
}
int  mod_playing(void) { return active; }

static double period_freq(int period)
{
    if (period < 1) period = 1;
    return PAL_CLOCK / (2.0 * period);
}

static void update_step(Chan *c, int period)
{
    c->step = period_freq(period) / (double)outrate;
}

static int fine_period(int period, int fine)
{
    /* Finetune: 1/8 Halbton je Stufe */
    return (int)(period * pow(2.0, -fine / 96.0) + 0.5);
}

static void trigger(Chan *c)
{
    c->pos = 0.0;
    c->playing = (c->smp > 0 && smp[c->smp].len > 0);
    c->vibpos = 0;
}

static void process_row(void)
{
    const unsigned char *p = patdata + ((long)orders[order] * 64 + row) * MOD_CH * 4;
    int i;
    for (i = 0; i < MOD_CH; ++i, p += 4) {
        Chan *c = &ch[i];
        int period = ((p[0] & 15) << 8) | p[1];
        int ins    = (p[0] & 0xF0) | (p[2] >> 4);
        int eff    = p[2] & 15, param = p[3];
        c->eff = eff; c->param = param;
        if (ins > 0 && ins <= 31) {
            c->smp = ins;
            c->vol = smp[ins].vol;
        }
        if (period > 0) {
            if (c->smp > 0) period = fine_period(period, smp[c->smp].fine);
            if (eff == 3 || eff == 5) {
                c->tperiod = period;          /* Tonportamento: nur Ziel */
            } else if (eff == 0xE && (param >> 4) == 0xD) {
                c->delaynote = period; c->delaycnt = param & 15;
            } else {
                c->period = period; c->tperiod = period;
                trigger(c);
                if (eff == 9) {
                    long off = (param ? (long)param : c->offset) * 256L;
                    c->offset = off;
                    if (c->smp && off < smp[c->smp].len) c->pos = (double)off;
                    else if (c->smp) c->pos = (double)smp[c->smp].len;
                }
            }
        }
        switch (eff) {
        case 0x1: if (param) c->portaspd = param; break;
        case 0x2: if (param) c->portaspd = param; break;
        case 0x3: if (param) c->tportaspd = param; break;
        case 0x4: if (param >> 4) c->vibspd = param >> 4;
                  if (param & 15) c->vibdep = param & 15; break;
        case 0xA: c->vslide = param; break;
        case 0xB: jumporder = param; breakrow = 0; break;
        case 0xC: c->vol = param > 64 ? 64 : param; break;
        case 0xD: breakrow = (param >> 4) * 10 + (param & 15);
                  if (breakrow > 63) breakrow = 0; break;
        case 0xE:
            switch (param >> 4) {
            case 0x1: c->period -= param & 15; break;
            case 0x2: c->period += param & 15; break;
            case 0x6:
                if (!(param & 15)) patloop_row = row;
                else if (patloop_cnt < (param & 15)) {
                    ++patloop_cnt; breakrow = patloop_row; jumporder = order;
                } else patloop_cnt = 0;
                break;
            case 0x9: c->retrig = param & 15; break;
            case 0xA: c->vol += param & 15; if (c->vol > 64) c->vol = 64; break;
            case 0xB: c->vol -= param & 15; if (c->vol < 0) c->vol = 0; break;
            case 0xC: if (!(param & 15)) c->vol = 0; break;
            case 0xE: patdelay = param & 15; break;
            default: break;
            }
            break;
        case 0xF:
            if (param == 0) break;
            if (param < 32) speed = param; else set_tempo(param);
            break;
        default: break;
        }
        if (c->period < 54) c->period = 54;
        if (c->period > 1814) c->period = 1814;
        update_step(c, c->period);
    }
}

static void process_tick(void)
{
    int i;
    for (i = 0; i < MOD_CH; ++i) {
        Chan *c = &ch[i];
        int period = c->period, x = c->param >> 4, y = c->param & 15;
        int vs = (c->eff == 0xA || c->eff == 5 || c->eff == 6);
        switch (c->eff) {
        case 0x0:                         /* Arpeggio */
            if (c->param) {
                int semi = (tick % 3 == 1) ? x : (tick % 3 == 2) ? y : 0;
                period = (int)(c->period * arpmul[semi] + 0.5);
            }
            break;
        case 0x1: c->period -= c->portaspd; break;
        case 0x2: c->period += c->portaspd; break;
        case 0x3: case 0x5:
            if (c->tperiod) {
                if (c->period < c->tperiod) {
                    c->period += c->tportaspd;
                    if (c->period > c->tperiod) c->period = c->tperiod;
                } else if (c->period > c->tperiod) {
                    c->period -= c->tportaspd;
                    if (c->period < c->tperiod) c->period = c->tperiod;
                }
            }
            break;
        case 0x4: case 0x6:
            {
                int p = c->vibpos & 63;
                int d = (vibtab[p & 31] * c->vibdep) / 128;
                period = c->period + ((p < 32) ? d : -d);
                c->vibpos += c->vibspd;
            }
            break;
        case 0xE:
            if ((c->param >> 4) == 0x9 && c->retrig && tick % c->retrig == 0)
                trigger(c);
            if ((c->param >> 4) == 0xC && tick == (c->param & 15)) c->vol = 0;
            if ((c->param >> 4) == 0xD && tick == c->delaycnt && c->delaynote) {
                c->period = c->delaynote; c->tperiod = c->delaynote;
                trigger(c); c->delaynote = 0;
            }
            break;
        default: break;
        }
        if (vs) {                         /* Axy, 5xy, 6xy: Volumeslide */
            if (x) c->vol += x; else c->vol -= y;
            if (c->vol > 64) c->vol = 64;
            if (c->vol < 0) c->vol = 0;
        }
        if (c->period < 54) c->period = 54;
        if (c->period > 1814) c->period = 1814;
        if (period < 54) period = 54;
        if (period > 1814) period = 1814;
        update_step(c, period);
    }
}

static void advance(void)
{
    if (++tick >= speed) {
        tick = 0;
        if (patdelay) { --patdelay; process_tick(); return; }
        if (breakrow >= 0) {
            row = breakrow; breakrow = -1;
            if (jumporder >= 0) { order = jumporder; jumporder = -1; }
            else ++order;
        } else if (++row >= 64) {
            row = 0; ++order;
        }
        if (order >= songlen) order = (restart < songlen) ? restart : 0;
        process_row();
    } else {
        process_tick();
    }
}

void mod_render(double *out, int n)
{
    int i, k;
    if (!active) { for (i = 0; i < n; ++i) out[i] = 0.0; return; }
    for (i = 0; i < n; ++i) {
        double s = 0.0;
        if (++tickpos >= ticklen) { tickpos = 0; advance(); }
        for (k = 0; k < MOD_CH; ++k) {
            Chan *c = &ch[k];
            const Sample *sp;
            long ip; double fr, a, b;
            if (!c->playing || !c->smp) continue;
            sp = &smp[c->smp];
            ip = (long)c->pos; fr = c->pos - ip;
            if (ip >= sp->len) { c->playing = 0; continue; }
            a = sp->d[ip];
            b = (ip + 1 < sp->len) ? sp->d[ip + 1]
              : (sp->llen > 2 ? sp->d[sp->lstart] : a);
            s += (a + (b - a) * fr) / 128.0 * (c->vol / 64.0);
            c->pos += c->step;
            if (sp->llen > 2) {
                while (c->pos >= sp->lstart + sp->llen) c->pos -= sp->llen;
            } else if (c->pos >= sp->len) {
                c->playing = 0;
            }
        }
        out[i] = s * 0.25;
    }
}
