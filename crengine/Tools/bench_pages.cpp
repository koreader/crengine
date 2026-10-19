// Headless crengine page-turn benchmark.
// Loads a document, resizes to a page size, and turns N pages,
// timing each turn. No X11/display needed. For Valgrind profiling.
#include "crengine.h"
#include "lvdocview.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file> [pages]\n", argv[0]);
        return 2;
    }
    const char * file = argv[1];
    int pages = argc >= 3 ? atoi(argv[2]) : 20;

    CRLog::setStdoutLogger();
    CRLog::setLogLevel(CRLog::LL_WARN);

    // Init font manager with a font dir (needed for rendering)
    // With USE_FONTCONFIG=0 there's no automatic font enumeration: register
    // the DejaVu fonts explicitly.
    InitFontManager(lString8());
    static const char * fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-BoldItalic.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        NULL
    };
    for (int i = 0; fonts[i]; i++) {
        fontMan->RegisterFont(lString8(fonts[i]));
    }
    fprintf(stderr, "%d fonts loaded\n", fontMan->GetFontCount());

    LVDocView text_view;
    text_view.Resize(600, 800);

    double t_load = now_sec();
    if (!text_view.LoadDocument(file)) {
        fprintf(stderr, "Cannot open document %s\n", file);
        return 1;
    }
    fprintf(stderr, "Loaded in %.3fs\n", now_sec() - t_load);
    fprintf(stderr, "Opened %s\n", file);
    double t0 = now_sec();
    text_view.Render();
    fprintf(stderr, "Rendered in %.3fs\n", now_sec() - t0);
    int total = text_view.getPageCount();
    fprintf(stderr, "Total pages: %d\n", total);

    int n = pages < total ? pages : total;
    double t_total = 0;
    for (int i = 0; i < n; i++) {
        double t0 = now_sec();
        text_view.goToPage(i);
        double dt = now_sec() - t0;
        t_total += dt;
        fprintf(stderr, "page %3d: %.3fs\n", i, dt);
    }
    fprintf(stderr, "avg over %d pages: %.3fs\n", n, t_total / n);
    return 0;
}
