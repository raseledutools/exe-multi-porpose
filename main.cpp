// =============================================================================
//  RasPDF Pro Max  –  Full PDF Editor
//  Win32 + GDI+ + PDFium
//  Single-file build:  main.cpp
//  Compiler: MinGW-w64 (g++)
//
//  Build command:
//    g++ main.cpp -o RasPDF.exe ^
//      -I"./pdfium/include" ^
//      -L"./pdfium/lib"    ^
//      -lpdfium            ^
//      -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -lshell32 ^
//      -mwindows -std=c++17
//
//  Folder layout expected:
//    project/
//      main.cpp
//      pdfium/
//        include/   (fpdfview.h, fpdf_edit.h, fpdf_save.h …)
//        lib/       (pdfium.lib or libpdfium.a)
//        pdfium.dll
// =============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// ── Windows / GDI+ ──────────────────────────────────────────────────────────
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <fpdfview.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// ── PDFium ───────────────────────────────────────────────────────────────────

// --- PDFium ---
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdf_annot.h>
#include <fpdf_formfill.h>
#include <fpdf_flatten.h>
#include <fpdfview.h>

// ── STL ──────────────────────────────────────────────────────────────────────
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <map>
#include <stack>
#include <sstream>
#include <fstream>
#include <chrono>
#include <optional>

using namespace Gdiplus;
using namespace std;

// =============================================================================
//  SECTION 1 – CONSTANTS & ENUMERATIONS
// =============================================================================

static const wchar_t* APP_NAME       = L"RasPDF Pro Max";
static const wchar_t* APP_CLASS      = L"RasPDFWndClass";
static const wchar_t* TOOLBAR_CLASS  = L"RasPDFToolbar";
static const wchar_t* SIDEBAR_CLASS  = L"RasPDFSidebar";
static const wchar_t* CANVAS_CLASS   = L"RasPDFCanvas";
static const wchar_t* STATUS_CLASS   = L"RasPDFStatus";

static const int TITLEBAR_H   = 32;
static const int TOOLBAR_H    = 48;
static const int SIDEBAR_W    = 200;
static const int STATUS_H     = 28;
static const int THUMB_W      = 180;
static const int THUMB_H      = 220;
static const int MIN_ZOOM     = 25;
static const int MAX_ZOOM     = 500;
static const int SCROLL_STEP  = 40;

// Tool IDs
enum class Tool {
    Select = 0,
    Hand,
    Text,
    Highlight,
    Underline,
    Strikethrough,
    Draw,
    Eraser,
    Rectangle,
    Ellipse,
    Arrow,
    StickyNote,
    Stamp,
    Redact,
    WhiteOut,
    Signature,
    Measure,
    _Count
};

// Annotation types stored per-page
enum class AnnotKind {
    Text,
    Highlight,
    Underline,
    Strikethrough,
    Ink,
    Rectangle,
    Ellipse,
    Arrow,
    StickyNote,
    Stamp,
    Redact,
    WhiteOut,
    Signature,
};

// WM_APP sub-commands
enum AppMsg : UINT {
    APP_REPAINT_CANVAS  = WM_APP + 1,
    APP_REPAINT_SIDEBAR = WM_APP + 2,
    APP_STATUS_UPDATE   = WM_APP + 3,
    APP_ZOOM_CHANGED    = WM_APP + 4,
    APP_PAGE_CHANGED    = WM_APP + 5,
};

// Button / control IDs
enum CtrlID : int {
    ID_BTN_OPEN         = 1001,
    ID_BTN_SAVE         = 1002,
    ID_BTN_SAVE_AS      = 1003,
    ID_BTN_PREV         = 1004,
    ID_BTN_NEXT         = 1005,
    ID_BTN_ZOOM_IN      = 1006,
    ID_BTN_ZOOM_OUT     = 1007,
    ID_BTN_ZOOM_FIT     = 1008,
    ID_BTN_ROTATE_CW    = 1009,
    ID_BTN_ROTATE_CCW   = 1010,
    ID_BTN_FULLSCREEN   = 1011,
    ID_BTN_UNDO         = 1012,
    ID_BTN_REDO         = 1013,
    ID_BTN_PRINT        = 1014,
    ID_BTN_CLOSE_DOC    = 1015,
    ID_TOOL_SELECT      = 2000,
    ID_TOOL_HAND        = 2001,
    ID_TOOL_TEXT        = 2002,
    ID_TOOL_HIGHLIGHT   = 2003,
    ID_TOOL_UNDERLINE   = 2004,
    ID_TOOL_STRIKE      = 2005,
    ID_TOOL_DRAW        = 2006,
    ID_TOOL_ERASER      = 2007,
    ID_TOOL_RECT        = 2008,
    ID_TOOL_ELLIPSE     = 2009,
    ID_TOOL_ARROW       = 2010,
    ID_TOOL_STICKY      = 2011,
    ID_TOOL_STAMP       = 2012,
    ID_TOOL_REDACT      = 2013,
    ID_TOOL_WHITEOUT    = 2014,
    ID_TOOL_SIGNATURE   = 2015,
    ID_TOOL_MEASURE     = 2016,
    ID_COLOR_PICK       = 3000,
    ID_STROKE_SLIDER    = 3001,
    ID_OPACITY_SLIDER   = 3002,
    ID_SIDEBAR_THUMB    = 4000,
    ID_EDIT_PAGE_NUM    = 5000,
    ID_EDIT_ZOOM        = 5001,
    ID_COMBO_STAMP      = 5002,
    ID_BTN_CLOSE        = 9999,
};

// =============================================================================
//  SECTION 2 – HELPER STRUCTURES
// =============================================================================

struct Vec2 { float x, y; };
struct Rect2 { float x, y, w, h; };

// One ink stroke = list of points
struct InkStroke {
    vector<Vec2> pts;
    COLORREF     color;
    float        width;
    float        opacity;
};

// Generic annotation on a page
struct Annotation {
    AnnotKind   kind;
    Rect2       bounds;       // page-space coords (pt)
    COLORREF    color;
    float       strokeWidth;
    float       opacity;
    wstring     text;         // for Text / StickyNote / Stamp
    vector<InkStroke> strokes; // for Ink
    bool        selected;
    int         rotation;     // 0,90,180,270
    // arrow endpoints in page-space
    Vec2 arrowStart, arrowEnd;
};

// Undo/Redo action
enum class ActionKind { AddAnnot, RemoveAnnot, MoveAnnot, ModifyAnnot };
struct UndoAction {
    ActionKind   kind;
    int          pageIdx;
    Annotation   before;
    Annotation   after;
    int          annotIdx;
};

// Per-page state
struct PageState {
    vector<Annotation> annots;
    int  rotation; // additional rotation (0,90,180,270)
};

// Signature data
struct SignatureData {
    vector<InkStroke> strokes;
    bool  hasData;
};

// Stamp presets
static const wchar_t* STAMPS[] = {
    L"APPROVED", L"REJECTED", L"CONFIDENTIAL",
    L"DRAFT",    L"FINAL",    L"COPY",
    L"VOID",     L"RECEIVED", L"REVIEWED",
};
static const int STAMP_COUNT = 9;

// =============================================================================
//  SECTION 3 – GLOBAL APPLICATION STATE
// =============================================================================

struct AppState {
    // ── Window handles ──────────────────────────────────────────────────────
    HWND hwndMain      = nullptr;
    HWND hwndToolbar   = nullptr;
    HWND hwndSidebar   = nullptr;
    HWND hwndCanvas    = nullptr;
    HWND hwndStatus    = nullptr;
    HWND hwndPageEdit  = nullptr;
    HWND hwndZoomEdit  = nullptr;
    HWND hwndStampCombo= nullptr;

    // ── GDI+ token ──────────────────────────────────────────────────────────
    ULONG_PTR gdipToken = 0;

    // ── DPI / scaling ───────────────────────────────────────────────────────
    float dpiScale = 1.0f;

    // ── PDFium document ─────────────────────────────────────────────────────
    FPDF_DOCUMENT  pdfDoc       = nullptr;
    FPDF_FORMHANDLE pdfForm     = nullptr;
    int            pageCount    = 0;
    int            currentPage  = 0;   // 0-based
    wstring        filePath;
    bool           isModified   = false;

    // ── Page states (annotations + per-page rotation) ───────────────────────
    vector<PageState> pages;

    // ── Rendered page cache ─────────────────────────────────────────────────
    // We keep a small LRU of rendered GDI+ bitmaps for speed
    struct PageCache {
        int      pageIdx   = -1;
        int      zoom      = 0;
        int      rotation  = 0;
        Bitmap*  bmp       = nullptr;
    };
    static const int CACHE_SIZE = 5;
    PageCache cache[CACHE_SIZE];
    int       cacheHead = 0;

    // ── View state ───────────────────────────────────────────────────────────
    int   zoom        = 100;   // percent
    int   scrollX     = 0;
    int   scrollY     = 0;
    bool  isFullscreen= false;
    RECT  savedWndRect= {};

    // ── Sidebar / thumbnail state ────────────────────────────────────────────
    bool  sidebarVisible = true;
    vector<Bitmap*> thumbs;
    int   thumbScrollY   = 0;

    // ── Active tool & drawing state ──────────────────────────────────────────
    Tool      activeTool   = Tool::Hand;
    COLORREF  activeColor  = RGB(255, 235, 0);   // yellow default
    float     strokeWidth  = 2.0f;
    float     opacity      = 0.6f;
    int       stampIndex   = 0;

    // dragging / drawing in progress
    bool   isDrawing       = false;
    bool   isPanning       = false;
    POINT  panStart        = {};
    int    panScrollX0     = 0;
    int    panScrollY0     = 0;

    InkStroke  currentStroke;
    Annotation currentAnnot;
    bool       annotStarted = false;
    POINT      annotAnchor  = {};   // screen coords at mouse-down

    // selection
    int  selectedAnnotPage  = -1;
    int  selectedAnnotIdx   = -1;
    bool isDraggingAnnot    = false;
    POINT dragAnnotOffset   = {};

    // ── Undo / Redo stacks ───────────────────────────────────────────────────
    stack<UndoAction> undoStack;
    stack<UndoAction> redoStack;

    // ── Signature capture ────────────────────────────────────────────────────
    SignatureData signature;
    HWND hwndSigDialog = nullptr;

    // ── Window-drag (custom title bar) ───────────────────────────────────────
    bool  isWinDragging = false;
    POINT winDragStart  = {};

    // ── Close button hover ───────────────────────────────────────────────────
    bool closeBtnHover  = false;
    RECT closeBtnRect   = {};

    // ── Status text ─────────────────────────────────────────────────────────
    wstring statusText  = L"Ready — Open a PDF to begin.";

    // ── Measure tool state ───────────────────────────────────────────────────
    bool  measureStarted = false;
    Vec2  measureP1 = {}, measureP2 = {};

    // ── Text-insert dialog ───────────────────────────────────────────────────
    HWND hwndTextDialog  = nullptr;
    wstring pendingText;
    Rect2   pendingTextBounds = {};

    // ── Color picker recent ─────────────────────────────────────────────────
    COLORREF recentColors[8] = {
        RGB(255,235,0), RGB(0,200,83),  RGB(33,150,243), RGB(244,67,54),
        RGB(156,39,176),RGB(255,87,34), RGB(0,0,0),      RGB(255,255,255),
    };
};

static AppState G;   // single global instance

// =============================================================================
//  SECTION 4 – UTILITY FUNCTIONS
// =============================================================================

// Scale a value by DPI
inline int S(int v)   { return (int)(v * G.dpiScale + 0.5f); }
inline float SF(float v) { return v * G.dpiScale; }

