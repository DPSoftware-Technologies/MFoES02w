// =============================================================================
// example_drawreplay — record draw commands, ship bytes, replay them elsewhere
// =============================================================================
//
// DrawReplay records draw calls instead of rasterising them. DRExport() packs
// the recording into one byte blob, DRRender() replays that blob onto any real
// LinuxGFX target. The point is bandwidth: a remote panel can be driven with a
// few hundred bytes of commands per frame instead of a multi-megabyte
// framebuffer.
//
// This example draws one scene twice —
//   A) straight into a GFXcanvas          (the reference rasterisation)
//   B) into a DrawReplay, exported to bytes, then DRRender'd into a second
//      GFXcanvas
// — and checks the two canvases match pixel for pixel. It then demonstrates
// the record mask, DRFlush(), and how malformed input is rejected.
//
// Console output only; no window is opened.

#include "DrawReplay.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0;

static void check (bool cond, const char *what) {
    printf("%-46s %s\n", what, cond ? "PASS" : "FAIL");
    if (!cond) g_fail++;
}

// The scene is written as a template so the exact same call sequence drives
// both the reference canvas and the recorder.
template <typename T>
static void scene (T &g) {
    g.fillScreen(GFX_BLACK);

    g.fillRect(10, 10, 120, 60, GFX_RED);
    g.drawRect(10, 10, 120, 60, GFX_WHITE);
    g.drawLine(0, 0, 199, 149, GFX_GREEN);
    g.drawCircle(150, 100, 30, GFX_CYAN);
    g.fillCircle(60, 110, 20, GFX_YELLOW);
    g.drawRoundRect(5, 80, 90, 40, 8, GFX_MAGENTA);
    g.fillRoundRect(100, 5, 60, 30, 6, GFX_BLUE);
    g.drawTriangle(20, 140, 60, 100, 100, 140, GFX_WHITE);
    g.fillTriangle(120, 140, 150, 110, 180, 140, GFX_GREEN);
    g.drawArc(100, 75, 45, 0.0f, 135.0f, GFX_WHITE);
    g.drawFastHLine(0, 75, 200, 0x80FFFFFFu);   // alpha blend over content
    g.drawFastVLine(100, 0, 150, 0x8000FF00u);
    g.drawPixel(3, 3, GFX_WHITE);

    g.setTextSize(1);
    g.setTextColor(GFX_WHITE, GFX_TRANSPARENT);
    g.setCursor(12, 20);
    g.writeText("DrawReplay");
    g.setText(12, 60, "remote", GFX_YELLOW, GFX_TRANSPARENT, 2, 2);

    static const uint8_t glyph[] = {
        0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18,
    };
    g.drawBitmap((int16_t)170, (int16_t)10, glyph, (int16_t)8, (int16_t)8, GFX_WHITE);
}

static bool sameCanvas (const GFXcanvas &a, const GFXcanvas &b, size_t &firstDiff) {
    const size_t n = (size_t)a.width() * (size_t)a.height();
    std::vector<uint32_t> pa(n), pb(n);
    if (!a.copyToARGB8888(pa.data()) || !b.copyToARGB8888(pb.data())) return false;
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i]) { firstDiff = i; return false; }
    }
    return true;
}

