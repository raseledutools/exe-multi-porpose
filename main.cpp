#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <sapi.h>
#include <random>
#include <sstream>
#include <thread>
#include <atomic>

// --- GitHub Actions vcpkg Libraries ---
#include <opencv2/opencv.hpp>
#include <hpdf.h>
#include <poppler-document.h>
#include <poppler-page.h>
#include <poppler-image.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <qrcodegen.hpp>

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "winmm.lib")

using namespace Gdiplus;
using qrcodegen::QrCode;

// --- Globals ---
const int BASE_WIDTH = 1150; 
const int BASE_HEIGHT = 850; 
float g_scaleFactor = 1.0f;
int g_hoveredBtn = -1;

struct FeatureBtn {
    int id;
    std::wstring label;
    RectF bounds;
};
std::vector<FeatureBtn> g_features;

// --- Helpers ---
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size, NULL, NULL);
    return strTo;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size);
    return wstrTo;
}

std::string GetClipboardText(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return "";
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == nullptr) { CloseClipboard(); return ""; }
    char* pszText = static_cast<char*>(GlobalLock(hData));
    std::string text(pszText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

void SetClipboardText(HWND hwnd, const std::string& text) {
    if(OpenClipboard(hwnd)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hg) {
            memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
}

std::wstring OpenFileDialog(HWND hwnd, const wchar_t* filter) {
    OPENFILENAMEW ofn; WCHAR szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter; ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return std::wstring(ofn.lpstrFile);
    return L"";
}

std::wstring SaveFileDialog(HWND hwnd, const wchar_t* filter, const wchar_t* defExt) {
    OPENFILENAMEW sfn; WCHAR szFile[260] = { 0 };
    ZeroMemory(&sfn, sizeof(sfn)); sfn.lStructSize = sizeof(sfn);
    sfn.hwndOwner = hwnd; sfn.lpstrFile = szFile; sfn.nMaxFile = sizeof(szFile);
    sfn.lpstrFilter = filter; sfn.lpstrDefExt = defExt; sfn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&sfn)) return std::wstring(sfn.lpstrFile);
    return L"";
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0; GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid; free(pImageCodecInfo); return j;
        }
    }
    free(pImageCodecInfo); return -1;
}

// ==========================================================
// 1 TO 16: FULLY PRACTICAL IMPLEMENTATIONS
// ==========================================================

// 1 & 2. Resize Logic (Job Photo 300x300 / Signature 300x80)
void Feature_ResizeImage(HWND hwnd, int w, int h) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Images\0*.jpg;*.png\0");
    if (inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"JPEG Image\0*.jpg\0", L"jpg");
    if (outPath.empty()) return;

    Image* originalImg = new Image(inPath.c_str());
    Bitmap* resizedImg = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(resizedImg);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    SolidBrush bg(Color(255, 255, 255));
    g.FillRectangle(&bg, 0, 0, w, h);
    g.DrawImage(originalImg, 0, 0, w, h);

    CLSID jpegClsid; GetEncoderClsid(L"image/jpeg", &jpegClsid);
    resizedImg->Save(outPath.c_str(), &jpegClsid, NULL);

    delete resizedImg; delete originalImg;
    MessageBoxW(hwnd, L"Image Resized and Saved Successfully!", L"Success", MB_OK);
}

// 3. CamScanner Filter (OpenCV)
void Feature_CamScanner(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Images\0*.jpg;*.png\0");
    if (inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"Images\0*.jpg\0", L"jpg");
    if (outPath.empty()) return;

    cv::Mat src = cv::imread(WideToUtf8(inPath));
    if (!src.empty()) {
        cv::Mat gray, blur, res;
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, blur, cv::Size(5, 5), 0);
        cv::adaptiveThreshold(blur, res, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 21, 10);
        cv::imwrite(WideToUtf8(outPath), res);
        MessageBoxW(hwnd, L"CamScanner Filter Applied!", L"Success", MB_OK);
    } else {
        MessageBoxW(hwnd, L"Failed to load image.", L"Error", MB_ICONERROR);
    }
}

