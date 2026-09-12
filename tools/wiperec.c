/* wiperec.c - Aufnahmewerkzeug fuer den Wischpfad des Credits-Endes.
 *
 *   make wiperec && ./wiperec tools/wipe_bg.bmp
 *
 * Zeigt das eingefrorene Credits-Bild (640x400) im VOLLBILD (skaliert,
 * Seitenverhaeltnis bleibt). Die Maus fuehrt den Schwammarm; linke Taste
 * gedrueckt = Schwamm auf dem Glas (wischt, wird schwarz). Aufzeichnung
 * beginnt mit dem ersten Druecken, 50 Hz. Noch ungewischte SICHTBARE
 * Pixel (genau die, die das Spiel spaeter nachputzen wuerde, auch kleine
 * Sterne) werden CYAN markiert.
 *   R   = von vorn (Bild und Aufnahme loeschen)
 *   D   = Restmarkierung an/aus
 *   F   = Vollbild/Fenster
 *   S   = speichern nach src/wipepath.h und beenden
 *   Esc = beenden ohne Speichern
 * Danach: make - das Spiel spielt den Pfad statt des eingebauten Plans.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "../src/armimg.h"

#define W 640
#define H 400
#define MAXN 6000

static Uint32 bg[W * H], fb[W * H];
static unsigned char mask[W * H];
static short path[MAXN][3];
static int n = 0;

/* Abdruck wie im Spiel (main.c cr_wipe_stamp): Ellipse, auf 4x4-Zellen
 * quantisiert (Pixelgrafik) */
#define CELL 4
static void stamp(double sx, double sy)
{
    const double ex = sx - AR_SPCX + AR_SPEX, ey = sy - AR_SPCY + AR_SPEY;
    const double rx = AR_SPRX, ry = AR_SPRY;
    int x, y, i, j;
    for (y = ((int)(ey - ry) / CELL) * CELL; y <= (int)(ey + ry); y += CELL) {
        double dy = (y + CELL * 0.5 - ey) / ry;
        if (y < 0 || y >= H || dy * dy > 1.0) continue;
        for (x = ((int)(ex - rx) / CELL) * CELL; x <= (int)(ex + rx); x += CELL) {
            double dx = (x + CELL * 0.5 - ex) / rx;
            if (x < 0 || x >= W || dx * dx + dy * dy > 1.0) continue;
            for (j = 0; j < CELL && y + j < H; ++j)
                for (i = 0; i < CELL && x + i < W; ++i)
                    mask[(y + j) * W + x + i] = 255;
        }
    }
}

static void blit(const unsigned char *img, int iw, int ih, int x0, int y0)
{
    int x, y;
    for (y = 0; y < ih; ++y)
        for (x = 0; x < iw; ++x) {
            const unsigned char *p = img + (y * iw + x) * 4;
            int px = x0 + x, py = y0 + y;
            unsigned long a = p[3], r0, g0, b0;
            Uint32 *d;
            if (!a || px < 0 || px >= W || py < 0 || py >= H) continue;
            a += a >> 7;
            d = &fb[py * W + px];
            r0 = (*d >> 16) & 0xFF; g0 = (*d >> 8) & 0xFF; b0 = *d & 0xFF;
            r0 = (r0 * (256 - a) + p[0] * a) >> 8;
            g0 = (g0 * (256 - a) + p[1] * a) >> 8;
            b0 = (b0 * (256 - a) + p[2] * a) >> 8;
            *d = 0xFF000000UL | (r0 << 16) | (g0 << 8) | b0;
        }
}