int main () {
    const int16_t W = 200, H = 150;

    // ----- A: reference rasterisation ---------------------------------------
    GFXcanvas reference(W, H, GFXcanvas::Format::ARGB8888);
    scene(reference);

    // ----- B: record -> bytes -> replay -------------------------------------
    DrawReplay rec(W, H);
    scene(rec);

    std::vector<uint8_t> blob = rec.DRExport();
    printf("commands=%lu  blob=%lu bytes  raw frame=%lu bytes  ratio=%.1fx\n\n",
           (unsigned long)rec.commandCount(),
           (unsigned long)blob.size(),
           (unsigned long)rec.rawFrameBytes(),
           (double)rec.rawFrameBytes() / (double)blob.size());

    check(blob.size() == rec.exportSize(), "exportSize() matches DRExport() length");

    int16_t  pw = 0, ph = 0;
    uint32_t pc = 0, pb = 0;
    check(DrawReplay::DRPeek(blob.data(), blob.size(), pw, ph, pc, pb) == DRError::Ok,
          "DRPeek accepts the blob");
    check(pw == W && ph == H, "DRPeek reports the recorded dimensions");
    check(pc == rec.commandCount(), "DRPeek reports the command count");

    GFXcanvas replayed(W, H, GFXcanvas::Format::ARGB8888);
    const DRError err = DrawReplay::DRRender(blob, replayed);
    check(err == DRError::Ok, "DRRender returns Ok");
    if (err != DRError::Ok) printf("   error: %s\n", DRErrorString(err));

    size_t diff = 0;
    const bool identical = sameCanvas(reference, replayed, diff);
    check(identical, "replayed canvas is pixel-identical");
    if (!identical) {
        printf("   first difference at index %lu (x=%lu y=%lu)\n",
               (unsigned long)diff,
               (unsigned long)(diff % (size_t)W),
               (unsigned long)(diff / (size_t)W));
    }

    // The recorder replays text layout locally with pixel output suppressed,
    // so the cursor ends up where a real target would leave it.
    check(rec.getCursorX() == reference.getCursorX() &&
          rec.getCursorY() == reference.getCursorY(),
          "recorder tracks the cursor like a real target");

    // ----- record mask: keeping pixel payloads out of the stream -------------
    {
        std::vector<uint32_t> sprite(64 * 64, GFX_RED);
        static const uint8_t glyph[] = { 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };

        // Drop every bitmap family — commands only, no inline pixels.
        DrawReplay noBmp(W, H);
        noBmp.DRIgnore(DR_REC_BITMAPS_ALL);
        noBmp.fillRect(0, 0, 10, 10, GFX_BLUE);
        noBmp.drawRGBBitmap(0, 0, sprite.data(), 64, 64);
        check(noBmp.commandCount() == 1, "masked bitmap does not enter the stream");
        check(noBmp.skippedCount() == 1, "masked bitmap is counted as skipped");
        check(!noBmp.DRRecords(DR_REC_BITMAP_RGB), "DRRecords reports the cleared flag");
        check(noBmp.DRRecords(DR_REC_GEOMETRY),    "DRRecords reports the set flag");

        // Selective: keep cheap 1-bit glyphs, drop the fat RGB blits.
        DrawReplay some(W, H);
        some.DRIgnore(DR_REC_BITMAP_RGB);
        some.drawBitmap((int16_t)0, (int16_t)0, glyph, (int16_t)8, (int16_t)8, GFX_WHITE);
        some.drawRGBBitmap(0, 0, sprite.data(), 64, 64);
        check(some.commandCount() == 1 && some.skippedCount() == 1,
              "per-function mask keeps 1-bit, drops RGB");

        // Size limit applies on top of the flags.
        DrawReplay capped(W, H);
        capped.setMaxInlineBytes(1024);
        capped.drawRGBBitmap(0, 0, sprite.data(), 64, 64);   // 16 KB
        check(capped.commandCount() == 0, "inline size limit drops an oversized bitmap");
    }

    // ----- DRFlush: drop history, keep draw state ----------------------------
    {
        const size_t before = rec.commandCount();
        rec.DRFlush();
        check(before > 0 && rec.commandCount() == 0 && rec.empty(),
              "DRFlush clears recorded history");
        rec.fillRect(0, 0, 4, 4, GFX_RED);
        check(rec.commandCount() == 1, "recording resumes after DRFlush");
    }

    // ----- malformed input is rejected, never over-read ----------------------
    {
        GFXcanvas sink(W, H, GFXcanvas::Format::ARGB8888);

        std::vector<uint8_t> bad = blob;
        bad[0] = 'X';
        check(DrawReplay::DRRender(bad, sink) == DRError::BadMagic, "bad magic rejected");

        std::vector<uint8_t> shortBlob(blob.begin(), blob.begin() + DR_HEADER_SIZE - 1);
        check(DrawReplay::DRRender(shortBlob, sink) == DRError::Truncated,
              "short header rejected");

        // Body cut short while the header still claims the full length.
        std::vector<uint8_t> cut(blob.begin(), blob.end() - 8);
        check(DrawReplay::DRRender(cut, sink) == DRError::Truncated,
              "truncated body rejected");

        std::vector<uint8_t> weird = blob;
        weird[DR_HEADER_SIZE] = 0x7E;              // reserved opcode
        check(DrawReplay::DRRender(weird, sink) == DRError::UnknownOpcode,
              "unknown opcode rejected");

        // A bitmap header claiming far more pixels than the stream holds must
        // be refused before anything is allocated.
        DrawReplay lie(W, H);
        static const uint8_t bits[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        lie.drawBitmap((int16_t)0, (int16_t)0, bits, (int16_t)8, (int16_t)8, GFX_WHITE);
        std::vector<uint8_t> hostile = lie.DRExport();
        hostile[DR_HEADER_SIZE + 1 + 4] = 0xFF;    // width low byte  -> huge
        hostile[DR_HEADER_SIZE + 1 + 5] = 0x7F;    // width high byte
        const DRError he = DrawReplay::DRRender(hostile, sink);
        check(he == DRError::BadPayload || he == DRError::Truncated,
              "oversized bitmap extent rejected, no huge alloc");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
