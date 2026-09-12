# GORPH

**Galactic Organic Robot Phalanx**

Ein nativer Fixed-Screen-Shooter im Stil der fruehen 80er, geschrieben in
ANSI C89 mit SDL2 — praesentiert im Look eines C64 samt Boot-Screen,
Rasterfarben, Multicolor-Sprites und Demo-Szene-Effekten.

Die Phalanx der Gorphianer greift in vier Wellen an. Wer alle vier
uebersteht, beginnt den Feldzug von vorn — schneller und gnadenloser.

## Missionen

1. **Astro Battles** — Alien-Formation hinter Schutzschilden
2. **Laser Attack** — Kanonenverbaende mit Sturzflug-Jaegern
3. **Space Warp** — Spiralflug-Objekte im Sternentunnel
4. **Flag Ship** — das Flaggschiff, Stueck fuer Stueck zerlegbar

## Bauen

Benoetigt SDL2 (`sdl2-config` im Pfad):

```
make
./gorph
```

## Steuerung

| Taste            | Funktion            |
|------------------|---------------------|
| Pfeile / WASD    | Bewegen             |
| Leertaste        | Feuer               |
| F11              | Vollbild            |
| Esc              | Beenden             |

Gamepad wird automatisch erkannt.

Debug-/Cheat-Tasten sind im Normalstart aus. `./gorph -cheat` schaltet sie ein:
1–4 Mission 1–4, 5 Titel-Intro neu, 6 Flagship-Kill, 8 Level-8-Outro,
9 Boot-Screen neu, N nächste Mission inkl. Rang, 0 in den Credits: Sprung kurz vors Ende.

## Features

- C64-Boot-Screen mit Typewriter-Prompt, danach nahtloser Zoom ins Spiel
- Intro mit Sternenfeld-Rotation, Logo-Einflug, Sprachausgabe und
  kreisendem Roboter samt Motion-Blur
- Attract-Mode: nach Inaktivitaet spielt eine Demo (Astro Battles und
  Space Warp im Wechsel)
- Lippensynchrone Typewriter-Outros mit Sprachsamples
- Software-VIC mit 16 Sprites, Screenshake, TV-Glitch-Uebergaengen
- Synthesizer-Soundeffekte plus gesampelte Sprache

## Technik

Spiellogik, Renderer (Text-/Bitmap-Multicolor-Modi) und Klangerzeugung
sind komplett eigenstaendig implementiert; alle Assets liegen eingebettet
im Quellcode. Kein fremder Programmcode enthalten.