static void save(void)
{
    FILE *f = fopen("src/wipepath.h", "w");
    int i;
    if (!f) { fprintf(stderr, "kann src/wipepath.h nicht schreiben\n"); return; }
    fprintf(f, "/* wipepath.h - aufgenommener Wischpfad fuer das Credits-Ende (Werkzeug\n"
               " * tools/wiperec.c: Maus = Schwamm, Taste gedrueckt = wischt; S speichert\n"
               " * hierher). CR_WPATH_N 0 = kein Pfad: das Spiel wischt nach dem\n"
               " * eingebauten Plan. Eintraege {x, y, down} je 50-Hz-Frame. */\n"
               "#ifndef WIPEPATH_H\n#define WIPEPATH_H\n#define CR_WPATH_N %d\n"
               "static const short cr_wpath[%d][3] = {\n", n, n > 0 ? n : 1);
    if (n == 0) fprintf(f, "{ 0, 0, 0 }\n");
    for (i = 0; i < n; ++i)
        fprintf(f, "{%d,%d,%d}%s%s", path[i][0], path[i][1], path[i][2],
                (i + 1 < n) ? "," : "", ((i & 7) == 7 || i + 1 == n) ? "\n" : "");
    fprintf(f, "};\n#endif\n");
    fclose(f);
    printf("gespeichert: src/wipepath.h (%d Frames = %.1f s)\n", n, n / 50.0);
}

int main(int argc, char **argv)
{
    SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex; SDL_Surface *s;
    int running = 1, started = 0, down = 0, mx = 320, my = 200, i, mark = 1, full = 1;
    Uint32 next;
    if (argc < 2) { fprintf(stderr, "wiperec hintergrund.bmp\n"); return 1; }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    s = SDL_LoadBMP(argv[1]);
    if (!s) { fprintf(stderr, "BMP: %s\n", SDL_GetError()); return 1; }
    {
        SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
        if (!c || c->w != W || c->h != H) { fprintf(stderr, "BMP muss 640x400 sein\n"); return 1; }
        memcpy(bg, c->pixels, sizeof(bg));
        SDL_FreeSurface(c); SDL_FreeSurface(s);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* scharf skalieren */
    win = SDL_CreateWindow("wiperec - Maus wischt, S speichert, R neu, D Marker, F Vollbild, Esc",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2,
                           SDL_WINDOW_FULLSCREEN_DESKTOP);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(ren, W, H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    printf("Wischen: linke Maustaste halten. R = neu, D = Restmarkierung, F = Vollbild, S = speichern+Ende, Esc = Ende\n");
    next = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_s) { save(); running = 0; }
                if (e.key.keysym.sym == SDLK_r) { n = 0; started = 0; memset(mask, 0, sizeof(mask)); }
                if (e.key.keysym.sym == SDLK_d) mark = !mark;
                if (e.key.keysym.sym == SDLK_f) {
                    full = !full;
                    SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) { down = 1; started = 1; }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) down = 0;
            if (e.type == SDL_MOUSEMOTION) { mx = e.motion.x; my = e.motion.y; }
        }
        if (started && n < MAXN) {
            path[n][0] = (short)mx; path[n][1] = (short)my; path[n][2] = (short)down; ++n;
            if (down) stamp(mx, my);
        }
        memcpy(fb, bg, sizeof(fb));
        for (i = 0; i < W * H; ++i) {
            if (mask[i]) { fb[i] = 0xFF000000UL; continue; }
            if (mark) {                   /* sichtbarer Rest (Luminanz >= 28)
                                           * wie im Spiel: cyan hervorheben */
                Uint32 c = bg[i];
                if ((((c >> 16) & 0xFF) | ((c >> 8) & 0xFF) | (c & 0xFF)) >= 28)
                    fb[i] = 0xFF40FFFFUL;
            }
        }
        {
            int x0 = mx - AR_SPCX, y0 = my - AR_SPCY, ry = y0 + AR_H;
            blit(ar_rgba, AR_W, AR_H, x0, y0);
            while (ry < H) { blit(ar_rgba + AR_RODY0 * AR_W * 4, AR_W, AR_H - AR_RODY0, x0, ry); ry += AR_H - AR_RODY0; }
        }
        SDL_UpdateTexture(tex, NULL, fb, W * sizeof(Uint32));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        next += 20;
        {
            Sint32 wait = (Sint32)(next - SDL_GetTicks());
            if (wait > 0) SDL_Delay((Uint32)wait); else next = SDL_GetTicks();
        }
        if (n >= MAXN) { printf("Aufnahme voll (%d Frames)\n", MAXN); }
    }
    SDL_Quit();
    return 0;
}