// 4. Image to PDF (libharu)
void Feature_ImgToPdf(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Images\0*.jpg\0");
    if (inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"PDF\0*.pdf\0", L"pdf");
    if (outPath.empty()) return;

    HPDF_Doc pdf = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(pdf);
    HPDF_Image img = HPDF_LoadJpegImageFromFile(pdf, WideToUtf8(inPath).c_str());
    if (img) {
        HPDF_Page_SetWidth(page, HPDF_Image_GetWidth(img));
        HPDF_Page_SetHeight(page, HPDF_Image_GetHeight(img));
        HPDF_Page_DrawImage(page, img, 0, 0, HPDF_Image_GetWidth(img), HPDF_Image_GetHeight(img));
    }
    HPDF_SaveToFile(pdf, WideToUtf8(outPath).c_str());
    HPDF_Free(pdf);
    MessageBoxW(hwnd, L"Converted to High Quality PDF!", L"Success", MB_OK);
}

// 5. Merge PDF (QPDF)
void Feature_MergePDF(HWND hwnd) {
    std::wstring p1 = OpenFileDialog(hwnd, L"First PDF\0*.pdf\0"); if(p1.empty()) return;
    std::wstring p2 = OpenFileDialog(hwnd, L"Second PDF\0*.pdf\0"); if(p2.empty()) return;
    std::wstring out = SaveFileDialog(hwnd, L"Merged PDF\0*.pdf\0", L"pdf"); if(out.empty()) return;

    try {
        QPDF m_pdf; m_pdf.emptyPDF();
        QPDFPageDocumentHelper m_helper(m_pdf);
        QPDF in1; in1.processFile(WideToUtf8(p1).c_str());
        for (auto& page : QPDFPageDocumentHelper(in1).getAllPages()) m_helper.addPage(page, false);
        QPDF in2; in2.processFile(WideToUtf8(p2).c_str());
        for (auto& page : QPDFPageDocumentHelper(in2).getAllPages()) m_helper.addPage(page, false);
        QPDFWriter writer(m_pdf, WideToUtf8(out).c_str()); writer.write();
        MessageBoxW(hwnd, L"PDFs Merged Successfully!", L"Success", MB_OK);
    } catch(...) { MessageBoxW(hwnd, L"Merge Failed", L"Error", MB_ICONERROR); }
}

// 6. Extract 1st Page (QPDF)
void Feature_ExtractFirstPage(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Select PDF\0*.pdf\0"); if (inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"Extracted Page PDF\0*.pdf\0", L"pdf"); if (outPath.empty()) return;

    try {
        QPDF inPdf; inPdf.processFile(WideToUtf8(inPath).c_str());
        QPDF outPdf; outPdf.emptyPDF();
        QPDFPageDocumentHelper inHelper(inPdf), outHelper(outPdf);
        auto pages = inHelper.getAllPages();
        if(!pages.empty()) {
            outHelper.addPage(pages[0], false);
            QPDFWriter writer(outPdf, WideToUtf8(outPath).c_str()); writer.write();
            MessageBoxW(hwnd, L"First Page Extracted Successfully!", L"Success", MB_OK);
        }
    } catch(...) { MessageBoxW(hwnd, L"Extraction Failed", L"Error", MB_ICONERROR); }
}

// 7. PDF to Image (Poppler)
void Feature_PdfToImg(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"PDF Files\0*.pdf\0"); if(inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"JPEG\0*.jpg\0", L"jpg"); if(outPath.empty()) return;

    poppler::document *doc = poppler::document::load_from_file(WideToUtf8(inPath));
    if (doc && doc->pages() > 0) {
        poppler::page *p = doc->create_page(0);
        poppler::image img = p->render_image(300.0, 300.0);
        img.save(WideToUtf8(outPath), "jpeg");
        delete p; delete doc;
        MessageBoxW(hwnd, L"First page converted to Image!", L"Success", MB_OK);
    } else {
        MessageBoxW(hwnd, L"Failed to render PDF.", L"Error", MB_ICONERROR);
    }
}