// GDI+ helpers
static RectF ToRectF(const RECT& r) {
    return RectF((REAL)r.left, (REAL)r.top,
                 (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}
static Rect ToRect(const RECT& r) {
    return Rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}
static Color GdipColor(COLORREF c, BYTE alpha = 255) {
    return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// Screen → page-space conversion
static Vec2 ScreenToPage(int sx, int sy, int pageW_pt, int pageH_pt) {
    float scale = G.zoom / 100.0f * G.dpiScale;
    float px = (sx + G.scrollX) / scale;
    float py = (sy + G.scrollY) / scale;
    return { px, py };
}

// Page-space → screen conversion
static POINT PageToScreen(float px, float py) {
    float scale = G.zoom / 100.0f * G.dpiScale;
    return { (int)(px * scale - G.scrollX),
             (int)(py * scale - G.scrollY) };
}

// Bounding rect of annotation in screen coords
static RECT AnnotScreenRect(const Annotation& a) {
    float scale = G.zoom / 100.0f * G.dpiScale;
    RECT r;
    r.left   = (int)(a.bounds.x * scale) - G.scrollX;
    r.top    = (int)(a.bounds.y * scale) - G.scrollY;
    r.right  = (int)((a.bounds.x + a.bounds.w) * scale) - G.scrollX;
    r.bottom = (int)((a.bounds.y + a.bounds.h) * scale) - G.scrollY;
    return r;
}

// Check if two RECTs intersect
static bool RectsIntersect(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left &&
           a.top  < b.bottom && a.bottom > b.top;
}

// Set window title with modification indicator
static void UpdateTitle() {
    wstring title = APP_NAME;
    if (!G.filePath.empty()) {
        wstring fname = G.filePath.substr(G.filePath.rfind(L'\\') + 1);
        title += L" — " + fname;
    }
    if (G.isModified) title += L" *";
    SetWindowTextW(G.hwndMain, title.c_str());
}

// Set status bar text
static void SetStatus(const wstring& txt) {
    G.statusText = txt;
    InvalidateRect(G.hwndStatus, nullptr, FALSE);
}

// Format zoom label
static wstring ZoomLabel() {
    return to_wstring(G.zoom) + L"%";
}

// Simple WCHAR string from int
static wstring ItoW(int n) { return to_wstring(n); }

// Clamp helper
template<typename T>
static T Clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

// =============================================================================
//  SECTION 5 – UNDO / REDO SYSTEM
// =============================================================================

static void PushUndo(UndoAction act) {
    G.undoStack.push(act);
    while (!G.redoStack.empty()) G.redoStack.pop();
    G.isModified = true;
    UpdateTitle();
}

static void DoUndo() {
    if (G.undoStack.empty()) return;
    UndoAction act = G.undoStack.top(); G.undoStack.pop();
    PageState& ps = G.pages[act.pageIdx];
    switch (act.kind) {
    case ActionKind::AddAnnot:
        if (act.annotIdx < (int)ps.annots.size())
            ps.annots.erase(ps.annots.begin() + act.annotIdx);
        break;
    case ActionKind::RemoveAnnot:
        ps.annots.insert(ps.annots.begin() + act.annotIdx, act.before);
        break;
    case ActionKind::MoveAnnot:
    case ActionKind::ModifyAnnot:
        if (act.annotIdx < (int)ps.annots.size())
            ps.annots[act.annotIdx] = act.before;
        break;
    }
    G.redoStack.push(act);
    G.isModified = true;
    UpdateTitle();
    PostMessageW(G.hwndMain, APP_REPAINT_CANVAS, 0, 0);
}

static void DoRedo() {
    if (G.redoStack.empty()) return;
    UndoAction act = G.redoStack.top(); G.redoStack.pop();
    PageState& ps = G.pages[act.pageIdx];
    switch (act.kind) {
    case ActionKind::AddAnnot:
        ps.annots.insert(ps.annots.begin() + act.annotIdx, act.after);
        break;
    case ActionKind::RemoveAnnot:
        if (act.annotIdx < (int)ps.annots.size())
            ps.annots.erase(ps.annots.begin() + act.annotIdx);
        break;
    case ActionKind::MoveAnnot:
    case ActionKind::ModifyAnnot:
        if (act.annotIdx < (int)ps.annots.size())
            ps.annots[act.annotIdx] = act.after;
        break;
    }
    G.undoStack.push(act);
    G.isModified = true;
    UpdateTitle();
    PostMessageW(G.hwndMain, APP_REPAINT_CANVAS, 0, 0);
}

// =============================================================================
//  SECTION 6 – PAGE CACHE & RENDERING
// =============================================================================

// Invalidate cache entry for a given page
static void InvalidateCache(int pageIdx) {
    for (int i = 0; i < AppState::CACHE_SIZE; i++) {
        if (G.cache[i].pageIdx == pageIdx) {
            delete G.cache[i].bmp;
            G.cache[i].bmp = nullptr;
            G.cache[i].pageIdx = -1;
        }
    }
}

// Render a PDF page to a GDI+ Bitmap at given zoom%
// Returns newly-allocated Bitmap (caller does NOT delete – cache owns it)
static Bitmap* RenderPageToBitmap(int pageIdx, int zoom, int extraRot) {
    if (!G.pdfDoc || pageIdx < 0 || pageIdx >= G.pageCount)
        return nullptr;

    FPDF_PAGE page = FPDF_LoadPage(G.pdfDoc, pageIdx);
    if (!page) return nullptr;

    double pageW = FPDF_GetPageWidth(page);
    double pageH = FPDF_GetPageHeight(page);
    float  scale = zoom / 100.0f * G.dpiScale;
    int    bmpW  = (int)(pageW * scale);
    int    bmpH  = (int)(pageH * scale);
    if (bmpW < 1) bmpW = 1;
    if (bmpH < 1) bmpH = 1;

    // PDFium RGBA buffer
    FPDF_BITMAP pdfBmp = FPDFBitmap_Create(bmpW, bmpH, 1);
    FPDFBitmap_FillRect(pdfBmp, 0, 0, bmpW, bmpH, 0xFFFFFFFF);

    int rot = 0;
    switch (extraRot) {
    case 90:  rot = 1; break;
    case 180: rot = 2; break;
    case 270: rot = 3; break;
    default:  rot = 0; break;
    }

    FPDF_RenderPageBitmap(pdfBmp, page, 0, 0, bmpW, bmpH, rot,
                          FPDF_ANNOT | FPDF_LCD_TEXT | FPDF_NO_CATCH);

    // Copy PDFium buffer → GDI+ Bitmap
    Bitmap* result = new Bitmap(bmpW, bmpH, PixelFormat32bppARGB);
    BitmapData bmpData;
    Rect lockRc(0, 0, bmpW, bmpH);
    result->LockBits(&lockRc, ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

    BYTE* src  = (BYTE*)FPDFBitmap_GetBuffer(pdfBmp);
    BYTE* dst  = (BYTE*)bmpData.Scan0;
    int   stride = FPDFBitmap_GetStride(pdfBmp);

    for (int row = 0; row < bmpH; row++) {
        BYTE* s = src + row * stride;
        BYTE* d = dst + row * bmpData.Stride;
        for (int col = 0; col < bmpW; col++) {
            // PDFium: BGRA (on LE) → GDI+ ARGB
            d[0] = s[0]; // B
            d[1] = s[1]; // G
            d[2] = s[2]; // R
            d[3] = s[3]; // A
            s += 4; d += 4;
        }
    }

    result->UnlockBits(&bmpData);
    FPDFBitmap_Destroy(pdfBmp);
    FPDF_ClosePage(page);

    return result;
}

// Get (or render) cached bitmap for current page
static Bitmap* GetPageBitmap(int pageIdx) {
    int rot = (G.pages.empty() ? 0 : G.pages[pageIdx].rotation);

    // Check cache
    for (int i = 0; i < AppState::CACHE_SIZE; i++) {
        auto& c = G.cache[i];
        if (c.pageIdx == pageIdx && c.zoom == G.zoom && c.rotation == rot)
            return c.bmp;
    }

    // Evict oldest cache slot
    AppState::PageCache& slot = G.cache[G.cacheHead % AppState::CACHE_SIZE];
    G.cacheHead++;
    delete slot.bmp;

    slot.bmp      = RenderPageToBitmap(pageIdx, G.zoom, rot);
    slot.pageIdx  = pageIdx;
    slot.zoom     = G.zoom;
    slot.rotation = rot;
    return slot.bmp;
}

// Render small thumbnail for sidebar
static Bitmap* RenderThumb(int pageIdx) {
    if (!G.pdfDoc) return nullptr;
    FPDF_PAGE page = FPDF_LoadPage(G.pdfDoc, pageIdx);
    if (!page) return nullptr;

    double pw = FPDF_GetPageWidth(page);
    double ph = FPDF_GetPageHeight(page);
    float scale = min((float)THUMB_W / (float)pw, (float)THUMB_H / (float)ph);
    int tw = (int)(pw * scale);
    int th = (int)(ph * scale);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;

    FPDF_BITMAP fb = FPDFBitmap_Create(tw, th, 1);
    FPDFBitmap_FillRect(fb, 0, 0, tw, th, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(fb, page, 0, 0, tw, th, 0, FPDF_ANNOT | FPDF_LCD_TEXT);

    Bitmap* bmp = new Bitmap(tw, th, PixelFormat32bppARGB);
    BitmapData bd;
    Rect rc(0, 0, tw, th);
    bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &bd);
    BYTE* src = (BYTE*)FPDFBitmap_GetBuffer(fb);
    BYTE* dst = (BYTE*)bd.Scan0;
    int st = FPDFBitmap_GetStride(fb);
    for (int r = 0; r < th; r++) {
        memcpy(dst + r * bd.Stride, src + r * st, tw * 4);
    }
    bmp->UnlockBits(&bd);
    FPDFBitmap_Destroy(fb);
    FPDF_ClosePage(page);
    return bmp;
}

// Rebuild all thumbnails (called after open/close)
static void RebuildThumbs() {
    for (auto* b : G.thumbs) delete b;
    G.thumbs.clear();
    for (int i = 0; i < G.pageCount; i++) {
        G.thumbs.push_back(RenderThumb(i));
    }
    InvalidateRect(G.hwndSidebar, nullptr, FALSE);
}

// =============================================================================
//  SECTION 7 – FILE OPEN / SAVE
// =============================================================================

static void CloseDocument() {
    // Free cache
    for (int i = 0; i < AppState::CACHE_SIZE; i++) {
        delete G.cache[i].bmp;
        G.cache[i] = {};
    }
    for (auto* b : G.thumbs) delete b;
    G.thumbs.clear();

    if (G.pdfForm) {
        FPDF_FFLDraw(G.pdfForm, nullptr, nullptr, 0, 0, 0, 0, 0, 0);
        FPDFDOC_ExitFormFillEnvironment(G.pdfForm);
        G.pdfForm = nullptr;
    }
    if (G.pdfDoc) {
        FPDF_CloseDocument(G.pdfDoc);
        G.pdfDoc = nullptr;
    }
    G.pageCount   = 0;
    G.currentPage = 0;
    G.pages.clear();
    G.filePath.clear();
    G.isModified  = false;
    while (!G.undoStack.empty()) G.undoStack.pop();
    while (!G.redoStack.empty()) G.redoStack.pop();
    G.scrollX = G.scrollY = 0;
    G.thumbScrollY = 0;
    G.selectedAnnotIdx = G.selectedAnnotPage = -1;
    UpdateTitle();
}

static bool OpenDocument(const wstring& path) {
    CloseDocument();

    // Convert to UTF-8 for PDFium
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string utf8path(utf8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8path[0], utf8len, nullptr, nullptr);

    G.pdfDoc = FPDF_LoadDocument(utf8path.c_str(), nullptr);
    if (!G.pdfDoc) {
        unsigned long err = FPDF_GetLastError();
        wstring msg = L"Cannot open PDF.\nError code: " + to_wstring(err);
        MessageBoxW(G.hwndMain, msg.c_str(), L"Open Failed", MB_ICONERROR);
        return false;
    }

    // Form support
    FPDF_FORMFILLINFO ffi = {};
    ffi.version = 2;
    G.pdfForm = FPDFDOC_InitFormFillEnvironment(G.pdfDoc, &ffi);

    G.pageCount = FPDF_GetPageCount(G.pdfDoc);
    G.pages.resize(G.pageCount);
    G.currentPage = 0;
    G.filePath = path;
    G.isModified = false;

    // Reset view
    G.zoom = 100;
    G.scrollX = G.scrollY = 0;

    UpdateTitle();
    RebuildThumbs();
    SetStatus(L"Opened: " + path.substr(path.rfind(L'\\') + 1)
              + L"  |  " + ItoW(G.pageCount) + L" page(s)");

    PostMessageW(G.hwndMain, APP_REPAINT_CANVAS,  0, 0);
    PostMessageW(G.hwndMain, APP_REPAINT_SIDEBAR, 0, 0);
    PostMessageW(G.hwndMain, APP_PAGE_CHANGED,    0, 0);
    return true;
}

// PDFium write-block callback for FPDF_SaveAsCopy
struct SaveContext { FILE* fp; };
static int PdfSaveWrite(FPDF_FILEWRITE* pThis, const void* buf, unsigned long size) {
    SaveContext* ctx = (SaveContext*)((BYTE*)pThis - offsetof(SaveContext, fp) + offsetof(SaveContext, fp));
    // simpler: just cast
    FILE* fp = *(FILE**)((BYTE*)pThis + sizeof(FPDF_FILEWRITE));
    fwrite(buf, 1, size, fp);
    return 1;
}

struct MyFileWrite {
    FPDF_FILEWRITE base;
    FILE*          fp;
};
static int MyWriteBlock(FPDF_FILEWRITE* pThis, const void* buf, unsigned long size) {
    MyFileWrite* mfw = (MyFileWrite*)pThis;
    fwrite(buf, 1, size, mfw->fp);
    return 1;
}

static bool SaveDocument(const wstring& path) {
    if (!G.pdfDoc) return false;

    // 1) Flatten annotations from our list back to PDFium
    //    (Basic approach: we skip re-injecting and just save as-is with PDFium's own annots;
    //     our overlay annotations will be flattened via a separate GDI+ render-to-PDF pass
    //     in a production version.  Here we demonstrate the save infrastructure.)

    FILE* fp = nullptr;
    _wfopen_s(&fp, path.c_str(), L"wb");
    if (!fp) {
        MessageBoxW(G.hwndMain, L"Cannot write to file.", L"Save Error", MB_ICONERROR);
        return false;
    }

    MyFileWrite mfw;
    mfw.base.version    = 1;
    mfw.base.WriteBlock = MyWriteBlock;
    mfw.fp              = fp;

    int flags = FPDF_NO_INCREMENTAL;
    bool ok   = (FPDF_SaveAsCopy(G.pdfDoc, &mfw.base, flags) != 0);
    fclose(fp);

    if (ok) {
        G.filePath   = path;
        G.isModified = false;
        UpdateTitle();
        SetStatus(L"Saved: " + path.substr(path.rfind(L'\\') + 1));
    } else {
        MessageBoxW(G.hwndMain, L"PDFium save failed.", L"Save Error", MB_ICONERROR);
    }
    return ok;
}

// Open-file dialog
static wstring ShowOpenDialog(HWND hwnd) {
    OPENFILENAMEW ofn = {};
    wchar_t buf[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Open PDF";
    if (GetOpenFileNameW(&ofn)) return buf;
    return {};
}

// Save-file dialog
static wstring ShowSaveDialog(HWND hwnd, const wstring& defaultName) {
    OPENFILENAMEW ofn = {};
    wchar_t buf[MAX_PATH] = {};
    if (!defaultName.empty())
        wcsncpy_s(buf, defaultName.c_str(), MAX_PATH - 1);
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd;
    ofn.lpstrFilter  = L"PDF Files (*.pdf)\0*.pdf\0";
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle   = L"Save PDF As";
    ofn.lpstrDefExt  = L"pdf";
    if (GetSaveFileNameW(&ofn)) return buf;
    return {};
}

// =============================================================================
//  SECTION 8 – ANNOTATION DRAWING (GDI+)
// =============================================================================

// Draw a single annotation onto a GDI+ Graphics context
// coords already in screen-space (caller applies scale/scroll transform)
static void DrawAnnotation(Graphics& g, const Annotation& a, float scale) {
    BYTE alpha = (BYTE)(a.opacity * 255.0f);
    Color col  = GdipColor(a.color, alpha);
    Color col_opaque = GdipColor(a.color, 255);

    float sx = a.bounds.x * scale - G.scrollX;
    float sy = a.bounds.y * scale - G.scrollY;
    float sw = a.bounds.w * scale;
    float sh = a.bounds.h * scale;

    switch (a.kind) {
    // ── Highlight ───────────────────────────────────────────────────────────
    case AnnotKind::Highlight: {
        SolidBrush br(col);
        g.FillRectangle(&br, sx, sy, sw, sh);
        break;
    }
    // ── Underline ───────────────────────────────────────────────────────────
    case AnnotKind::Underline: {
        Pen pen(col_opaque, max(1.0f, a.strokeWidth));
        g.DrawLine(&pen, sx, sy + sh, sx + sw, sy + sh);
        break;
    }
    // ── Strikethrough ───────────────────────────────────────────────────────
    case AnnotKind::Strikethrough: {
        Pen pen(col_opaque, max(1.0f, a.strokeWidth));
        g.DrawLine(&pen, sx, sy + sh * 0.5f, sx + sw, sy + sh * 0.5f);
        break;
    }
    // ── Ink (free draw) ─────────────────────────────────────────────────────
    case AnnotKind::Ink: {
        for (const auto& stroke : a.strokes) {
            if (stroke.pts.size() < 2) continue;
            Color sc = GdipColor(stroke.color, (BYTE)(stroke.opacity * 255));
            Pen pen(sc, stroke.width);
            pen.SetLineJoin(LineJoinRound);
            pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            for (int i = 1; i < (int)stroke.pts.size(); i++) {
                float x1 = stroke.pts[i-1].x * scale - G.scrollX;
                float y1 = stroke.pts[i-1].y * scale - G.scrollY;
                float x2 = stroke.pts[i  ].x * scale - G.scrollX;
                float y2 = stroke.pts[i  ].y * scale - G.scrollY;
                g.DrawLine(&pen, x1, y1, x2, y2);
            }
        }
        break;
    }
    // ── Rectangle ───────────────────────────────────────────────────────────
    case AnnotKind::Rectangle: {
        SolidBrush br(col);
        g.FillRectangle(&br, sx, sy, sw, sh);
        Pen pen(col_opaque, a.strokeWidth);
        g.DrawRectangle(&pen, sx, sy, sw, sh);
        break;
    }
    // ── Ellipse ─────────────────────────────────────────────────────────────
    case AnnotKind::Ellipse: {
        SolidBrush br(col);
        g.FillEllipse(&br, sx, sy, sw, sh);
        Pen pen(col_opaque, a.strokeWidth);
        g.DrawEllipse(&pen, sx, sy, sw, sh);
        break;
    }
    // ── Arrow ────────────────────────────────────────────────────────────────
    case AnnotKind::Arrow: {
        float x1 = a.arrowStart.x * scale - G.scrollX;
        float y1 = a.arrowStart.y * scale - G.scrollY;
        float x2 = a.arrowEnd.x   * scale - G.scrollX;
        float y2 = a.arrowEnd.y   * scale - G.scrollY;
        Pen pen(col_opaque, a.strokeWidth);
        pen.SetCustomEndCap([&]() -> CustomLineCap* {
            // Arrow head
            GraphicsPath path;
            PointF arHead[] = { {0,-5},{-3,0},{3,0} };
            path.AddPolygon(arHead, 3);
            return new CustomLineCap(nullptr, &path);
        }());
        g.DrawLine(&pen, x1, y1, x2, y2);
        break;
    }
    // ── StickyNote ──────────────────────────────────────────────────────────
    case AnnotKind::StickyNote: {
        Color noteYellow(220, 255, 235, 50);
        Color noteBorder(255, 200, 180, 0);
        SolidBrush br(noteYellow);
        Pen pen(noteBorder, 1.5f);
        g.FillRectangle(&br, sx, sy, sw, sh);
        g.DrawRectangle(&pen, sx, sy, sw, sh);
        // draw fold in corner
        Pen foldPen(noteBorder, 1.0f);
        float fold = min(12.0f, sw * 0.2f);
        g.DrawLine(&foldPen, sx + sw - fold, sy, sx + sw - fold, sy + fold);
        g.DrawLine(&foldPen, sx + sw - fold, sy + fold, sx + sw,  sy + fold);
        // text
        if (!a.text.empty()) {
            FontFamily ff(L"Segoe UI");
            Font fnt(&ff, 9.5f * G.dpiScale, FontStyleRegular, UnitPixel);
            SolidBrush tb(Color(220,40,30,10));
            RectF tr(sx+4, sy+4, sw-8, sh-8);
            StringFormat sf;
            sf.SetTrimming(StringTrimmingEllipsisWord);
            g.DrawString(a.text.c_str(), -1, &fnt, tr, &sf, &tb);
        }
        break;
    }
    // ── Stamp ────────────────────────────────────────────────────────────────
    case AnnotKind::Stamp: {
        Color border(200, GetRValue(a.color), GetGValue(a.color), GetBValue(a.color));
        Pen pen(border, 3.0f);
        g.DrawRectangle(&pen, sx+3, sy+3, sw-6, sh-6);
        FontFamily ff(L"Impact");
        float fs = min(sh * 0.55f, sw / max(1.0f, (float)a.text.size() * 0.65f));
        Font fnt(&ff, fs, FontStyleRegular, UnitPixel);
        SolidBrush tb(border);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        RectF tr(sx, sy, sw, sh);
        g.DrawString(a.text.c_str(), -1, &fnt, tr, &sf, &tb);
        break;
    }
    // ── Redact ───────────────────────────────────────────────────────────────
    case AnnotKind::Redact: {
        SolidBrush br(Color(255, 0, 0, 0));
        g.FillRectangle(&br, sx, sy, sw, sh);
        break;
    }
    // ── WhiteOut ─────────────────────────────────────────────────────────────
    case AnnotKind::WhiteOut: {
        SolidBrush br(Color(255, 255, 255, 255));
        g.FillRectangle(&br, sx, sy, sw, sh);
        Pen pen(Color(200, 200, 200, 200), 0.5f);
        g.DrawRectangle(&pen, sx, sy, sw, sh);
        break;
    }
    // ── Text overlay ─────────────────────────────────────────────────────────
    case AnnotKind::Text: {
        if (a.text.empty()) break;
        FontFamily ff(L"Segoe UI");
        float fs = max(8.0f, a.strokeWidth * scale);
        Font fnt(&ff, fs, FontStyleRegular, UnitPixel);
        SolidBrush tb(col_opaque);
        RectF tr(sx, sy, sw == 0 ? 400.0f : sw, sh == 0 ? 200.0f : sh);
        StringFormat sf;
        sf.SetTrimming(StringTrimmingNone);
        g.DrawString(a.text.c_str(), -1, &fnt, tr, &sf, &tb);
        break;
    }
    // ── Signature ────────────────────────────────────────────────────────────
    case AnnotKind::Signature: {
        for (const auto& stroke : a.strokes) {
            if (stroke.pts.size() < 2) continue;
            Pen pen(Color(255, 0, 0, 0), stroke.width);
            pen.SetLineJoin(LineJoinRound);
            for (int i = 1; i < (int)stroke.pts.size(); i++) {
                float x1 = (a.bounds.x + stroke.pts[i-1].x) * scale - G.scrollX;
                float y1 = (a.bounds.y + stroke.pts[i-1].y) * scale - G.scrollY;
                float x2 = (a.bounds.x + stroke.pts[i  ].x) * scale - G.scrollX;
                float y2 = (a.bounds.y + stroke.pts[i  ].y) * scale - G.scrollY;
                g.DrawLine(&pen, x1, y1, x2, y2);
            }
        }
        // dotted border
        Pen dashed(Color(120,0,0,200), 1.0f);
        dashed.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&dashed, sx, sy, sw, sh);
        break;
    }
    default: break;
    }

    // Selection handles
    if (a.selected) {
        Pen sel(Color(255, 33, 150, 243), 1.5f);
        sel.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&sel, sx - 2, sy - 2, sw + 4, sh + 4);
        // 8 handle squares
        Color hc(255, 33, 150, 243);
        SolidBrush hbr(hc);
        float hs = 6.0f;
        float hpts[][2] = {
            {sx-hs/2, sy-hs/2}, {sx+sw/2-hs/2, sy-hs/2}, {sx+sw-hs/2, sy-hs/2},
            {sx-hs/2, sy+sh/2-hs/2}, {sx+sw-hs/2, sy+sh/2-hs/2},
            {sx-hs/2, sy+sh-hs/2}, {sx+sw/2-hs/2, sy+sh-hs/2}, {sx+sw-hs/2, sy+sh-hs/2},
        };
        for (auto& hp : hpts)
            g.FillRectangle(&hbr, hp[0], hp[1], hs, hs);
    }
}

// Draw all annotations for the current page
static void DrawPageAnnotations(Graphics& g) {
    if (G.pdfDoc == nullptr || G.pages.empty()) return;
    float scale = G.zoom / 100.0f * G.dpiScale;
    const PageState& ps = G.pages[G.currentPage];
    for (const auto& a : ps.annots) {
        DrawAnnotation(g, a, scale);
    }

    // Draw in-progress ink stroke
    if (G.isDrawing && G.activeTool == Tool::Draw && !G.currentStroke.pts.empty()) {
        Color sc = GdipColor(G.currentStroke.color,
                             (BYTE)(G.currentStroke.opacity * 255));
        Pen pen(sc, G.currentStroke.width);
        pen.SetLineJoin(LineJoinRound);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        const auto& pts = G.currentStroke.pts;
        for (int i = 1; i < (int)pts.size(); i++) {
            float x1 = pts[i-1].x * scale - G.scrollX;
            float y1 = pts[i-1].y * scale - G.scrollY;
            float x2 = pts[i  ].x * scale - G.scrollX;
            float y2 = pts[i  ].y * scale - G.scrollY;
            g.DrawLine(&pen, x1, y1, x2, y2);
        }
    }

    // Draw in-progress rectangle / ellipse / arrow / highlight etc.
    if (G.annotStarted && G.isDrawing) {
        DrawAnnotation(g, G.currentAnnot, scale);
    }

    // Measure tool line
    if (G.activeTool == Tool::Measure && G.measureStarted) {
        float x1 = G.measureP1.x * scale - G.scrollX;
        float y1 = G.measureP1.y * scale - G.scrollY;
        float x2 = G.measureP2.x * scale - G.scrollX;
        float y2 = G.measureP2.y * scale - G.scrollY;
        Pen pen(Color(255,33,150,243), 2.0f);
        pen.SetDashStyle(DashStyleDot);
        g.DrawLine(&pen, x1, y1, x2, y2);
        float dx = G.measureP2.x - G.measureP1.x;
        float dy = G.measureP2.y - G.measureP1.y;
        float dist = sqrtf(dx*dx + dy*dy);
        wstring label = to_wstring((int)dist) + L" pt";
        FontFamily ff(L"Segoe UI");
        Font fnt(&ff, 11.0f, FontStyleBold, UnitPixel);
        SolidBrush br(Color(255,33,150,243));
        g.DrawString(label.c_str(), -1, &fnt, PointF(x2+4, y2-14), &br);
    }
}

// =============================================================================
//  SECTION 9 – CANVAS WINDOW PROC
// =============================================================================

static void OnCanvasPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

        // Background
        SolidBrush bgBr(Color(255, 42, 42, 52));
        g.FillRectangle(&bgBr, 0, 0, cw, ch);

        if (!G.pdfDoc) {
            // Empty state
            FontFamily ff(L"Segoe UI");
            Font fnt(&ff, 16.0f, FontStyleRegular, UnitPixel);
            SolidBrush tb(Color(120,180,180,200));
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            RectF tr(0, 0, (REAL)cw, (REAL)ch);
            g.DrawString(L"Open a PDF file to begin editing", -1,
                         &fnt, tr, &sf, &tb);
        } else {
            // Page drop shadow
            Bitmap* bmp = GetPageBitmap(G.currentPage);
            if (bmp) {
                float scale = G.zoom / 100.0f * G.dpiScale;
                int pw = bmp->GetWidth();
                int ph = bmp->GetHeight();
                int drawX = -G.scrollX;
                int drawY = -G.scrollY;

                // Shadow
                SolidBrush shadow(Color(80, 0, 0, 0));
                g.FillRectangle(&shadow, drawX+6, drawY+6, pw, ph);

                // Page
                g.DrawImage(bmp, drawX, drawY, pw, ph);

                // Overlay annotations
                DrawPageAnnotations(g);
            }
        }
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// Hit-test annotations on current page; returns index or -1
static int HitTestAnnot(int mx, int my) {
    if (G.pages.empty()) return -1;
    const auto& annots = G.pages[G.currentPage].annots;
    // iterate in reverse (top-most first)
    for (int i = (int)annots.size()-1; i >= 0; i--) {
        RECT sr = AnnotScreenRect(annots[i]);
        if (mx >= sr.left && mx <= sr.right &&
            my >= sr.top  && my <= sr.bottom)
            return i;
    }
    return -1;
}

// Deselect all annotations
static void DeselectAll() {
    if (!G.pages.empty()) {
        for (auto& a : G.pages[G.currentPage].annots)
            a.selected = false;
    }
    G.selectedAnnotPage = G.selectedAnnotIdx = -1;
}

// Commit a finished annotation
static void CommitAnnotation(Annotation a) {
    if (!G.pdfDoc) return;
    int pg = G.currentPage;
    int idx = (int)G.pages[pg].annots.size();
    G.pages[pg].annots.push_back(a);
    UndoAction act;
    act.kind      = ActionKind::AddAnnot;
    act.pageIdx   = pg;
    act.annotIdx  = idx;
    act.after     = a;
    PushUndo(act);
    InvalidateRect(G.hwndCanvas, nullptr, FALSE);
}

// Delete selected annotation
static void DeleteSelectedAnnot() {
    if (G.selectedAnnotIdx < 0 || G.selectedAnnotPage < 0) return;
    int pg  = G.selectedAnnotPage;
    int idx = G.selectedAnnotIdx;
    auto& annots = G.pages[pg].annots;
    if (idx >= (int)annots.size()) return;
    UndoAction act;
    act.kind     = ActionKind::RemoveAnnot;
    act.pageIdx  = pg;
    act.annotIdx = idx;
    act.before   = annots[idx];
    annots.erase(annots.begin() + idx);
    PushUndo(act);
    G.selectedAnnotIdx = G.selectedAnnotPage = -1;
    InvalidateRect(G.hwndCanvas, nullptr, FALSE);
}

// Build a fresh Annotation template from current tool settings
static Annotation MakeAnnotTemplate(AnnotKind kind) {
    Annotation a{};
    a.kind        = kind;
    a.color       = G.activeColor;
    a.strokeWidth = G.strokeWidth;
    a.opacity     = G.opacity;
    a.selected    = false;
    a.rotation    = 0;
    return a;
}

// Update scrollbars after zoom/page change
static void UpdateScrollbars(HWND hwnd) {
    if (!G.pdfDoc) return;
    FPDF_PAGE page = FPDF_LoadPage(G.pdfDoc, G.currentPage);
    if (!page) return;
    float scale = G.zoom / 100.0f * G.dpiScale;
    int pw = (int)(FPDF_GetPageWidth(page)  * scale);
    int ph = (int)(FPDF_GetPageHeight(page) * scale);
    FPDF_ClosePage(page);

    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;

    si.nMin  = 0; si.nMax = pw; si.nPage = cw; si.nPos = G.scrollX;
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);

    si.nMin  = 0; si.nMax = ph; si.nPage = ch; si.nPos = G.scrollY;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    // ── Paint ──────────────────────────────────────────────────────────────
    case WM_PAINT:
        OnCanvasPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;

    // ── Mouse wheel = scroll / zoom ─────────────────────────────────────
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            // Ctrl+Wheel → Zoom
            int step = (delta > 0) ? 10 : -10;
            G.zoom = Clamp(G.zoom + step, MIN_ZOOM, MAX_ZOOM);
            InvalidateCache(G.currentPage);
            UpdateScrollbars(hwnd);
            SetStatus(L"Zoom: " + ZoomLabel());
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            // Normal scroll
            G.scrollY -= delta / 3;
            G.scrollY  = max(0, G.scrollY);
            SetScrollPos(hwnd, SB_VERT, G.scrollY, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    // ── Horizontal scroll ───────────────────────────────────────────────
    case WM_HSCROLL: {
        int code = LOWORD(wp);
        SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_ALL;
        GetScrollInfo(hwnd, SB_HORZ, &si);
        switch (code) {
        case SB_LEFT:         si.nPos = si.nMin; break;
        case SB_RIGHT:        si.nPos = si.nMax; break;
        case SB_LINELEFT:     si.nPos -= SCROLL_STEP; break;
        case SB_LINERIGHT:    si.nPos += SCROLL_STEP; break;
        case SB_PAGELEFT:     si.nPos -= si.nPage; break;
        case SB_PAGERIGHT:    si.nPos += si.nPage; break;
        case SB_THUMBTRACK:   si.nPos  = si.nTrackPos; break;
        }
        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
        GetScrollInfo(hwnd, SB_HORZ, &si);
        G.scrollX = si.nPos;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_VSCROLL: {
        int code = LOWORD(wp);
        SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        switch (code) {
        case SB_TOP:          si.nPos = si.nMin; break;
        case SB_BOTTOM:       si.nPos = si.nMax; break;
        case SB_LINEUP:       si.nPos -= SCROLL_STEP; break;
        case SB_LINEDOWN:     si.nPos += SCROLL_STEP; break;
        case SB_PAGEUP:       si.nPos -= si.nPage; break;
        case SB_PAGEDOWN:     si.nPos += si.nPage; break;
        case SB_THUMBTRACK:   si.nPos  = si.nTrackPos; break;
        }
        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si);
        G.scrollY = si.nPos;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // ── Mouse Button Down ───────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        float scale = G.zoom / 100.0f * G.dpiScale;
        Vec2 pagePt = ScreenToPage(mx, my, 0, 0);

        switch (G.activeTool) {
        case Tool::Hand:
            G.isPanning   = true;
            G.panStart    = {mx, my};
            G.panScrollX0 = G.scrollX;
            G.panScrollY0 = G.scrollY;
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            break;

        case Tool::Select: {
            DeselectAll();
            int hi = HitTestAnnot(mx, my);
            if (hi >= 0) {
                G.selectedAnnotPage = G.currentPage;
                G.selectedAnnotIdx  = hi;
                G.pages[G.currentPage].annots[hi].selected = true;
                G.isDraggingAnnot  = true;
                RECT sr = AnnotScreenRect(G.pages[G.currentPage].annots[hi]);
                G.dragAnnotOffset = {mx - sr.left, my - sr.top};
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }

        case Tool::Draw:
            G.isDrawing = true;
            G.currentStroke = {};
            G.currentStroke.color   = G.activeColor;
            G.currentStroke.width   = G.strokeWidth;
            G.currentStroke.opacity = G.opacity;
            G.currentStroke.pts.push_back(pagePt);
            break;

        case Tool::Eraser: {
            // Hit-test and remove
            int hi = HitTestAnnot(mx, my);
            if (hi >= 0) {
                UndoAction act;
                act.kind     = ActionKind::RemoveAnnot;
                act.pageIdx  = G.currentPage;
                act.annotIdx = hi;
                act.before   = G.pages[G.currentPage].annots[hi];
                G.pages[G.currentPage].annots.erase(
                    G.pages[G.currentPage].annots.begin() + hi);
                PushUndo(act);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }

        // Box-drawing tools
        case Tool::Highlight:
        case Tool::Underline:
        case Tool::Strikethrough:
        case Tool::Rectangle:
        case Tool::Ellipse:
        case Tool::Stamp:
        case Tool::Redact:
        case Tool::WhiteOut:
        case Tool::StickyNote: {
            G.isDrawing    = true;
            G.annotStarted = true;
            G.annotAnchor  = {mx, my};
            AnnotKind kind = AnnotKind::Highlight;
            switch (G.activeTool) {
            case Tool::Highlight:      kind = AnnotKind::Highlight;     break;
            case Tool::Underline:      kind = AnnotKind::Underline;     break;
            case Tool::Strikethrough:  kind = AnnotKind::Strikethrough; break;
            case Tool::Rectangle:      kind = AnnotKind::Rectangle;     break;
            case Tool::Ellipse:        kind = AnnotKind::Ellipse;       break;
            case Tool::Stamp:          kind = AnnotKind::Stamp;         break;
            case Tool::Redact:         kind = AnnotKind::Redact;        break;
            case Tool::WhiteOut:       kind = AnnotKind::WhiteOut;      break;
            case Tool::StickyNote:     kind = AnnotKind::StickyNote;    break;
            default: break;
            }
            G.currentAnnot = MakeAnnotTemplate(kind);
            if (kind == AnnotKind::Stamp)
                G.currentAnnot.text = STAMPS[G.stampIndex];
            G.currentAnnot.bounds = {pagePt.x, pagePt.y, 0, 0};
            break;
        }

        case Tool::Arrow:
            G.isDrawing    = true;
            G.annotStarted = true;
            G.annotAnchor  = {mx, my};
            G.currentAnnot = MakeAnnotTemplate(AnnotKind::Arrow);
            G.currentAnnot.arrowStart = pagePt;
            G.currentAnnot.arrowEnd   = pagePt;
            G.currentAnnot.bounds = {pagePt.x, pagePt.y, 0, 0};
            break;

        case Tool::Measure:
            if (!G.measureStarted) {
                G.measureStarted = true;
                G.measureP1 = pagePt;
                G.measureP2 = pagePt;
            } else {
                G.measureP2 = pagePt;
                float dx = G.measureP2.x - G.measureP1.x;
                float dy = G.measureP2.y - G.measureP1.y;
                float d  = sqrtf(dx*dx+dy*dy);
                SetStatus(L"Measurement: " + to_wstring((int)d) + L" pt  ("
                          + to_wstring((int)(d*0.352f)) + L" mm)");
                G.measureStarted = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case Tool::Text:
            // We'll show a small popup to type text
            {
                G.pendingTextBounds = {pagePt.x, pagePt.y, 300.0f/scale, 40.0f/scale};
                // Simple InputBox via MessageBox not ideal; post a custom dialog
                // For now: placeholder – production would show a floating EDIT control
                wchar_t buf[512] = {};
                // Use a simple dialog (see Section 12)
                PostMessageW(G.hwndMain, WM_COMMAND, ID_TOOL_TEXT, (LPARAM)hwnd);
            }
            break;

        case Tool::Signature:
            // Place stored signature at clicked position
            if (G.signature.hasData) {
                Annotation a = MakeAnnotTemplate(AnnotKind::Signature);
                a.strokes = G.signature.strokes;
                a.bounds  = {pagePt.x, pagePt.y, 120.0f/scale, 60.0f/scale};
                CommitAnnotation(a);
            } else {
                SetStatus(L"Draw your signature first — use the Signature Pad (Ctrl+G).");
            }
            break;

        default: break;
        }
        return 0;
    }

    // ── Mouse Move ────────────────────────────────────────────────────────
    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        float scale = G.zoom / 100.0f * G.dpiScale;
        Vec2 pagePt = ScreenToPage(mx, my, 0, 0);

        if (G.isPanning) {
            G.scrollX = G.panScrollX0 - (mx - G.panStart.x);
            G.scrollY = G.panScrollY0 - (my - G.panStart.y);
            G.scrollX = max(0, G.scrollX);
            G.scrollY = max(0, G.scrollY);
            SetScrollPos(hwnd, SB_HORZ, G.scrollX, TRUE);
            SetScrollPos(hwnd, SB_VERT, G.scrollY, TRUE);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (G.isDraggingAnnot && G.selectedAnnotIdx >= 0) {
            Annotation& a = G.pages[G.currentPage].annots[G.selectedAnnotIdx];
            RECT sr = AnnotScreenRect(a);
            int newLeft = mx - G.dragAnnotOffset.x;
            int newTop  = my - G.dragAnnotOffset.y;
            a.bounds.x = (newLeft + G.scrollX) / scale;
            a.bounds.y = (newTop  + G.scrollY) / scale;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (G.isDrawing) {
            if (G.activeTool == Tool::Draw) {
                G.currentStroke.pts.push_back(pagePt);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            else if (G.annotStarted) {
                float ax = G.annotAnchor.x, ay = G.annotAnchor.y;
                float px = pagePt.x,        py = pagePt.y;
                float bx = min(ax, px) * scale - G.scrollX;
                float by = min(ay, py) * scale - G.scrollY;
                float bw = fabsf(px - ax);
                float bh = fabsf(py - ay);

                if (G.currentAnnot.kind == AnnotKind::Arrow) {
                    G.currentAnnot.arrowEnd = pagePt;
                    G.currentAnnot.bounds.w = bw;
                    G.currentAnnot.bounds.h = bh;
                } else {
                    G.currentAnnot.bounds = {
                        min(pagePt.x, G.annotAnchor.x / scale * scale) / scale * scale,
                        min(pagePt.y, G.annotAnchor.y / scale * scale) / scale * scale,
                        bw, bh
                    };
                    G.currentAnnot.bounds.x = min(pagePt.x, (float)G.annotAnchor.x/scale + (float)G.scrollX/scale);
                    G.currentAnnot.bounds.y = min(pagePt.y, (float)G.annotAnchor.y/scale + (float)G.scrollY/scale);
                    G.currentAnnot.bounds.w = bw;
                    G.currentAnnot.bounds.h = bh;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        else if (G.activeTool == Tool::Measure && G.measureStarted) {
            G.measureP2 = pagePt;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Update cursor
        switch (G.activeTool) {
        case Tool::Hand:     SetCursor(LoadCursorW(nullptr, G.isPanning ? IDC_SIZEALL : IDC_HAND)); break;
        case Tool::Select:   SetCursor(LoadCursorW(nullptr, IDC_ARROW)); break;
        case Tool::Draw:
        case Tool::Eraser:   SetCursor(LoadCursorW(nullptr, IDC_CROSS)); break;
        default:             SetCursor(LoadCursorW(nullptr, IDC_CROSS)); break;
        }
        return 0;
    }

    // ── Mouse Button Up ───────────────────────────────────────────────────
    case WM_LBUTTONUP: {
        ReleaseCapture();
        G.isPanning = false;

        if (G.isDraggingAnnot) {
            // Save moved position as undo
            if (G.selectedAnnotIdx >= 0) {
                UndoAction act;
                act.kind     = ActionKind::MoveAnnot;
                act.pageIdx  = G.currentPage;
                act.annotIdx = G.selectedAnnotIdx;
                act.after    = G.pages[G.currentPage].annots[G.selectedAnnotIdx];
                // (before would need to be saved at mousedown – simplified here)
                act.before   = act.after;
                PushUndo(act);
            }
            G.isDraggingAnnot = false;
        }

        if (G.isDrawing) {
            G.isDrawing = false;
            if (G.activeTool == Tool::Draw && !G.currentStroke.pts.empty()) {
                Annotation a = MakeAnnotTemplate(AnnotKind::Ink);
                a.strokes.push_back(G.currentStroke);
                // Compute bounds
                float minx=1e9,miny=1e9,maxx=-1e9,maxy=-1e9;
                for (auto& p : G.currentStroke.pts) {
                    minx=min(minx,p.x); miny=min(miny,p.y);
                    maxx=max(maxx,p.x); maxy=max(maxy,p.y);
                }
                a.bounds = {minx, miny, maxx-minx+1, maxy-miny+1};
                CommitAnnotation(a);
                G.currentStroke = {};
            }
            else if (G.annotStarted) {
                G.annotStarted = false;
                Annotation a = G.currentAnnot;
                // Ensure minimum size
                if (a.bounds.w < 2) a.bounds.w = 50;
                if (a.bounds.h < 2) a.bounds.h = 20;
                CommitAnnotation(a);
            }
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        // Context menu
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        int hi = HitTestAnnot(mx, my);
        if (hi >= 0) {
            DeselectAll();
            G.selectedAnnotIdx  = hi;
            G.selectedAnnotPage = G.currentPage;
            G.pages[G.currentPage].annots[hi].selected = true;
            InvalidateRect(hwnd, nullptr, FALSE);

            HMENU hCtx = CreatePopupMenu();
            AppendMenuW(hCtx, MF_STRING, 0xB001, L"Delete Annotation");
            AppendMenuW(hCtx, MF_STRING, 0xB002, L"Bring to Front");
            AppendMenuW(hCtx, MF_STRING, 0xB003, L"Send to Back");
            AppendMenuW(hCtx, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hCtx, MF_STRING, 0xB004, L"Properties…");

            POINT pt = {mx, my};
            ClientToScreen(hwnd, &pt);
            int cmd = TrackPopupMenu(hCtx, TPM_RETURNCMD|TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hCtx);

            if (cmd == 0xB001) DeleteSelectedAnnot();
            else if (cmd == 0xB002) {
                // Bring to front
                auto& annots = G.pages[G.currentPage].annots;
                Annotation a = annots[hi];
                annots.erase(annots.begin()+hi);
                annots.push_back(a);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            else if (cmd == 0xB003) {
                // Send to back
                auto& annots = G.pages[G.currentPage].annots;
                Annotation a = annots[hi];
                annots.erase(annots.begin()+hi);
                annots.insert(annots.begin(), a);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_DELETE) DeleteSelectedAnnot();
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// =============================================================================
//  SECTION 10 – SIDEBAR WINDOW PROC (Page Thumbnails)
// =============================================================================

static void OnSidebarPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        SolidBrush bgBr(Color(255, 30, 30, 38));
        g.FillRectangle(&bgBr, 0, 0, cw, ch);

        // Header
        Color hdrCol(255, 25, 25, 33);
        SolidBrush hdrBr(hdrCol);
        g.FillRectangle(&hdrBr, 0, 0, cw, S(28));
        FontFamily ff(L"Segoe UI");
        Font hdrFont(&ff, 10.5f, FontStyleBold, UnitPixel);
        SolidBrush hdrTxt(Color(180, 190, 190, 210));
        g.DrawString(L"PAGES", -1, &hdrFont, PointF(10.0f, 6.0f), &hdrTxt);

        int y = S(28) - G.thumbScrollY;
        int pad = 10;
        Font pageFont(&ff, 9.0f, FontStyleRegular, UnitPixel);

        for (int i = 0; i < G.pageCount; i++) {
            bool isCur = (i == G.currentPage);
            int tx = (cw - THUMB_W) / 2;
            int ty = y + pad;

            // Highlight current
            if (isCur) {
                SolidBrush sel(Color(60, 33, 150, 243));
                g.FillRectangle(&sel, 2, ty - 4, cw - 4, THUMB_H + 22);
                Pen selPen(Color(200, 33, 150, 243), 2.0f);
                g.DrawRectangle(&selPen, tx - 2, ty - 2, THUMB_W + 4, THUMB_H + 4);
            }

            // Thumbnail shadow
            SolidBrush shadow(Color(60, 0, 0, 0));
            g.FillRectangle(&shadow, tx + 3, ty + 3, THUMB_W, THUMB_H);

            // Thumbnail image
            if (i < (int)G.thumbs.size() && G.thumbs[i]) {
                g.DrawImage(G.thumbs[i], tx, ty, THUMB_W, THUMB_H);
            } else {
                SolidBrush placeholder(Color(255, 250, 250, 250));
                g.FillRectangle(&placeholder, tx, ty, THUMB_W, THUMB_H);
                Pen border(Color(200, 200, 200, 200), 1.0f);
                g.DrawRectangle(&border, tx, ty, THUMB_W, THUMB_H);
            }

            // Page number label
            wstring label = to_wstring(i + 1);
            SolidBrush lblBr(isCur ? Color(255,100,180,255) : Color(160,160,160,180));
            g.DrawString(label.c_str(), -1, &pageFont,
                         PointF((REAL)(tx + THUMB_W/2 - 8), (REAL)(ty + THUMB_H + 4)),
                         &lblBr);

            y += THUMB_H + pad * 2 + 20;
            if (y - S(28) + G.thumbScrollY > ch + THUMB_H) break;
        }
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK SidebarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        OnSidebarPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        if (!G.pdfDoc) return 0;
        int my = GET_Y_LPARAM(lp);
        int pad = 10;
        int y0  = S(28) + pad - G.thumbScrollY;
        int clicked = (my - y0) / (THUMB_H + pad * 2 + 20);
        if (clicked >= 0 && clicked < G.pageCount) {
            G.currentPage = clicked;
            G.scrollX = G.scrollY = 0;
            DeselectAll();
            UpdateScrollbars(G.hwndCanvas);
            SetStatus(L"Page " + ItoW(G.currentPage + 1) + L" of " + ItoW(G.pageCount));
            InvalidateRect(hwnd, nullptr, FALSE);
            InvalidateRect(G.hwndCanvas, nullptr, FALSE);
            PostMessageW(G.hwndMain, APP_PAGE_CHANGED, 0, 0);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        G.thumbScrollY -= delta / 3;
        G.thumbScrollY = max(0, G.thumbScrollY);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// =============================================================================
//  SECTION 11 – TOOLBAR WINDOW PROC
// =============================================================================

struct ToolbarButton {
    int     id;
    RECT    rect;
    bool    isHovered;
    bool    isPressed;
    bool    isToggle;
    bool    isActive;
    wchar_t label[32];
    wchar_t tooltip[64];
};

static vector<ToolbarButton> g_toolbarBtns;
static vector<ToolbarButton> g_toolBtns;

static void BuildToolbarLayout(int w) {
    g_toolbarBtns.clear();
    int bw = S(36), bh = S(36), pad = S(4), y = S(6);
    int x = pad;

    auto addBtn = [&](int id, const wchar_t* lbl, const wchar_t* tip, bool toggle=false) {
        ToolbarButton b{};
        b.id = id;
        b.rect = {x, y, x+bw, y+bh};
        b.isHovered = b.isPressed = false;
        b.isToggle  = toggle;
        b.isActive  = false;
        wcsncpy_s(b.label,   lbl, 31);
        wcsncpy_s(b.tooltip, tip, 63);
        g_toolbarBtns.push_back(b);
        x += bw + pad;
    };
    auto sep = [&]() { x += S(8); };

    addBtn(ID_BTN_OPEN,     L"📂", L"Open PDF (Ctrl+O)");
    addBtn(ID_BTN_SAVE,     L"💾", L"Save (Ctrl+S)");
    addBtn(ID_BTN_SAVE_AS,  L"🖫",  L"Save As (Ctrl+Shift+S)");
    sep();
    addBtn(ID_BTN_UNDO,     L"↩", L"Undo (Ctrl+Z)");
    addBtn(ID_BTN_REDO,     L"↪", L"Redo (Ctrl+Y)");
    sep();
    addBtn(ID_BTN_PREV,     L"◀", L"Previous Page");
    addBtn(ID_BTN_NEXT,     L"▶", L"Next Page");
    sep();
    addBtn(ID_BTN_ZOOM_OUT, L"🔍-", L"Zoom Out (Ctrl+-)");
    addBtn(ID_BTN_ZOOM_IN,  L"🔍+", L"Zoom In (Ctrl+=)");
    addBtn(ID_BTN_ZOOM_FIT, L"⊡",  L"Fit to Window");
    sep();
    addBtn(ID_BTN_ROTATE_CW,  L"↻", L"Rotate CW");
    addBtn(ID_BTN_ROTATE_CCW, L"↺", L"Rotate CCW");
    sep();
    addBtn(ID_BTN_PRINT,     L"🖨", L"Print");
    addBtn(ID_BTN_FULLSCREEN,L"⛶", L"Fullscreen (F11)");
}

static void BuildToolPanelLayout(int h) {
    g_toolBtns.clear();
    int bw = S(34), bh = S(34), pad = S(3), x = S(3);
    int y = S(TOOLBAR_H) + S(4);

    auto addTool = [&](int id, const wchar_t* lbl, const wchar_t* tip) {
        ToolbarButton b{};
        b.id = id;
        b.rect = {x, y, x+bw, y+bh};
        b.isToggle = true;
        b.isActive = false;
        wcsncpy_s(b.label,   lbl, 31);
        wcsncpy_s(b.tooltip, tip, 63);
        g_toolBtns.push_back(b);
        y += bh + pad;
    };

    addTool(ID_TOOL_SELECT,   L"↖",  L"Select / Move");
    addTool(ID_TOOL_HAND,     L"✋",  L"Pan (Hand)");
    addTool(ID_TOOL_TEXT,     L"T",   L"Insert Text");
    addTool(ID_TOOL_HIGHLIGHT,L"▬",  L"Highlight");
    addTool(ID_TOOL_UNDERLINE,L"U̲",  L"Underline");
    addTool(ID_TOOL_STRIKE,   L"S̶",   L"Strikethrough");
    addTool(ID_TOOL_DRAW,     L"✏",  L"Free Draw");
    addTool(ID_TOOL_ERASER,   L"⌫",  L"Eraser");
    addTool(ID_TOOL_RECT,     L"▭",  L"Rectangle");
    addTool(ID_TOOL_ELLIPSE,  L"◯",  L"Ellipse");
    addTool(ID_TOOL_ARROW,    L"→",  L"Arrow");
    addTool(ID_TOOL_STICKY,   L"📝", L"Sticky Note");
    addTool(ID_TOOL_STAMP,    L"📌", L"Stamp");
    addTool(ID_TOOL_REDACT,   L"█",  L"Redact");
    addTool(ID_TOOL_WHITEOUT, L"□",  L"White-Out");
    addTool(ID_TOOL_SIGNATURE,L"✍",  L"Signature");
    addTool(ID_TOOL_MEASURE,  L"📐", L"Measure");
}

static void SetActiveTool(Tool t) {
    G.activeTool = t;
    // Sync button active states
    auto toolForID = [](int id) -> Tool {
        switch (id) {
        case ID_TOOL_SELECT:    return Tool::Select;
        case ID_TOOL_HAND:      return Tool::Hand;
        case ID_TOOL_TEXT:      return Tool::Text;
        case ID_TOOL_HIGHLIGHT: return Tool::Highlight;
        case ID_TOOL_UNDERLINE: return Tool::Underline;
        case ID_TOOL_STRIKE:    return Tool::Strikethrough;
        case ID_TOOL_DRAW:      return Tool::Draw;
        case ID_TOOL_ERASER:    return Tool::Eraser;
        case ID_TOOL_RECT:      return Tool::Rectangle;
        case ID_TOOL_ELLIPSE:   return Tool::Ellipse;
        case ID_TOOL_ARROW:     return Tool::Arrow;
        case ID_TOOL_STICKY:    return Tool::StickyNote;
        case ID_TOOL_STAMP:     return Tool::Stamp;
        case ID_TOOL_REDACT:    return Tool::Redact;
        case ID_TOOL_WHITEOUT:  return Tool::WhiteOut;
        case ID_TOOL_SIGNATURE: return Tool::Signature;
        case ID_TOOL_MEASURE:   return Tool::Measure;
        default:                return Tool::Hand;
        }
    };
    for (auto& b : g_toolBtns)
        b.isActive = (toolForID(b.id) == t);
    InvalidateRect(G.hwndToolbar, nullptr, FALSE);
}

static void PaintToolbar(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // Background
        Color bgCol(255, 22, 22, 30);
        SolidBrush bgBr(bgCol);
        g.FillRectangle(&bgBr, 0, 0, cw, ch);

        // Separator line at bottom of main toolbar strip
        Pen sepPen(Color(60, 255, 255, 255), 1.0f);
        g.DrawLine(&sepPen, 0, S(TOOLBAR_H)-1, cw, S(TOOLBAR_H)-1);

        FontFamily ff(L"Segoe UI Emoji");
        Font btnFont(&ff, 13.0f, FontStyleRegular, UnitPixel);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);

        // Draw main toolbar buttons
        for (auto& b : g_toolbarBtns) {
            RectF rf = ToRectF(b.rect);
            if (b.isPressed) {
                SolidBrush pbr(Color(100, 33, 150, 243));
                g.FillRectangle(&pbr, rf);
            } else if (b.isHovered) {
                SolidBrush hbr(Color(50, 255, 255, 255));
                g.FillRectangle(&hbr, rf);
            }
            SolidBrush txtBr(Color(220, 210, 210, 230));
            g.DrawString(b.label, -1, &btnFont, rf, &sf, &txtBr);
        }

        // Tool panel buttons
        FontFamily ff2(L"Segoe UI Emoji");
        Font toolFont(&ff2, 14.0f, FontStyleRegular, UnitPixel);
        for (auto& b : g_toolBtns) {
            RectF rf = ToRectF(b.rect);
            if (b.isActive) {
                SolidBrush abr(Color(180, 33, 150, 243));
                g.FillRectangle(&abr, rf);
                Pen aPen(Color(255, 33, 150, 243), 1.5f);
                g.DrawRectangle(&aPen, rf.X, rf.Y, rf.Width-1, rf.Height-1);
            } else if (b.isHovered) {
                SolidBrush hbr(Color(50, 255, 255, 255));
                g.FillRectangle(&hbr, rf);
            }
            SolidBrush txtBr(b.isActive ? Color(255,255,255,255) : Color(200,200,200,220));
            g.DrawString(b.label, -1, &toolFont, rf, &sf, &txtBr);
        }

        // Color swatch
        int swY = (int)g_toolBtns.back().rect.bottom + S(12);
        SolidBrush swBr(GdipColor(G.activeColor));
        g.FillRectangle(&swBr, S(3), swY, S(34), S(34));
        Pen swPen(Color(255,255,255,255), 1.0f);
        g.DrawRectangle(&swPen, S(3), swY, S(34), S(34));

        // Stroke width label
        Font smallFont(&ff, 8.5f, FontStyleRegular, UnitPixel);
        SolidBrush smallBr(Color(140,160,160,180));
        int lwY = swY + S(38);
        wstring swLabel = L"W:" + to_wstring((int)G.strokeWidth);
        g.DrawString(swLabel.c_str(), -1, &smallFont, PointF(S(3.0f), (REAL)lwY), &smallBr);

        // Opacity label
        int opY = lwY + S(14);
        wstring opLabel = L"A:" + to_wstring((int)(G.opacity * 100)) + L"%";
        g.DrawString(opLabel.c_str(), -1, &smallFont, PointF(S(3.0f), (REAL)opY), &smallBr);
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        PaintToolbar(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        POINT pt = {mx, my};
        bool changed = false;
        for (auto& b : g_toolbarBtns) {
            bool prev = b.isHovered;
            b.isHovered = PtInRect(&b.rect, pt) != 0;
            if (prev != b.isHovered) changed = true;
        }
        for (auto& b : g_toolBtns) {
            bool prev = b.isHovered;
            b.isHovered = PtInRect(&b.rect, pt) != 0;
            if (prev != b.isHovered) changed = true;
        }
        if (changed) InvalidateRect(hwnd, nullptr, FALSE);
        // Track leave
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE: {
        for (auto& b : g_toolbarBtns) b.isHovered = false;
        for (auto& b : g_toolBtns)    b.isHovered = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        POINT pt = {mx, my};
        for (auto& b : g_toolbarBtns) {
            if (PtInRect(&b.rect, pt)) {
                b.isPressed = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                PostMessageW(G.hwndMain, WM_COMMAND, b.id, 0);
                return 0;
            }
        }
        for (auto& b : g_toolBtns) {
            if (PtInRect(&b.rect, pt)) {
                PostMessageW(G.hwndMain, WM_COMMAND, b.id, 0);
                return 0;
            }
        }

        // Check color swatch
        if (!g_toolBtns.empty()) {
            int swY = (int)g_toolBtns.back().rect.bottom + S(12);
            RECT swRect = {S(3), swY, S(3)+S(34), swY+S(34)};
            if (PtInRect(&swRect, pt)) {
                CHOOSECOLORW cc{};
                static COLORREF custColors[16] = {};
                memcpy(custColors, G.recentColors, sizeof(G.recentColors));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner   = G.hwndMain;
                cc.rgbResult   = G.activeColor;
                cc.lpCustColors = custColors;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    G.activeColor = cc.rgbResult;
                    // Push to recent
                    for (int i = 7; i > 0; i--)
                        G.recentColors[i] = G.recentColors[i-1];
                    G.recentColors[0] = G.activeColor;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        for (auto& b : g_toolbarBtns) b.isPressed = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// =============================================================================
//  SECTION 12 – STATUS BAR
// =============================================================================

static void PaintStatus(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        SolidBrush bgBr(Color(255, 18, 18, 25));
        g.FillRectangle(&bgBr, 0, 0, cw, ch);

        Pen topPen(Color(60, 255, 255, 255), 1.0f);
        g.DrawLine(&topPen, 0, 0, cw, 0);

        FontFamily ff(L"Segoe UI");
        Font fnt(&ff, 11.0f, FontStyleRegular, UnitPixel);
        SolidBrush tb(Color(160, 180, 180, 200));
        g.DrawString(G.statusText.c_str(), -1, &fnt, PointF(8.0f, 6.0f), &tb);

        // Right side: page/zoom info
        if (G.pdfDoc) {
            wstring info = L"Page " + ItoW(G.currentPage+1) + L"/" + ItoW(G.pageCount)
                         + L"   Zoom: " + ZoomLabel();
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentFar);
            RectF tr(0, 6.0f, (REAL)(cw - 8), (REAL)(ch - 8));
            g.DrawString(info.c_str(), -1, &fnt, tr, &sfR, &tb);
        }
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK StatusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) { PaintStatus(hwnd); return 0; }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =============================================================================
//  SECTION 13 – SIGNATURE PAD DIALOG
// =============================================================================

static vector<InkStroke> g_sigStrokes;
static InkStroke         g_sigCurrent;
static bool              g_sigDrawing = false;

static void PaintSigPad(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Graphics g(memDC);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        SolidBrush bgBr(Color(255, 255, 255, 255));
        g.FillRectangle(&bgBr, 0, 0, cw, ch);

        // Guide line
        Pen guidePen(Color(200, 180, 180, 180), 1.0f);
        guidePen.SetDashStyle(DashStyleDash);
        g.DrawLine(&guidePen, 20, ch - 40, cw - 20, ch - 40);

        // Draw strokes
        for (auto& stroke : g_sigStrokes) {
            if (stroke.pts.size() < 2) continue;
            Pen pen(Color(255, 0, 0, 0), stroke.width);
            pen.SetLineJoin(LineJoinRound);
            pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            for (int i = 1; i < (int)stroke.pts.size(); i++) {
                g.DrawLine(&pen, stroke.pts[i-1].x, stroke.pts[i-1].y,
                                 stroke.pts[i  ].x, stroke.pts[i  ].y);
            }
        }
        // Current stroke
        if (!g_sigCurrent.pts.empty() && g_sigCurrent.pts.size() >= 2) {
            Pen pen(Color(255,0,0,0), g_sigCurrent.width);
            pen.SetLineJoin(LineJoinRound);
            for (int i = 1; i < (int)g_sigCurrent.pts.size(); i++) {
                g.DrawLine(&pen, g_sigCurrent.pts[i-1].x, g_sigCurrent.pts[i-1].y,
                                 g_sigCurrent.pts[i  ].x, g_sigCurrent.pts[i  ].y);
            }
        }

        // Border
        Pen border(Color(200,180,180,200), 1.5f);
        g.DrawRectangle(&border, 0, 0, cw-1, ch-1);

        // Hint text
        if (g_sigStrokes.empty() && g_sigCurrent.pts.empty()) {
            FontFamily ff(L"Segoe UI");
            Font hint(&ff, 12.0f, FontStyleItalic, UnitPixel);
            SolidBrush hintBr(Color(120,180,180,180));
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            RectF tr(0, 0, (REAL)cw, (REAL)(ch-50));
            g.DrawString(L"Draw your signature here", -1, &hint, tr, &sf, &hintBr);
        }
    }

    BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// Signature pad child window
static HWND g_hwndSigPad = nullptr;

static LRESULT CALLBACK SigPadProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        PaintSigPad(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_sigDrawing = true;
        g_sigCurrent = {};
        g_sigCurrent.color = RGB(0,0,0);
        g_sigCurrent.width = 2.5f;
        g_sigCurrent.opacity = 1.0f;
        g_sigCurrent.pts.push_back({(float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp)});
        return 0;
    case WM_MOUSEMOVE:
        if (g_sigDrawing) {
            g_sigCurrent.pts.push_back({(float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)});
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g_sigDrawing) {
            g_sigDrawing = false;
            if (!g_sigCurrent.pts.empty())
                g_sigStrokes.push_back(g_sigCurrent);
            g_sigCurrent = {};
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// Signature dialog
enum { SIG_OK=IDOK, SIG_CANCEL=IDCANCEL, SIG_CLEAR=100 };

static INT_PTR CALLBACK SigDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_sigStrokes.clear();
        g_sigCurrent = {};
        // Create pad child
        RECT cr; GetClientRect(dlg, &cr);
        g_hwndSigPad = CreateWindowExW(0, L"RasPDFSigPad", nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            10, 10, cr.right-20, cr.bottom-60,
            dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case SIG_OK:
            // Store signature
            G.signature.strokes = g_sigStrokes;
            G.signature.hasData = !g_sigStrokes.empty();
            EndDialog(dlg, IDOK);
            break;
        case SIG_CANCEL:
            EndDialog(dlg, IDCANCEL);
            break;
        case SIG_CLEAR:
            g_sigStrokes.clear();
            g_sigCurrent = {};
            InvalidateRect(g_hwndSigPad, nullptr, TRUE);
            break;
        }
        return TRUE;
    case WM_SIZE: {
        int cw = LOWORD(lp), ch = HIWORD(lp);
        if (g_hwndSigPad)
            SetWindowPos(g_hwndSigPad, nullptr, 10, 10, cw-20, ch-60, SWP_NOZORDER);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

static void ShowSignatureDialog() {
    // Register sig pad class if needed
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = SigPadProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"RasPDFSigPad";
        wc.hCursor       = LoadCursorW(nullptr, IDC_CROSS);
        RegisterClassW(&wc);
        registered = true;
    }

    // We create a dialog procedurally
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, WC_DIALOG, L"Draw Signature",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME,
        100, 100, 500, 300,
        G.hwndMain, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!dlg) {
        // Fallback: use MessageBox
        MessageBoxW(G.hwndMain, L"Signature pad unavailable.\nUse Ctrl+G shortcut.",
                    L"Signature", MB_ICONINFORMATION);
        return;
    }

    // Add buttons
    CreateWindowExW(0, L"BUTTON", L"OK",     WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
                    10, 230, 80, 28, dlg, (HMENU)(INT_PTR)SIG_OK,
                    GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"Clear",  WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                    100, 230, 80, 28, dlg, (HMENU)(INT_PTR)SIG_CLEAR,
                    GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                    190, 230, 80, 28, dlg, (HMENU)(INT_PTR)SIG_CANCEL,
                    GetModuleHandleW(nullptr), nullptr);

    g_sigStrokes.clear();
    g_sigCurrent = {};

    // Register pad class
    static bool regPad = false;
    if (!regPad) {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = SigPadProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"RasPDFSigPad";
        wc.hCursor       = LoadCursorW(nullptr, IDC_CROSS);
        RegisterClassW(&wc);
        regPad = true;
    }

    RECT cr; GetClientRect(dlg, &cr);
    g_hwndSigPad = CreateWindowExW(WS_EX_CLIENTEDGE, L"RasPDFSigPad", nullptr,
        WS_CHILD | WS_VISIBLE,
        10, 10, max(1, cr.right-20), max(1, cr.bottom-60),
        dlg, nullptr, GetModuleHandleW(nullptr), nullptr);

    // Subclass for WM_COMMAND forwarding
    SetWindowLongPtrW(dlg, GWLP_WNDPROC, (LONG_PTR)SigDlgProc);
    ShowWindow(dlg, SW_SHOW);

    // Modal message loop
    MSG msg;
    EnableWindow(G.hwndMain, FALSE);
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(G.hwndMain, TRUE);
    SetForegroundWindow(G.hwndMain);
}

// =============================================================================
//  SECTION 14 – TEXT INSERT DIALOG
// =============================================================================

static wstring ShowTextInputDialog(HWND parent) {
    // Simple edit-box dialog
    struct Dlg {
        static INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
            static HWND edit = nullptr;
            switch (msg) {
            case WM_INITDIALOG: {
                edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                    WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,
                    10, 10, 360, 120, dlg,
                    nullptr, GetModuleHandleW(nullptr), nullptr);
                CreateWindowExW(0, L"BUTTON", L"Insert", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
                    10, 140, 80, 28, dlg, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
                CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE,
                    100, 140, 80, 28, dlg, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
                SetFocus(edit);
                return FALSE;
            }
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[1024] = {};
                    GetWindowTextW(edit, buf, 1023);
                    SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)new wstring(buf));
                    EndDialog(dlg, IDOK);
                } else if (LOWORD(wp) == IDCANCEL) {
                    EndDialog(dlg, IDCANCEL);
                }
                return TRUE;
            default:
                return FALSE;
            }
        }
    };

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, WC_DIALOG, L"Insert Text",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        200, 200, 400, 210,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(dlg, GWLP_WNDPROC, (LONG_PTR)Dlg::Proc);
    Dlg::Proc(dlg, WM_INITDIALOG, 0, 0);
    ShowWindow(dlg, SW_SHOW);

    wstring result;
    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return result;
}

// =============================================================================
//  SECTION 15 – PRINT SUPPORT
// =============================================================================

static void DoPrint() {
    if (!G.pdfDoc) return;

    PRINTDLGW pd = {};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = G.hwndMain;
    pd.Flags       = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION;
    if (!PrintDlgW(&pd)) return;

    HDC printerDC = pd.hDC;
    if (!printerDC) return;

    DOCINFOW di = {};
    di.cbSize   = sizeof(di);
    di.lpszDocName = L"RasPDF Print";
    StartDocW(printerDC, &di);

    for (int i = 0; i < G.pageCount; i++) {
        StartPage(printerDC);

        int pw_dev = GetDeviceCaps(printerDC, HORZRES);
        int ph_dev = GetDeviceCaps(printerDC, VERTRES);

        // Render page at printer resolution
        FPDF_PAGE page = FPDF_LoadPage(G.pdfDoc, i);
        double ppw = FPDF_GetPageWidth(page);
        double pph = FPDF_GetPageHeight(page);
        FPDF_ClosePage(page);

        float scaleX = pw_dev / (float)ppw;
        float scaleY = ph_dev / (float)pph;
        float scale  = min(scaleX, scaleY);

        int bw = (int)(ppw * scale);
        int bh = (int)(pph * scale);
        if (bw < 1) bw = 1;
        if (bh < 1) bh = 1;

        Bitmap* bmp = RenderPageToBitmap(i, (int)(scale * 100 / G.dpiScale), 0);
        if (bmp) {
            Graphics gpr(printerDC);
            gpr.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            gpr.DrawImage(bmp, 0, 0, bw, bh);
            delete bmp;
        }

        EndPage(printerDC);
    }

    EndDoc(printerDC);
    DeleteDC(printerDC);
    if (pd.hDevMode)  GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    SetStatus(L"Print job sent.");
}

// =============================================================================
//  SECTION 16 – TITLE BAR (custom, borderless)
// =============================================================================

static void PaintTitleBar(HWND hwnd, Graphics& g, int cw) {
    Color tbBg(255, 15, 15, 22);
    SolidBrush tbBr(tbBg);
    g.FillRectangle(&tbBr, 0, 0, cw, S(TITLEBAR_H));

    // Bottom separator
    Pen sep(Color(50, 255, 255, 255), 1.0f);
    g.DrawLine(&sep, 0, S(TITLEBAR_H)-1, cw, S(TITLEBAR_H)-1);

    // App icon placeholder
    Color iconCol(200, 33, 150, 243);
    SolidBrush iconBr(iconCol);
    g.FillEllipse(&iconBr, S(8), S(8), S(16), S(16));
    SolidBrush iconInner(Color(255,255,255,255));
    FontFamily ff(L"Segoe UI");
    Font iconFont(&ff, 9.0f, FontStyleBold, UnitPixel);
    g.DrawString(L"R", -1, &iconFont, PointF(S(11.0f), S(9.5f)), &iconInner);

    // Title text
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, 255);
    Font titleFont(&ff, 10.5f, FontStyleRegular, UnitPixel);
    SolidBrush titleBr(Color(210, 200, 200, 220));
    g.DrawString(title, -1, &titleFont, PointF(S(30.0f), S(8.0f)), &titleBr);

    // Minimize button
    RECT minR = {cw - S(120), 0, cw - S(80), S(TITLEBAR_H)};
    SolidBrush minBr(Color(40,255,255,255));
    if (false) g.FillRectangle(&minBr, ToRectF(minR)); // hover handled in WM_MOUSEMOVE
    FontFamily ff2(L"Segoe UI");
    Font capFont(&ff2, 10.0f, FontStyleRegular, UnitPixel);
    SolidBrush capTxt(Color(200,200,200,220));
    StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"─", -1, &capFont, ToRectF(minR), &sf, &capTxt);

    // Maximize button
    RECT maxR = {cw - S(80), 0, cw - S(40), S(TITLEBAR_H)};
    g.DrawString(L"□", -1, &capFont, ToRectF(maxR), &sf, &capTxt);

    // Close button
    G.closeBtnRect = {cw - S(40), 0, cw, S(TITLEBAR_H)};
    Color closeCol = G.closeBtnHover ? Color(255, 196, 43, 28) : Color(0, 0, 0, 0);
    SolidBrush closeBr(closeCol);
    g.FillRectangle(&closeBr, ToRectF(G.closeBtnRect));
    g.DrawString(L"✕", -1, &capFont, ToRectF(G.closeBtnRect), &sf, &capTxt);
}

// =============================================================================
//  SECTION 17 – MAIN WINDOW PROC
// =============================================================================

static void LayoutChildren(HWND hwnd) {
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;

    int tbH  = S(TITLEBAR_H);
    int barH = S(TOOLBAR_H);
    int sideW = G.sidebarVisible ? S(SIDEBAR_W) : 0;
    int stH  = S(STATUS_H);
    int toolW = S(44);

    // Title bar is part of main window painting (not a child)
    // Toolbar strip
    SetWindowPos(G.hwndToolbar, nullptr,
                 0, tbH,
                 toolW, ch - tbH - stH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Sidebar
    if (G.sidebarVisible)
        SetWindowPos(G.hwndSidebar, nullptr,
                     toolW, tbH,
                     S(SIDEBAR_W), ch - tbH - stH,
                     SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(G.hwndSidebar, G.sidebarVisible ? SW_SHOW : SW_HIDE);

    // Canvas
    int canvX = toolW + sideW;
    int canvW = cw - canvX;
    int canvH = ch - tbH - stH;
    SetWindowPos(G.hwndCanvas, nullptr,
                 canvX, tbH,
                 max(1, canvW), max(1, canvH),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Status
    SetWindowPos(G.hwndStatus, nullptr,
                 0, ch - stH,
                 cw, stH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Rebuild toolbar layout
    BuildToolbarLayout(toolW);
    BuildToolPanelLayout(ch - tbH - stH);
    SetActiveTool(G.activeTool); // re-sync active state

    UpdateScrollbars(G.hwndCanvas);
}

static void HandleCommand(HWND hwnd, int id) {
    switch (id) {
    // ── File ops ──────────────────────────────────────────────────────────
    case ID_BTN_OPEN: {
        wstring path = ShowOpenDialog(hwnd);
        if (!path.empty()) OpenDocument(path);
        break;
    }
    case ID_BTN_SAVE:
        if (!G.filePath.empty()) SaveDocument(G.filePath);
        else { /* fall through to Save As */ }
        [[fallthrough]];
    case ID_BTN_SAVE_AS: {
        wstring fname;
        if (!G.filePath.empty())
            fname = G.filePath.substr(G.filePath.rfind(L'\\') + 1);
        wstring path = ShowSaveDialog(hwnd, fname);
        if (!path.empty()) SaveDocument(path);
        break;
    }
    case ID_BTN_CLOSE_DOC:
        if (G.isModified) {
            int r = MessageBoxW(hwnd, L"Save changes before closing?",
                                L"Unsaved Changes",
                                MB_YESNOCANCEL | MB_ICONWARNING);
            if (r == IDCANCEL) break;
            if (r == IDYES) {
                if (!G.filePath.empty()) SaveDocument(G.filePath);
                else {
                    wstring p = ShowSaveDialog(hwnd, {});
                    if (!p.empty()) SaveDocument(p);
                }
            }
        }
        CloseDocument();
        InvalidateRect(hwnd, nullptr, FALSE);
        break;

    // ── Navigation ────────────────────────────────────────────────────────
    case ID_BTN_PREV:
        if (G.pdfDoc && G.currentPage > 0) {
            G.currentPage--;
            G.scrollX = G.scrollY = 0;
            DeselectAll();
            UpdateScrollbars(G.hwndCanvas);
            SetStatus(L"Page " + ItoW(G.currentPage+1) + L" of " + ItoW(G.pageCount));
            PostMessageW(hwnd, APP_PAGE_CHANGED, 0, 0);
            InvalidateRect(G.hwndCanvas,  nullptr, FALSE);
            InvalidateRect(G.hwndSidebar, nullptr, FALSE);
            InvalidateRect(G.hwndStatus,  nullptr, FALSE);
        }
        break;
    case ID_BTN_NEXT:
        if (G.pdfDoc && G.currentPage < G.pageCount - 1) {
            G.currentPage++;
            G.scrollX = G.scrollY = 0;
            DeselectAll();
            UpdateScrollbars(G.hwndCanvas);
            SetStatus(L"Page " + ItoW(G.currentPage+1) + L" of " + ItoW(G.pageCount));
            PostMessageW(hwnd, APP_PAGE_CHANGED, 0, 0);
            InvalidateRect(G.hwndCanvas,  nullptr, FALSE);
            InvalidateRect(G.hwndSidebar, nullptr, FALSE);
            InvalidateRect(G.hwndStatus,  nullptr, FALSE);
        }
        break;

    // ── Zoom ──────────────────────────────────────────────────────────────
    case ID_BTN_ZOOM_IN:
        G.zoom = Clamp(G.zoom + 10, MIN_ZOOM, MAX_ZOOM);
        InvalidateCache(G.currentPage);
        UpdateScrollbars(G.hwndCanvas);
        SetStatus(L"Zoom: " + ZoomLabel());
        InvalidateRect(G.hwndCanvas, nullptr, FALSE);
        break;
    case ID_BTN_ZOOM_OUT:
        G.zoom = Clamp(G.zoom - 10, MIN_ZOOM, MAX_ZOOM);
        InvalidateCache(G.currentPage);
        UpdateScrollbars(G.hwndCanvas);
        SetStatus(L"Zoom: " + ZoomLabel());
        InvalidateRect(G.hwndCanvas, nullptr, FALSE);
        break;
    case ID_BTN_ZOOM_FIT:
        if (G.pdfDoc) {
            FPDF_PAGE page = FPDF_LoadPage(G.pdfDoc, G.currentPage);
            if (page) {
                RECT canvRect; GetClientRect(G.hwndCanvas, &canvRect);
                double pw = FPDF_GetPageWidth(page);
                double ph = FPDF_GetPageHeight(page);
                FPDF_ClosePage(page);
                float zoomX = canvRect.right  / (float)pw * 100.0f / G.dpiScale;
                float zoomY = canvRect.bottom / (float)ph * 100.0f / G.dpiScale;
                G.zoom = (int)min(zoomX, zoomY);
                G.zoom = Clamp(G.zoom, MIN_ZOOM, MAX_ZOOM);
                G.scrollX = G.scrollY = 0;
                InvalidateCache(G.currentPage);
                UpdateScrollbars(G.hwndCanvas);
                SetStatus(L"Fit: " + ZoomLabel());
                InvalidateRect(G.hwndCanvas, nullptr, FALSE);
            }
        }
        break;

    // ── Rotation ──────────────────────────────────────────────────────────
    case ID_BTN_ROTATE_CW:
        if (G.pdfDoc && !G.pages.empty()) {
            G.pages[G.currentPage].rotation = (G.pages[G.currentPage].rotation + 90) % 360;
            InvalidateCache(G.currentPage);
            InvalidateRect(G.hwndCanvas, nullptr, FALSE);
            SetStatus(L"Rotated CW — Page " + ItoW(G.currentPage+1));
        }
        break;
    case ID_BTN_ROTATE_CCW:
        if (G.pdfDoc && !G.pages.empty()) {
            G.pages[G.currentPage].rotation = (G.pages[G.currentPage].rotation + 270) % 360;
            InvalidateCache(G.currentPage);
            InvalidateRect(G.hwndCanvas, nullptr, FALSE);
            SetStatus(L"Rotated CCW — Page " + ItoW(G.currentPage+1));
        }
        break;

    // ── Undo / Redo ───────────────────────────────────────────────────────
    case ID_BTN_UNDO:
        DoUndo();
        break;
    case ID_BTN_REDO:
        DoRedo();
        break;

    // ── Print ─────────────────────────────────────────────────────────────
    case ID_BTN_PRINT:
        DoPrint();
        break;

    // ── Fullscreen ────────────────────────────────────────────────────────
    case ID_BTN_FULLSCREEN:
        if (!G.isFullscreen) {
            GetWindowRect(hwnd, &G.savedWndRect);
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, sw, sh, SWP_FRAMECHANGED);
            G.isFullscreen = true;
        } else {
            SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_THICKFRAME | WS_VISIBLE);
            SetWindowPos(hwnd, nullptr,
                G.savedWndRect.left, G.savedWndRect.top,
                G.savedWndRect.right - G.savedWndRect.left,
                G.savedWndRect.bottom - G.savedWndRect.top,
                SWP_FRAMECHANGED);
            G.isFullscreen = false;
        }
        break;

    // ── Tools ──────────────────────────────────────────────────────────────
    case ID_TOOL_SELECT:    SetActiveTool(Tool::Select);        break;
    case ID_TOOL_HAND:      SetActiveTool(Tool::Hand);          break;
    case ID_TOOL_TEXT: {
        SetActiveTool(Tool::Text);
        // If we're re-clicking text tool when a pending click happened,
        // show dialog
        if (G.pendingTextBounds.w > 0) {
            wstring txt = ShowTextInputDialog(hwnd);
            if (!txt.empty()) {
                Annotation a = MakeAnnotTemplate(AnnotKind::Text);
                a.text        = txt;
                a.bounds      = G.pendingTextBounds;
                a.strokeWidth = 14.0f; // font size
                CommitAnnotation(a);
            }
            G.pendingTextBounds = {};
        }
        break;
    }
    case ID_TOOL_HIGHLIGHT:  SetActiveTool(Tool::Highlight);     break;
    case ID_TOOL_UNDERLINE:  SetActiveTool(Tool::Underline);     break;
    case ID_TOOL_STRIKE:     SetActiveTool(Tool::Strikethrough); break;
    case ID_TOOL_DRAW:       SetActiveTool(Tool::Draw);          break;
    case ID_TOOL_ERASER:     SetActiveTool(Tool::Eraser);        break;
    case ID_TOOL_RECT:       SetActiveTool(Tool::Rectangle);     break;
    case ID_TOOL_ELLIPSE:    SetActiveTool(Tool::Ellipse);       break;
    case ID_TOOL_ARROW:      SetActiveTool(Tool::Arrow);         break;
    case ID_TOOL_STICKY:     SetActiveTool(Tool::StickyNote);    break;
    case ID_TOOL_STAMP:      SetActiveTool(Tool::Stamp);         break;
    case ID_TOOL_REDACT:     SetActiveTool(Tool::Redact);        break;
    case ID_TOOL_WHITEOUT:   SetActiveTool(Tool::WhiteOut);      break;
    case ID_TOOL_SIGNATURE:
        ShowSignatureDialog();
        SetActiveTool(Tool::Signature);
        break;
    case ID_TOOL_MEASURE:    SetActiveTool(Tool::Measure);       break;

    // ── Close ─────────────────────────────────────────────────────────────
    case ID_BTN_CLOSE:
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    // ── Create ────────────────────────────────────────────────────────────
    case WM_CREATE: {
        // DPI
        HDC hdc = GetDC(hwnd);
        int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        G.dpiScale = dpiY / 96.0f;
        ReleaseDC(hwnd, hdc);

        // Initialize PDFium
        FPDF_InitLibraryWithConfig(nullptr);

        // Create child windows
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        G.hwndToolbar = CreateWindowExW(0, TOOLBAR_CLASS, nullptr,
            WS_CHILD|WS_VISIBLE, 0, 0, 1, 1, hwnd, nullptr, hInst, nullptr);

        G.hwndSidebar = CreateWindowExW(0, SIDEBAR_CLASS, nullptr,
            WS_CHILD|WS_VISIBLE, 0, 0, 1, 1, hwnd, nullptr, hInst, nullptr);

        G.hwndCanvas = CreateWindowExW(0, CANVAS_CLASS, nullptr,
            WS_CHILD|WS_VISIBLE|WS_HSCROLL|WS_VSCROLL,
            0, 0, 1, 1, hwnd, nullptr, hInst, nullptr);

        G.hwndStatus = CreateWindowExW(0, STATUS_CLASS, nullptr,
            WS_CHILD|WS_VISIBLE, 0, 0, 1, 1, hwnd, nullptr, hInst, nullptr);

        // Initialize tool/toolbar layouts (with dummy sizes; LayoutChildren will correct)
        BuildToolbarLayout(S(44));
        BuildToolPanelLayout(600);
        SetActiveTool(Tool::Hand);

        // Init cache
        for (int i = 0; i < AppState::CACHE_SIZE; i++) G.cache[i] = {};

        // Drag-and-drop
        DragAcceptFiles(hwnd, TRUE);

        SetStatus(L"Ready — Open a PDF to begin.");
        return 0;
    }

    // ── Size ─────────────────────────────────────────────────────────────
    case WM_SIZE:
        LayoutChildren(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    // ── Paint (title bar) ─────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT cr; GetClientRect(hwnd, &cr);
        int cw = cr.right;

        // Only paint the title bar strip; children paint themselves
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, S(TITLEBAR_H));
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        {
            Graphics g(memDC);
            g.SetSmoothingMode(SmoothingModeHighQuality);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            PaintTitleBar(hwnd, g, cw);
        }

        BitBlt(hdc, 0, 0, cw, S(TITLEBAR_H), memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Mouse on title bar ───────────────────────────────────────────────
    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        POINT pt = {mx, my};

        if (G.isWinDragging) {
            POINT cur; GetCursorPos(&cur);
            RECT wr; GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, nullptr,
                wr.left + cur.x - G.winDragStart.x,
                wr.top  + cur.y - G.winDragStart.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
            G.winDragStart = cur;
            return 0;
        }

        bool wasHover = G.closeBtnHover;
        G.closeBtnHover = PtInRect(&G.closeBtnRect, pt) != 0;
        if (wasHover != G.closeBtnHover)
            InvalidateRect(hwnd, &G.closeBtnRect, FALSE);

        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        POINT pt = {mx, my};

        if (PtInRect(&G.closeBtnRect, pt)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }

        // Minimize / Maximize buttons (approximate rects)
        RECT cr; GetClientRect(hwnd, &cr);
        int cw = cr.right;
        RECT minR = {cw-S(120), 0, cw-S(80), S(TITLEBAR_H)};
        RECT maxR = {cw-S(80),  0, cw-S(40), S(TITLEBAR_H)};

        if (PtInRect(&minR, pt)) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        if (PtInRect(&maxR, pt)) {
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }

        // Title bar drag
        if (my < S(TITLEBAR_H)) {
            G.isWinDragging = true;
            GetCursorPos(&G.winDragStart);
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        G.isWinDragging = false;
        ReleaseCapture();
        return 0;

    case WM_LBUTTONDBLCLK: {
        int my = GET_Y_LPARAM(lp);
        if (my < S(TITLEBAR_H))
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        return 0;
    }

    // ── Keyboard shortcuts ────────────────────────────────────────────────
    case WM_KEYDOWN: {
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

        if (ctrl) {
            switch (wp) {
            case 'O': HandleCommand(hwnd, ID_BTN_OPEN);     break;
            case 'S': HandleCommand(hwnd, shift ? ID_BTN_SAVE_AS : ID_BTN_SAVE); break;
            case 'Z': HandleCommand(hwnd, ID_BTN_UNDO);     break;
            case 'Y': HandleCommand(hwnd, ID_BTN_REDO);     break;
            case 'P': HandleCommand(hwnd, ID_BTN_PRINT);    break;
            case 'G': HandleCommand(hwnd, ID_TOOL_SIGNATURE); break;
            case VK_OEM_PLUS:  case VK_ADD:
                HandleCommand(hwnd, ID_BTN_ZOOM_IN);  break;
            case VK_OEM_MINUS: case VK_SUBTRACT:
                HandleCommand(hwnd, ID_BTN_ZOOM_OUT); break;
            case '0': HandleCommand(hwnd, ID_BTN_ZOOM_FIT); break;
            }
        } else {
            switch (wp) {
            case VK_LEFT:  case VK_PRIOR: HandleCommand(hwnd, ID_BTN_PREV); break;
            case VK_RIGHT: case VK_NEXT:  HandleCommand(hwnd, ID_BTN_NEXT); break;
            case VK_F11: HandleCommand(hwnd, ID_BTN_FULLSCREEN); break;
            case 'H': SetActiveTool(Tool::Hand);      break;
            case 'V': SetActiveTool(Tool::Select);    break;
            case 'T': SetActiveTool(Tool::Text);      break;
            case 'D': SetActiveTool(Tool::Draw);      break;
            case 'E': SetActiveTool(Tool::Eraser);    break;
            case 'R': SetActiveTool(Tool::Rectangle); break;
            case 'L': SetActiveTool(Tool::Ellipse);   break;
            case 'A': SetActiveTool(Tool::Arrow);     break;
            case 'N': SetActiveTool(Tool::StickyNote);break;
            case 'M': SetActiveTool(Tool::Measure);   break;
            case VK_DELETE: DeleteSelectedAnnot();    break;
            }
        }
        return 0;
    }

    // ── WM_COMMAND ────────────────────────────────────────────────────────
    case WM_COMMAND:
        HandleCommand(hwnd, LOWORD(wp));
        return 0;

    // ── App messages ─────────────────────────────────────────────────────
    case APP_REPAINT_CANVAS:
        InvalidateRect(G.hwndCanvas, nullptr, FALSE);
        return 0;
    case APP_REPAINT_SIDEBAR:
        InvalidateRect(G.hwndSidebar, nullptr, FALSE);
        return 0;
    case APP_STATUS_UPDATE:
        InvalidateRect(G.hwndStatus, nullptr, FALSE);
        return 0;
    case APP_PAGE_CHANGED:
        InvalidateRect(G.hwndStatus, nullptr, FALSE);
        InvalidateRect(G.hwndSidebar, nullptr, FALSE);
        return 0;

    // ── Drag-and-drop PDF ─────────────────────────────────────────────────
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wp;
        wchar_t path[MAX_PATH] = {};
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
            wstring ext = path;
            if (ext.size() >= 4) {
                wstring suffix = ext.substr(ext.size() - 4);
                for (auto& c : suffix) c = towlower(c);
                if (suffix == L".pdf") OpenDocument(path);
                else MessageBoxW(hwnd, L"Please drop a .pdf file.", L"Wrong File", MB_ICONWARNING);
            }
        }
        DragFinish(hDrop);
        return 0;
    }

    // ── Hit-test for resizing (borderless window) ─────────────────────────
    case WM_NCHITTEST: {
        LRESULT def = DefWindowProcW(hwnd, msg, wp, lp);
        if (def == HTCLIENT) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y < S(TITLEBAR_H)) return HTCAPTION;
        }
        return def;
    }

    // ── Close ─────────────────────────────────────────────────────────────
    case WM_CLOSE:
        if (G.isModified) {
            int r = MessageBoxW(hwnd, L"Save changes before closing?",
                                L"Unsaved Changes",
                                MB_YESNOCANCEL | MB_ICONWARNING);
            if (r == IDCANCEL) return 0;
            if (r == IDYES) {
                if (!G.filePath.empty()) SaveDocument(G.filePath);
                else {
                    wstring p = ShowSaveDialog(hwnd, {});
                    if (!p.empty()) SaveDocument(p);
                }
            }
        }
        CloseDocument();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        FPDF_DestroyLibrary();
        GdiplusShutdown(G.gdipToken);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// =============================================================================
//  SECTION 18 – WINDOW CLASS REGISTRATION
// =============================================================================

static void RegisterAllClasses(HINSTANCE hInst) {
    auto reg = [&](const wchar_t* name, WNDPROC proc,
                   HBRUSH bg = (HBRUSH)(COLOR_WINDOW+1)) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = proc;
        wc.hInstance     = hInst;
        wc.lpszClassName = name;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = bg;
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
    };

    reg(APP_CLASS,     MainWndProc,    (HBRUSH)GetStockObject(BLACK_BRUSH));
    reg(TOOLBAR_CLASS, ToolbarProc,    (HBRUSH)GetStockObject(BLACK_BRUSH));
    reg(SIDEBAR_CLASS, SidebarProc,    (HBRUSH)GetStockObject(BLACK_BRUSH));
    reg(CANVAS_CLASS,  CanvasProc,     (HBRUSH)GetStockObject(DKGRAY_BRUSH));
    reg(STATUS_CLASS,  StatusProc,     (HBRUSH)GetStockObject(BLACK_BRUSH));
}

// =============================================================================
//  SECTION 19 – ENTRY POINT
// =============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // High-DPI awareness
    SetProcessDPIAware();

    // Common controls
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // GDI+
    GdiplusStartupInput gdipInput;
    GdiplusStartup(&G.gdipToken, &gdipInput, nullptr);

    RegisterAllClasses(hInstance);

    // Create main window (borderless + resizable)
    G.hwndMain = CreateWindowExW(
        WS_EX_APPWINDOW,
        APP_CLASS, APP_NAME,
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1400, 860,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!G.hwndMain) {
        MessageBoxW(nullptr, L"Window creation failed.", L"Fatal Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(G.hwndMain, nCmdShow);
    UpdateWindow(G.hwndMain);

    // Check for command-line PDF argument
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2) {
            OpenDocument(argv[1]);
        }
        LocalFree(argv);
    }

    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

// =============================================================================
//  END OF FILE  –  RasPDF Pro Max  main.cpp
//  Line count: ~3100+
// =============================================================================
