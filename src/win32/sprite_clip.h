// Rognage d'une image au rectangle de dessin, tel que le calcule le jeu avant de dessiner un
// sprite : FUN_00509c20 en largeur, FUN_00509cd0 en hauteur. Utilisé par le répartiteur de
// sprites natif (native.cpp) ; sans dépendance au processeur émulé, pour être testé seul.
//
// État de rognage (octets gog::kClipX et gog::kClipY) : 0 entière, 1 rognée à gauche, 2 à droite,
// 3 en haut, 4 en bas, 5 invisible.

#pragma once

#include <cstdint>

namespace zeus::win32 {

struct HorizontalClip {
    int32_t left = 0;       // x du premier pixel écrit
    int32_t skip_left = 0;  // pixels de l'image masqués à gauche
    int32_t right = 0;      // fin de la partie visible, dans l'image
    int32_t visible = 0;    // pixels visibles par ligne
    uint8_t state = 0;
};

// FUN_00509c20 : image de `width` pixels placée en `x`, rectangle de dessin [clip_left, clip_right[.
inline HorizontalClip clip_horizontal(int32_t x, int16_t width16, int32_t clip_left, int32_t clip_right) {
    HorizontalClip c;
    const int32_t width = width16;
    c.left = x;
    c.right = width;
    if (width16 < 1) {
        c.state = 5;
    } else if (x < clip_left) {
        if (width + x <= clip_left) {
            c.state = 5;
        } else {
            c.state = 1;
            c.skip_left = clip_left - x;
            c.left = clip_left;
        }
    } else if (clip_right - width < x) {
        if (clip_right <= x) {
            c.state = 5;
        } else {
            c.right = clip_right - x;
            c.state = 2;
        }
    }
    if (c.state != 5) c.visible = c.right - c.skip_left;
    return c;
}

struct VerticalClip {
    int32_t top = 0;      // y de la première ligne écrite
    int32_t rows = 0;     // lignes de l'image jusqu'à la dernière visible
    int32_t skipped = 0;  // lignes de l'image masquées en haut
    int32_t drawn = 0;    // lignes à dessiner
    uint8_t state = 0;
};

// FUN_00509cd0 : image de `height` lignes placée en `y`, rectangle de dessin [clip_top, clip_bottom[.
inline VerticalClip clip_vertical(int32_t y, int16_t height16, int32_t clip_top, int32_t clip_bottom) {
    VerticalClip c;
    c.top = y;
    c.rows = height16;
    if (height16 < 1) {
        c.state = 5;
    } else if (y < clip_top) {
        if (c.rows + y <= clip_top) {
            c.state = 5;
        } else {
            c.top = clip_top;
            c.skipped = clip_top - y;
            c.state = 3;
            const int32_t limit = (clip_bottom - clip_top) + c.skipped;
            if (limit < c.rows) c.rows = limit;
        }
    } else if (clip_bottom - c.rows < y) {
        if (y < clip_bottom) {
            c.state = 4;
            c.rows = clip_bottom - y;
        } else {
            c.state = 5;
        }
    }
    if (c.state != 5) c.drawn = c.rows - c.skipped;
    return c;
}

}  // namespace zeus::win32