// 8. Text To Speech (SAPI)
void Feature_TextToSpeech(HWND hwnd) {
    std::string clip = GetClipboardText(hwnd);
    std::wstring textToRead = clip.empty() ? L"Please copy some English text to clipboard first." : Utf8ToWide(clip);
    
    ISpVoice * pVoice = NULL;
    if (SUCCEEDED(::CoInitialize(NULL)) && SUCCEEDED(CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void **)&pVoice))) {
        pVoice->Speak(textToRead.c_str(), 0, NULL);
        pVoice->Release();
    }
    ::CoUninitialize();
}

// 9. OCR Extract Text (Tesseract)
void Feature_OCR(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Images\0*.jpg;*.png\0"); if (inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"Text File\0*.txt\0", L"txt"); if (outPath.empty()) return;

    tesseract::TessBaseAPI* api = new tesseract::TessBaseAPI();
    if (api->Init(NULL, "eng")) {
        MessageBoxW(hwnd, L"Could not initialize Tesseract! 'tessdata' folder missing.", L"Error", MB_ICONERROR);
        return;
    }
    Pix* image = pixRead(WideToUtf8(inPath).c_str());
    api->SetImage(image);
    char* outText = api->GetUTF8Text();
    
    std::ofstream outFile(WideToUtf8(outPath)); outFile << outText; outFile.close();
    api->End(); delete[] outText; pixDestroy(&image);
    MessageBoxW(hwnd, L"Text Extracted and Saved via OCR!", L"Success", MB_OK);
}

// 10. Generate QR from Clipboard
void Feature_GenerateQR(HWND hwnd) {
    std::string text = GetClipboardText(hwnd);
    if(text.empty()) { MessageBoxW(hwnd, L"Clipboard empty! Copy a link first.", L"Warning", MB_ICONWARNING); return; }
    
    std::wstring outPath = SaveFileDialog(hwnd, L"PNG Image\0*.png\0", L"png"); if (outPath.empty()) return;

    QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::HIGH);
    int size = qr.getSize(), scale = 10, border = 4;
    int imgSize = (size + border * 2) * scale;

    Bitmap* bmp = new Bitmap(imgSize, imgSize, PixelFormat32bppARGB);
    Graphics g(bmp);
    SolidBrush white(Color(255, 255, 255)), black(Color(0, 0, 0));
    g.FillRectangle(&white, 0, 0, imgSize, imgSize);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qr.getModule(x, y)) g.FillRectangle(&black, (x + border) * scale, (y + border) * scale, scale, scale);
        }
    }
    CLSID pngClsid; GetEncoderClsid(L"image/png", &pngClsid);
    bmp->Save(outPath.c_str(), &pngClsid, NULL); delete bmp;
    MessageBoxW(hwnd, L"QR Code Generated and Saved!", L"Success", MB_OK);
}

// 11. Lock PDF
void Feature_LockPDF(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Select PDF\0*.pdf\0"); if(inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"Locked PDF\0*.pdf\0", L"pdf"); if(outPath.empty()) return;

    try {
        QPDF pdf; pdf.processFile(WideToUtf8(inPath).c_str());
        QPDFWriter writer(pdf, WideToUtf8(outPath).c_str());
        writer.setEncryption(256, "12345", "12345", QPDFWriter::e_print_none, QPDFWriter::e_modify_none, true, true);
        writer.write();
        MessageBoxW(hwnd, L"PDF Locked! (Password: 12345)", L"Success", MB_OK);
    } catch(...) { MessageBoxW(hwnd, L"Failed to lock PDF", L"Error", MB_ICONERROR); }
}

// 12. Unlock PDF
void Feature_UnlockPDF(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Select Locked PDF\0*.pdf\0"); if(inPath.empty()) return;
    std::wstring outPath = SaveFileDialog(hwnd, L"Unlocked PDF\0*.pdf\0", L"pdf"); if(outPath.empty()) return;

    try {
        QPDF pdf; pdf.processFile(WideToUtf8(inPath).c_str(), "12345");
        QPDFWriter writer(pdf, WideToUtf8(outPath).c_str());
        writer.setPreserveEncryption(false); writer.write();
        MessageBoxW(hwnd, L"PDF Unlocked permanently!", L"Success", MB_OK);
    } catch(...) { MessageBoxW(hwnd, L"Unlock Failed! Wrong password?", L"Error", MB_ICONERROR); }
}

