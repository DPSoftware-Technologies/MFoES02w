#pragma once
#include <cstdint>

namespace uisys {

// ─── Font descriptor ──────────────────────────────────────────────────────────
//
// Wraps GFX font selection. GFX libraries (Adafruit-style) use:
//   gfx.setTextSize(scale)          — 1=6x8px, 2=12x16px, 3=18x24px ...
//   gfx.setFont(const GFXfont*)     — custom bitmap font (nullptr = default)
//
// Usage:
//   uisys::Font f = uisys::Font::Size(2);         // built-in 2x scale
//   uisys::Font f = uisys::Font::Custom(&myFont); // custom GFX font
//   f.apply(gfx);                                 // apply to GFX context

struct Font {
    uint8_t         scale  = 1;       // text size multiplier (1-4)
    const void*     gfxFont = nullptr; // GFXfont* — null = built-in

    // Character dimensions for layout calculations
    int charW() const { return 6 * scale; }   // default font: 6px wide
    int charH() const { return 8 * scale; }   // default font: 8px tall

    // ── Presets ───────────────────────────────────────────────────────────────

    static Font Size(uint8_t s) {
        Font f; f.scale = s; return f;
    }

    static Font Custom(const void* font, uint8_t s = 1) {
        Font f; f.gfxFont = font; f.scale = s; return f;
    }

    static Font Small()  { return Size(1); }
    static Font Medium() { return Size(2); }
    static Font Large()  { return Size(3); }
    static Font Huge()   { return Size(4); }

    // ── Apply to GFX context ──────────────────────────────────────────────────

    template<typename GFX>
    void apply(GFX& gfx) const {
        gfx.setTextSize(scale);
        if (gfxFont)
            gfx.setFont((const GFXfont*)gfxFont);
        else
            gfx.setFont(nullptr);
    }

    // Helper: text pixel width for a given string length
    int textWidth(int chars) const { return chars * charW(); }
};

} // namespace uisys