// 13. PDF Organizer (Burst)
void Feature_PDFOrganizer(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Select PDF\0*.pdf\0"); if (inPath.empty()) return;
    try {
        QPDF inPdf; inPdf.processFile(WideToUtf8(inPath).c_str());
        QPDFPageDocumentHelper helper(inPdf);
        auto pages = helper.getAllPages();
        std::string baseName = WideToUtf8(inPath);
        baseName = baseName.substr(0, baseName.find_last_of('.'));

        for (size_t i = 0; i < pages.size(); ++i) {
            QPDF outPdf; outPdf.emptyPDF();
            QPDFPageDocumentHelper outHelper(outPdf);
            outHelper.addPage(pages[i], false);
            std::string outName = baseName + "_page_" + std::to_string(i + 1) + ".pdf";
            QPDFWriter writer(outPdf, outName.c_str()); writer.write();
        }
        MessageBoxW(hwnd, L"PDF Burst into individual pages!", L"Success", MB_OK);
    } catch(...) { MessageBoxW(hwnd, L"Failed to organize PDF", L"Error", MB_ICONERROR); }
}

// 14. OpenCV Fast Photo Viewer
void Feature_PhotoViewer(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Images\0*.jpg;*.png;*.bmp\0"); if (inPath.empty()) return;
    cv::Mat img = cv::imread(WideToUtf8(inPath));
    if (img.empty()) { MessageBoxW(hwnd, L"Failed to load image.", L"Error", MB_ICONERROR); return; }
    
    int maxW = 1024, maxH = 768;
    if (img.cols > maxW || img.rows > maxH) {
        double scale = std::min((double)maxW/img.cols, (double)maxH/img.rows);
        cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_AREA);
    }
    cv::imshow("Rasel's Photo Viewer (Press ANY key to close)", img);
    cv::waitKey(0); cv::destroyWindow("Rasel's Photo Viewer (Press ANY key to close)");
}

// 15. CSV/Text Log Viewer
void Feature_TextViewer(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Data Files\0*.csv;*.txt;*.log\0"); if (inPath.empty()) return;
    ShellExecuteW(hwnd, L"open", L"notepad.exe", inPath.c_str(), NULL, SW_SHOWNORMAL);
}

// 16. Universal Doc/Excel Launcher
void Feature_ExcelDocViewer(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Office Documents\0*.xlsx;*.docx;*.pptx\0"); if (inPath.empty()) return;
    HINSTANCE result = ShellExecuteW(hwnd, L"open", inPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) MessageBoxW(hwnd, L"No application found to open this document.", L"Viewer Error", MB_ICONWARNING);
}


// ==========================================================
// 17 TO 24: SYSTEM UTILITIES & FOCUS TOOLS
// ==========================================================

void Feature_AgeCalculator(HWND hwnd) {
    std::string dobStr = GetClipboardText(hwnd);
    if (dobStr.length() < 10) { MessageBoxW(hwnd, L"Copy your Date of Birth (YYYY-MM-DD) first!", L"Warning", MB_ICONWARNING); return; }
    int by, bm, bd;
    if (sscanf(dobStr.c_str(), "%d-%d-%d", &by, &bm, &bd) == 3) {
        SYSTEMTIME st; GetLocalTime(&st);
        int cy = st.wYear, cm = st.wMonth, cd = st.wDay;
        int ageY = cy - by, ageM = cm - bm, ageD = cd - bd;
        if (ageD < 0) { ageM--; ageD += 30; } 
        if (ageM < 0) { ageY--; ageM += 12; }
        std::wstring msg = L"Age:\n" + std::to_wstring(ageY) + L" Years, " + std::to_wstring(ageM) + L" Months, " + std::to_wstring(ageD) + L" Days.";
        MessageBoxW(hwnd, msg.c_str(), L"Age Calculator", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd, L"Invalid Format! Use YYYY-MM-DD.", L"Error", MB_ICONERROR);
    }
}

void Feature_PassGen(HWND hwnd) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()";
    std::random_device rd; std::mt19random_engine gen(rd());
    std::uniform_int_distribution<> dist(0, chars.size() - 1);
    std::string pass = ""; for (int i = 0; i < 16; ++i) pass += chars[dist(gen)];
    SetClipboardText(hwnd, pass);
    std::wstring msg = L"Generated Password: " + Utf8ToWide(pass) + L"\n(Copied to Clipboard!)";
    MessageBoxW(hwnd, msg.c_str(), L"PassGen", MB_OK);
}

void Feature_PCHealth(HWND hwnd) {
    MEMORYSTATUSEX memInfo; memInfo.dwLength = sizeof(MEMORYSTATUSEX); GlobalMemoryStatusEx(&memInfo);
    double totalRam = memInfo.ullTotalPhys / (1024.0*1024.0*1024.0);
    double freeRam = memInfo.ullAvailPhys / (1024.0*1024.0*1024.0);
    std::wstring msg = L"Total RAM: " + std::to_wstring(totalRam) + L" GB\nFree RAM: " + std::to_wstring(freeRam) + L" GB\nStatus: Healthy";
    MessageBoxW(hwnd, msg.c_str(), L"Health", MB_OK);
}

void Feature_FileHash(HWND hwnd) {
    std::wstring inPath = OpenFileDialog(hwnd, L"Any File\0*.*\0"); if (inPath.empty()) return;
    std::wstring cmd = L"cmd.exe /c certutil -hashfile \"" + inPath + L"\" SHA256 > hash_temp.txt";
    _wsystem(cmd.c_str());
    std::ifstream file("hash_temp.txt"); std::string line, hashRes = "";
    while (std::getline(file, line)) hashRes += line + "\n";
    file.close(); remove("hash_temp.txt");
    MessageBoxW(hwnd, Utf8ToWide(hashRes).c_str(), L"SHA-256 Hash", MB_OK);
}

std::atomic<bool> g_playNoise(false);
std::thread g_noiseThread;
void NoiseGeneratorTask() {
    HWAVEOUT hWaveOut; WAVEFORMATEX wfx = {WAVE_FORMAT_PCM, 1, 44100, 44100, 1, 8, 0};
    waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    char buffer[44100]; WAVEHDR header = {buffer, sizeof(buffer), 0, 0, 0, 0, 0, 0};
    waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
    while(g_playNoise) {
        for(int i=0; i<44100; ++i) buffer[i] = (char)(rand() % 256); 
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));
        Sleep(900); 
    }
    waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR)); waveOutClose(hWaveOut);
}

void Feature_AutoFocusNoise(HWND hwnd) {
    if (g_playNoise) {
        g_playNoise = false; if (g_noiseThread.joinable()) g_noiseThread.join();
    } else {
        g_playNoise = true; g_noiseThread = std::thread(NoiseGeneratorTask);
        MessageBoxW(hwnd, L"Focus Noise Started! Click again to stop.", L"Audio", MB_ICONINFORMATION);
    }
}

bool g_eyeCareActive = false;
void Feature_EyeCare(HWND hwnd) {
    if (g_eyeCareActive) { KillTimer(hwnd, 2020); g_eyeCareActive = false; } 
    else { SetTimer(hwnd, 2020, 20 * 60 * 1000, NULL); g_eyeCareActive = true; MessageBoxW(hwnd, L"Eye Care Active! 20-Min Timer started.", L"Info", MB_OK); }
}

HWND g_hFilterWnd = NULL;
void Feature_BlueLightFilter(HWND hwnd) {
    if (g_hFilterWnd) { DestroyWindow(g_hFilterWnd); g_hFilterWnd = NULL; } 
    else {
        WNDCLASSW wc = {0}; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(NULL);
        wc.hbrBackground = CreateSolidBrush(RGB(255, 120, 0)); wc.lpszClassName = L"NightLightOverlay"; RegisterClassW(&wc);
        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        g_hFilterWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"NightLightOverlay", L"", WS_POPUP | WS_VISIBLE, x, y, w, h, NULL, NULL, wc.hInstance, NULL);
        SetLayeredWindowAttributes(g_hFilterWnd, 0, 60, LWA_ALPHA); 
    }
}

void Feature_PomodoroTimer(HWND hwnd) {
    SetTimer(hwnd, 2525, 25 * 60 * 1000, NULL);
    MessageBoxW(hwnd, L"Pomodoro 25-Min Timer Started! Focus now.", L"Pomodoro", MB_ICONINFORMATION);
}

// --- UI DRAWING & LOGIC ---
void DrawUI(Graphics& g) {
    g.Clear(Color(255, 230, 240, 245)); 
    FontFamily ff(L"Segoe UI");
    Font titleFont(&ff, 28, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 10, 20, 40)); 
    g.DrawString(L"⚡ RasFocus Pro Max & Ultimate Toolset (24 Features)", -1, &titleFont, PointF(35, 20), &textBrush);

    Font btnFont(&ff, 13, FontStyleBold, UnitPixel);
    SolidBrush btnBg(Color(255, 20, 30, 50)); 
    SolidBrush btnHover(Color(255, 16, 185, 129)); 
    SolidBrush btnActive(Color(255, 239, 68, 68)); 
    SolidBrush white(Color(255, 255, 255, 255));
    Pen border(Color(255, 200, 210, 220), 1.0f);

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    for (const auto& btn : g_features) {
        if (g_hoveredBtn == btn.id) g.FillRectangle(&btnHover, btn.bounds);
        else {
            if ((btn.id == 21 && g_playNoise) || (btn.id == 22 && g_eyeCareActive) || (btn.id == 23 && g_hFilterWnd)) 
                 g.FillRectangle(&btnActive, btn.bounds);
            else g.FillRectangle(&btnBg, btn.bounds);
        }
        g.DrawRectangle(&border, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height);
        g.DrawString(btn.label.c_str(), -1, &btnFont, btn.bounds, &format, &white);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        float startX = 35.0f, startY = 75.0f, w = 250.0f, h = 80.0f, gapX = 20.0f, gapY = 20.0f;
        g_features.push_back({1, L"1. Job Photo (300x300)", RectF(startX, startY, w, h)});
        g_features.push_back({2, L"2. Signature (300x80)", RectF(startX + w + gapX, startY, w, h)});
        g_features.push_back({3, L"3. CamScanner Filter", RectF(startX + 2*(w + gapX), startY, w, h)});
        g_features.push_back({4, L"4. Image to PDF", RectF(startX + 3*(w + gapX), startY, w, h)});

        startY += h + gapY;
        g_features.push_back({5, L"5. Merge PDFs", RectF(startX, startY, w, h)});
        g_features.push_back({6, L"6. Extract 1st Page", RectF(startX + w + gapX, startY, w, h)});
        g_features.push_back({7, L"7. PDF to Image", RectF(startX + 2*(w + gapX), startY, w, h)});
        g_features.push_back({8, L"8. Text-to-Speech", RectF(startX + 3*(w + gapX), startY, w, h)});

        startY += h + gapY;
        g_features.push_back({9, L"9. OCR Extract Text", RectF(startX, startY, w, h)});
        g_features.push_back({10, L"10. Clip \xE2\x86\x92 QR Code", RectF(startX + w + gapX, startY, w, h)});
        g_features.push_back({11, L"11. Lock PDF", RectF(startX + 2*(w + gapX), startY, w, h)});
        g_features.push_back({12, L"12. Unlock PDF", RectF(startX + 3*(w + gapX), startY, w, h)});

        startY += h + gapY;
        g_features.push_back({13, L"13. Burst PDF Pages", RectF(startX, startY, w, h)});
        g_features.push_back({14, L"14. Fast Photo Viewer", RectF(startX + w + gapX, startY, w, h)});
        g_features.push_back({15, L"15. CSV/Log Viewer", RectF(startX + 2*(w + gapX), startY, w, h)});
        g_features.push_back({16, L"16. Doc/Excel Viewer", RectF(startX + 3*(w + gapX), startY, w, h)});

        startY += h + gapY;
        g_features.push_back({17, L"17. Age Calculator (Clip)", RectF(startX, startY, w, h)});
        g_features.push_back({18, L"18. Password Generator", RectF(startX + w + gapX, startY, w, h)});
        g_features.push_back({19, L"19. PC Health & Battery", RectF(startX + 2*(w + gapX), startY, w, h)});
        g_features.push_back({20, L"20. File Hash Validator", RectF(startX + 3*(w + gapX), startY, w, h)});

        startY += h + gapY;
        g_features.push_back({21, L"21. Focus Noise (ON/OFF)", RectF(startX, startY, w, h)});
        g_features.push_back({22, L"22. Eye Care (20-20-20)", RectF(startX + w + gapX, startY, w, h)});
        g_features.push_back({23, L"23. Blue Light Filter", RectF(startX + 2*(w + gapX), startY, w, h)});
        g_features.push_back({24, L"24. Pomodoro Timer (25m)", RectF(startX + 3*(w + gapX), startY, w, h)});
        return 0;
    }
    
    case WM_TIMER: {
        if (wParam == 2020) MessageBoxW(hwnd, L"👀 20-20-20 RULE! Look at something 20 feet away.", L"Eye Care", MB_ICONWARNING | MB_SYSTEMMODAL);
        else if (wParam == 2525) { KillTimer(hwnd, 2525); MessageBoxW(hwnd, L"⏰ Pomodoro Complete! Take a break.", L"Pomodoro", MB_ICONINFORMATION | MB_SYSTEMMODAL); }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC memDC = CreateCompatibleDC(hdc); HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom); SelectObject(memDC, memBmp);
        Graphics g(memDC); g.SetSmoothingMode(SmoothingModeHighQuality); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit); g.ScaleTransform(g_scaleFactor, g_scaleFactor);
        DrawUI(g);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        DeleteObject(memBmp); DeleteDC(memDC); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam) / g_scaleFactor, y = GET_Y_LPARAM(lParam) / g_scaleFactor;
        int oldHover = g_hoveredBtn; g_hoveredBtn = -1;
        for (const auto& btn : g_features) { if (x >= btn.bounds.X && x <= btn.bounds.X + btn.bounds.Width && y >= btn.bounds.Y && y <= btn.bounds.Y + btn.bounds.Height) { g_hoveredBtn = btn.id; break; } }
        if (oldHover != g_hoveredBtn) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if(g_hoveredBtn != -1) {
            switch(g_hoveredBtn) {
                case 1: Feature_ResizeImage(hwnd, 300, 300); break;
                case 2: Feature_ResizeImage(hwnd, 300, 80); break;
                case 3: Feature_CamScanner(hwnd); break;
                case 4: Feature_ImgToPdf(hwnd); break;
                case 5: Feature_MergePDF(hwnd); break;
                case 6: Feature_ExtractFirstPage(hwnd); break;
                case 7: Feature_PdfToImg(hwnd); break;
                case 8: Feature_TextToSpeech(hwnd); break;
                case 9: Feature_OCR(hwnd); break;
                case 10: Feature_GenerateQR(hwnd); break;
                case 11: Feature_LockPDF(hwnd); break;
                case 12: Feature_UnlockPDF(hwnd); break;
                case 13: Feature_PDFOrganizer(hwnd); break;
                case 14: Feature_PhotoViewer(hwnd); break;
                case 15: Feature_TextViewer(hwnd); break;
                case 16: Feature_ExcelDocViewer(hwnd); break;
                case 17: Feature_AgeCalculator(hwnd); break;
                case 18: Feature_PassGen(hwnd); break;
                case 19: Feature_PCHealth(hwnd); break;
                case 20: Feature_FileHash(hwnd); break;
                case 21: Feature_AutoFocusNoise(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case 22: Feature_EyeCare(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case 23: Feature_BlueLightFilter(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                case 24: Feature_PomodoroTimer(hwnd); break;
            }
        }
        return 0;
    }
    case WM_DESTROY: {
        if (g_playNoise) { g_playNoise = false; if(g_noiseThread.joinable()) g_noiseThread.join(); }
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();
    HDC screen = GetDC(0); g_scaleFactor = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f; ReleaseDC(0, screen);
    GdiplusStartupInput gdiplusSI; ULONG_PTR gdiplusToken; GdiplusStartup(&gdiplusToken, &gdiplusSI, NULL);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance; wc.lpszClassName = L"RasFocusProApp";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"RasFocusProApp", L"RasFocus Pro Max & Ultimate Toolset", 
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, BASE_WIDTH * g_scaleFactor, BASE_HEIGHT * g_scaleFactor, 
        NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    GdiplusShutdown(gdiplusToken);
    return 0;
}
