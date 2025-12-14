#define _UNICODE

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <richedit.h>
#include <commctrl.h>

#include <string>
#include <thread>
#include <sstream>
#include <vector>
#include <map>

#include <fstream>
#include <set>
#include <filesystem>

#include "imessage_convert.hpp"
#include "android_sms_convert.hpp"

namespace fs = std::filesystem;

// Analysis entry point
std::string runAnalysisToString(const std::string& inputPathStr);

// Discord conversion
bool ConvertDiscordToInstagramFolder(const std::string& inputPathStr,
                                     const std::string& outputPathStr,
                                     const std::string& chatTitle,
                                     std::string&      errorOut);
// Whatsapp conversion 
bool ConvertWhatsAppToInstagramFolder(
    const std::string& inputPathStr,
    const std::string& outputPathStr,
    const std::string& chatTitle,
    std::string&       errorOut
    );             
// ============================================================================
// Shared analytics types 
// ============================================================================
struct MonthlyEmotionPoint
{
    int year;
    int month;
    double avgCompound;
};

struct MonthlyCountPoint
{
    int year;
    int month;
    long long totalMessages;
};

struct MonthlyResponsePoint
{
    int year;
    int month;
    double avgMinutes;
};

struct MonthlyRomanticPoint
{
    int year;
    int month;
    long long romanticMessages;
};

struct MonthlyAvgLengthPoint
{
    int year;
    int month;
    double avgWords;
};

// Per-user series for charts
struct UserMonthlyCountPoint
{
    int year;
    int month;
    long long totalMessages;
};

struct UserMonthlyEmotionPoint
{
    int year;
    int month;
    double avgCompound;
};

struct UserMonthlyResponsePoint
{
    int year;
    int month;
    double avgMinutes;
};

struct UserMonthlyRomanticPoint
{
    int year;
    int month;
    long long romanticMessages;
};

struct UserMonthlyAvgLengthPoint
{
    int year;
    int month;
    double avgWords;
};

// Global analytics 
extern int  g_heatmapCounts[7][24];
extern bool g_heatmapReady;

extern std::vector<MonthlyEmotionPoint>        g_monthlyEmotionPoints;
extern std::vector<MonthlyCountPoint>          g_monthlyCountPoints;
extern std::vector<MonthlyResponsePoint>       g_monthlyResponsePoints;
extern std::vector<MonthlyRomanticPoint>       g_monthlyRomanticPoints;
extern std::vector<MonthlyAvgLengthPoint>      g_monthlyAvgLengthPoints;

extern std::vector<std::string>                g_chartUserNames;

extern std::vector<std::vector<UserMonthlyCountPoint>>      g_userMonthlyCountSeries;
extern std::vector<std::vector<UserMonthlyEmotionPoint>>    g_userMonthlyEmotionSeries;
extern std::vector<std::vector<UserMonthlyResponsePoint>>   g_userMonthlyResponseSeries;
extern std::vector<std::vector<UserMonthlyRomanticPoint>>   g_userMonthlyRomanticSeries;
extern std::vector<std::vector<UserMonthlyAvgLengthPoint>>  g_userMonthlyAvgLengthSeries;

// ============================================================================
// Control IDs / custom messages
// ============================================================================
#define IDM_CONTACT_BASE 5000   // base command ID for Android contact menu items

enum
{
    IDC_BTN_FILE      = 1001,
    IDC_BTN_FOLDER    = 1002,
    IDC_BTN_RUN       = 1003,
    IDC_BTN_DISCORD   = 1004,
    IDC_BTN_WHATSAPP  = 1005,
    IDC_BTN_IMESSAGE  = 1006,
    IDC_BTN_ANDROID   = 1007,

    IDC_TAB_MAIN      = 1500,

    IDC_EDIT_OUT      = 2001,
    IDC_VISUAL_CANVAS = 2002,

    IDC_STATUS        = 3001,

    WM_APP_ANALYSIS_COMPLETE = WM_APP + 1
};

// ============================================================================
// Globals
// ============================================================================
HINSTANCE    g_hInst         = nullptr;
HWND         g_hEdit         = nullptr;
HWND         g_hStatus       = nullptr;
HWND         g_hBtnFile      = nullptr;
HWND         g_hBtnFolder    = nullptr;
HWND         g_hBtnDiscord   = nullptr;
HWND         g_hBtnWhatsApp  = nullptr;
HWND         g_hBtnImessage  = nullptr;
HWND         g_hBtnAndroid   = nullptr;
HWND         g_hBtnRun       = nullptr;
HWND         g_hTab          = nullptr;
HWND         g_hVisualCanvas = nullptr;
HFONT        g_hUIFont       = nullptr;
HFONT        g_hHeadingFont  = nullptr;
std::wstring g_selectedPath;
HBRUSH       g_hBgBrush      = nullptr;

// ============================================================================
// String / formatting helpers
// ============================================================================

std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty())
        return std::wstring();

    int len = MultiByteToWideChar(
        CP_UTF8, 0,
        str.c_str(), (int)str.size(),
        nullptr, 0
    );

    std::wstring out(len, L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0,
        str.c_str(), (int)str.size(),
        &out[0], len
    );

    return out;
}

std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty())
        return std::string();

    int len = WideCharToMultiByte(
        CP_UTF8, 0,
        ws.c_str(), (int)ws.size(),
        nullptr, 0,
        nullptr, nullptr
    );

    std::string s(len, '\0');

    WideCharToMultiByte(
        CP_UTF8, 0,
        ws.c_str(), (int)ws.size(),
        &s[0], len,
        nullptr, nullptr
    );

    return s;
}

std::wstring formatWithCommasW(long long value)
{
    bool negative = value < 0;
    unsigned long long v =
        static_cast<unsigned long long>(negative ? -value : value);

    std::wstring s;
    if (v == 0)
    {
        s = L"0";
    }
    else
    {
        while (v > 0)
        {
            int digit = static_cast<int>(v % 10);
            s.insert(s.begin(), static_cast<wchar_t>(L'0' + digit));
            v /= 10;
        }
    }

    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(s.begin() + i, L',');

    if (negative)
        s.insert(s.begin(), L'-');

    return s;
}

std::wstring PickFolderModern(HWND hWnd, const wchar_t* title)
{
    std::wstring result;

    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd)
        return L"";

    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);

    if (title) pfd->SetTitle(title);

    hr = pfd->Show(hWnd);
    if (SUCCEEDED(hr))
    {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi)) && psi)
        {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
            {
                result = pszPath;
                CoTaskMemFree(pszPath);
            }
            psi->Release();
        }
    }

    pfd->Release();
    return result;
}


static std::wstring MakeMonthLabel(int month, int year)
{
    static const wchar_t* names[12] = {
        L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
        L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"
    };

    int idx = (month >= 1 && month <= 12) ? (month - 1) : 0;
    wchar_t buf[32];
    wsprintfW(buf, L"%s %02d", names[idx], year % 100);
    return buf;
}

static std::vector<int> ComputeXPositions(int count, const RECT& plotRect)
{
    std::vector<int> positions(count);
    int plotW = plotRect.right - plotRect.left;

    if (count == 1)
    {
        positions[0] = plotRect.left + plotW / 2;
        return positions;
    }

    for (int i = 0; i < count; ++i)
    {
        double xRatio = static_cast<double>(i) / static_cast<double>(count - 1);
        positions[i] = plotRect.left + static_cast<int>(xRatio * plotW);
    }

    return positions;
}

struct AndroidContactInfo
{
    std::string address;
    std::string contactName;
};

// simple attribute parser: attrName="value"
static bool ExtractXmlAttribute(const std::string& src,
                                const std::string& attrName,
                                std::string&       out)
{
    std::string pattern = attrName + "=\"";
    std::size_t pos = src.find(pattern);
    if (pos == std::string::npos)
        return false;

    pos += pattern.size();
    std::size_t end = src.find('"', pos);
    if (end == std::string::npos)
        return false;

    out.assign(src.begin() + static_cast<std::ptrdiff_t>(pos),
               src.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

static bool IsNullOrEmptyAttr(const std::string& s)
{
    return s.empty() || s == "null";
}

// Scan the XML once and collect unique (address, contact_name) pairs.
static bool GetAndroidContactsFromXml(
    const std::wstring& xmlPathW,
    std::vector<AndroidContactInfo>& outContacts,
    std::wstring& errorOutW)
{
    std::string xmlPathUtf8 = WideToUtf8(xmlPathW);
    std::ifstream in(xmlPathUtf8, std::ios::binary);
    if (!in)
    {
        errorOutW = L"Failed to open Android SMS XML file.";
        return false;
    }

    std::vector<AndroidContactInfo> contacts;
    std::set<std::pair<std::string,std::string>> seen;

    std::string line;
    bool inSms = false;
    std::string smsChunk;

    // Limit: we don't want millionsof contacts; but we'll scan entire file
    // for correctness. If performance is an issue, we can later add a cap.
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (!inSms)
        {
            std::size_t pos = line.find("<sms ");
            if (pos == std::string::npos)
                continue;

            inSms = true;
            smsChunk.clear();
            smsChunk.append(line.substr(pos));
            smsChunk.push_back('\n');

            if (smsChunk.find("/>") != std::string::npos)
            {
                inSms = false;
            }
            else
            {
                continue;
            }
        }
        else
        {
            smsChunk.append(line);
            smsChunk.push_back('\n');

            if (line.find("/>") == std::string::npos)
                continue;

            inSms = false;
        }

       
        std::string address;
        std::string contactName;

        ExtractXmlAttribute(smsChunk, "address",      address);
        ExtractXmlAttribute(smsChunk, "contact_name", contactName);

        if (IsNullOrEmptyAttr(address) && IsNullOrEmptyAttr(contactName))
            continue;

        if (!IsNullOrEmptyAttr(contactName) && contactName == "(Unknown)")
        {
            contactName.clear();
        }

        std::pair<std::string,std::string> key { address, contactName };
        if (seen.insert(key).second)
        {
            AndroidContactInfo info;
            info.address     = address;
            info.contactName = contactName;
            contacts.push_back(std::move(info));
        }
    }

    if (contacts.empty())
    {
        errorOutW = L"No contacts found in the Android SMS XML file.";
        return false;
    }

    outContacts = std::move(contacts);
    errorOutW.clear();
    return true;
}


// ============================================================================
// RichEdit helpers (color / formatting of the text report)
// ============================================================================

void AppendColoredLine(HWND hEdit, const std::string& line)
{
    // Append at end
    CHARRANGE cr;
    cr.cpMin = cr.cpMax = -1;
    SendMessage(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    CHARFORMAT2 cf{};
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR | CFM_BOLD;
    cf.dwEffects   = 0;
    cf.crTextColor = RGB(220, 220, 230); // def

    // Section headers / “big” labels
    if (line.rfind("=== Message Stats ===", 0) == 0)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(130, 210, 255);
    }
    else if (line.rfind("User:", 0) == 0)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(120, 230, 210);
    }
    else if (line.find("[General Message Data & Conversation Dynamics]") != std::string::npos)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(210, 190, 255);
    }
    else if (line.find("[Reactions]") != std::string::npos)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(255, 205, 160);
    }
    else if (line.find("[Word Usage & Longest Messages]") != std::string::npos)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(200, 235, 180);
    }
    else if (line.find("[VADER Sentiment Analysis]") != std::string::npos)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(255, 180, 190);
    }
    else if (line.find("[NRC Emotion Profile]") != std::string::npos)
    {
        cf.dwEffects   |= CFE_BOLD;
        cf.crTextColor  = RGB(170, 215, 255);
    }
    else
    {
        bool isIndented =
            (line.size() >= 4 &&
             line[0] == ' ' && line[1] == ' ' &&
             line[2] == ' ' && line[3] == ' ');

        if (line.find("% Positive messages:")    != std::string::npos ||
            line.find("% Negative messages:")    != std::string::npos ||
            line.find("% Neutral messages:")     != std::string::npos ||
            line.find("Average compound score:") != std::string::npos)
        {
            cf.crTextColor = RGB(245, 170, 180);
        }
        else if (line.find("anger:")        != std::string::npos ||
                 line.find("anticipation:") != std::string::npos ||
                 line.find("disgust:")      != std::string::npos ||
                 line.find("fear:")         != std::string::npos ||
                 line.find("joy:")          != std::string::npos ||
                 line.find("sadness:")      != std::string::npos ||
                 line.find("surprise:")     != std::string::npos ||
                 line.find("trust:")        != std::string::npos ||
                 line.find("negative:")     != std::string::npos ||
                 line.find("positive:")     != std::string::npos)
        {
            cf.crTextColor = RGB(160, 205, 245);
        }
        else if (line.find("Reactions sent:") != std::string::npos)
        {
            cf.crTextColor = RGB(245, 190, 150);
        }
        else if (line.find("Top 10 most used words:") != std::string::npos)
        {
            cf.crTextColor = RGB(230, 210, 170);
        }
        else if (isIndented)
        {
            cf.crTextColor = RGB(190, 170, 235);
        }
    }

    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    std::wstring wline = Utf8ToWide(line);
    wline += L"\r\n";
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)wline.c_str());
}

void SetEditTextFormatted(HWND hEdit, const std::string& text)
{
    SendMessage(hEdit, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(hEdit, L"");

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
        AppendColoredLine(hEdit, line);

    SendMessage(hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hEdit, nullptr, TRUE);
}

// ============================================================================
// File / folder dialogs
// ============================================================================

std::wstring PickJsonFile(HWND hWnd)
{
    wchar_t fileBuf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hWnd;
    ofn.lpstrFilter = L"JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle  = L"Select chat JSON file";

    if (GetOpenFileNameW(&ofn))
        return std::wstring(fileBuf);

    return L"";
}


std::wstring PickFile(HWND hWnd)
{
    return PickJsonFile(hWnd);
}

std::wstring PickXmlOrAnyFile(HWND hWnd)
{
    wchar_t fileBuf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hWnd;

    // Show XML first, but still allow everything
    ofn.lpstrFilter =
        L"XML files (*.xml)\0*.xml\0"
        L"All files (*.*)\0*.*\0";

    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;

    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    ofn.lpstrTitle  = L"Select Android SMS Backup XML";

    if (GetOpenFileNameW(&ofn))
        return std::wstring(fileBuf);

    return L"";
}


std::wstring PickWhatsAppTxtFile(HWND hWnd)
{
    wchar_t fileBuf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hWnd;
    ofn.lpstrFilter = L"WhatsApp exports (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle  = L"Select WhatsApp _chat.txt file";

    if (GetOpenFileNameW(&ofn))
        return std::wstring(fileBuf);

    return L"";
}
std::wstring PickFolder(HWND hWnd)
{
    // Modern folder picker because the other one was terrible
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool didInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    IFileOpenDialog* dlg = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg)
    {
        if (didInit) CoUninitialize();
        return L"";
    }

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    std::wstring result;

    hr = dlg->Show(hWnd);
    if (SUCCEEDED(hr))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item)
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
            {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dlg->Release();
    if (didInit) CoUninitialize();
    return result;
}


// ============================================================================
// Background analysis thread
// ============================================================================

void RunAnalysisThread(HWND hWnd, std::wstring path)
{
    // Disable controls while analysis is running
    EnableWindow(g_hBtnFile,    FALSE);
    EnableWindow(g_hBtnFolder,  FALSE);
    EnableWindow(g_hBtnRun,     FALSE);
    EnableWindow(g_hBtnDiscord, FALSE);
    EnableWindow(g_hBtnWhatsApp, FALSE); 
    EnableWindow(g_hBtnImessage, FALSE);
    EnableWindow(g_hBtnAndroid, FALSE);

    SetWindowTextW(g_hStatus,
                   L"Status: Analyzing (this may take a while).");

    std::string report;
    try
    {
        std::string utf8Path = WideToUtf8(path);
        report = runAnalysisToString(utf8Path);
    }
    catch (const std::exception& ex)
    {
        report = std::string("Error during analysis:\n") + ex.what();
    }

    // Hand ownership of the report to the UI thread
    std::string* heapReport = new std::string(std::move(report));
    PostMessageW(hWnd, WM_APP_ANALYSIS_COMPLETE,
                 (WPARAM)heapReport, 0);
}

// ============================================================================
// Layout
// ============================================================================

void DoLayout(HWND hWnd)
{
    RECT rc;
    GetClientRect(hWnd, &rc);

    int margin   = 8;
    int buttonH  = 28;
    int spacing  = 6;
    int statusH  = 20;
    int tabH     = 26;

    int x = margin;
    int y = margin;
    int bw = 160;

    MoveWindow(g_hBtnFile,   x, y, bw, buttonH, TRUE);
    x += bw + spacing;
    MoveWindow(g_hBtnFolder, x, y, bw, buttonH, TRUE);
    x += bw + spacing;
    MoveWindow(g_hBtnDiscord, x, y, bw, buttonH, TRUE);
    x += bw + spacing;
    MoveWindow(g_hBtnWhatsApp, x, y, bw, buttonH, TRUE);  
    x += bw + spacing;
    MoveWindow(g_hBtnAndroid, x, y, bw, buttonH, TRUE);
    x += bw + spacing;
    MoveWindow(g_hBtnImessage, x, y, bw, buttonH, TRUE);
    x += bw + spacing * 3;               // extra space before Run
    int runBw = bw + 20;     
    MoveWindow(g_hBtnRun,    x, y, runBw, buttonH, TRUE);

    int contentLeft   = margin;
    int contentRight  = rc.right - margin;
    int contentTop    = y + buttonH + margin;
    int contentBottom = rc.bottom - margin - statusH - spacing;

    MoveWindow(g_hTab,
               contentLeft,
               contentTop,
               contentRight - contentLeft,
               tabH,
               TRUE);

    int viewTop = contentTop + tabH + spacing;

    MoveWindow(g_hEdit,
               contentLeft,
               viewTop,
               contentRight - contentLeft,
               contentBottom - viewTop,
               TRUE);

    MoveWindow(g_hVisualCanvas,
               contentLeft,
               viewTop,
               contentRight - contentLeft,
               contentBottom - viewTop,
               TRUE);

    MoveWindow(g_hStatus,
               margin,
               contentBottom + spacing,
               rc.right - 2 * margin,
               statusH,
               TRUE);
}

// ============================================================================
// Tab switching (Summary vs Visuals)
// ============================================================================

void UpdateTabVisibility()
{
    int sel = (int)SendMessageW(g_hTab, TCM_GETCURSEL, 0, 0);
    if (sel == 1)
    {
        ShowWindow(g_hEdit, SW_HIDE);
        ShowWindow(g_hVisualCanvas, SW_SHOW);
        InvalidateRect(g_hVisualCanvas, nullptr, TRUE);
    }
    else
    {
        ShowWindow(g_hVisualCanvas, SW_HIDE);
        ShowWindow(g_hEdit, SW_SHOW);
    }
}

// ============================================================================
// Visual canvas window proc – draws all charts
// ============================================================================

LRESULT CALLBACK VisualsWndProc(HWND hWnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    // Persistent vertical scroll position
    static int s_scrollPos = 0;

    switch (msg)
    {
    // Scroll handling 
    case WM_VSCROLL:
    {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_ALL;
        GetScrollInfo(hWnd, SB_VERT, &si);

        int oldPos = si.nPos;

        switch (LOWORD(wParam))
        {
        case SB_LINEUP:      si.nPos -= 20;            break;
        case SB_LINEDOWN:    si.nPos += 20;            break;
        case SB_PAGEUP:      si.nPos -= (int)si.nPage; break;
        case SB_PAGEDOWN:    si.nPos += (int)si.nPage; break;
        case SB_THUMBTRACK:  si.nPos  = si.nTrackPos;  break;
        default: return 0;
        }

        if (si.nPos < si.nMin) si.nPos = si.nMin;
        {
            int maxPos = si.nMax - (int)si.nPage + 1;
            if (maxPos < 0) maxPos = 0;
            if (si.nPos > maxPos) si.nPos = maxPos;
        }

        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);

        if (si.nPos != oldPos)
        {
            s_scrollPos = si.nPos;
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        int lines = zDelta / WHEEL_DELTA; // positive = wheel up

        for (int i = 0; i < abs(lines); ++i)
        {
            WPARAM wp = (lines > 0) ? SB_LINEUP : SB_LINEDOWN;
            SendMessageW(hWnd, WM_VSCROLL, wp, 0);
        }
        return 0;
    }

    // Painting Section
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int width  = rc.right  - rc.left;
        int height = rc.bottom - rc.top;

        // Background
        HBRUSH bgBrush = CreateSolidBrush(RGB(26, 26, 34));
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(hdc, TRANSPARENT);
        COLORREF defaultText = RGB(220, 220, 230);
        SetTextColor(hdc, defaultText);

        int margin   = 10;
        int chartGap = 16;
        const int TITLE_H = 40;

        int chartWidth  = width - 2 * margin;
        int chartHeight = 160; // charts are tall; scrolling handles overflow

        int y = margin - s_scrollPos;

        HFONT bodyFont    = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT headingFont = g_hHeadingFont ? g_hHeadingFont : bodyFont;
        HFONT oldFont     = (HFONT)SelectObject(hdc, bodyFont);

        // Color palette for per-user lines
        COLORREF userColors[] = {
            RGB(255, 180, 180),
            RGB(180, 200, 255),
            RGB(255, 220, 150),
            RGB(200, 190, 255),
            RGB(180, 255, 200),
            RGB(255, 190, 230)
        };
        int numUserColors = (int)(sizeof(userColors) / sizeof(userColors[0]));

        // =========================================================
        // 1) Hour vs weekday heatmap
        // =========================================================
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(200, 160, 255));
            RECT heatTitleRc{ margin, y, margin + chartWidth, y + 20 };
            DrawTextW(hdc, L"Hour vs Weekday Heatmap (all messages)", -1,
                      &heatTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);
            RECT heatDescRc{ margin, y + 20, margin + chartWidth, y + 40 };
            DrawTextW(hdc,
                      L"Red squares = more messages. Columns are hours (0–23), rows are weekdays.",
                      -1, &heatDescRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            y += TITLE_H;

            RECT heatRect{ margin, y, margin + chartWidth, y + chartHeight };

            if (!g_heatmapReady)
            {
                DrawTextW(hdc, L"Run analysis to see heatmap.", -1,
                          &heatRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                int leftMargin   = 45;
                int bottomMargin = 22;
                int topMargin    = 8;
                int rightMargin  = 8;

                RECT plotRect{
                    heatRect.left   + leftMargin,
                    heatRect.top    + topMargin,
                    heatRect.right  - rightMargin,
                    heatRect.bottom - bottomMargin
                };

                int rows = 7;
                int cols = 24;

                int cellW = (plotRect.right  - plotRect.left) / cols;
                int cellH = (plotRect.bottom - plotRect.top)  / rows;
                if (cellW < 1) cellW = 1;
                if (cellH < 1) cellH = 1;

                int maxVal = 0;
                for (int d = 0; d < 7; ++d)
                    for (int h2 = 0; h2 < 24; ++h2)
                        if (g_heatmapCounts[d][h2] > maxVal)
                            maxVal = g_heatmapCounts[d][h2];

                if (maxVal == 0)
                {
                    DrawTextW(hdc, L"No messages with timestamps.", -1,
                              &heatRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    // Draw grid of colored cells
                    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(18, 18, 24));
                    HPEN oldPen  = (HPEN)SelectObject(hdc, gridPen);

                    for (int d = 0; d < rows; ++d)
                    {
                        for (int h2 = 0; h2 < cols; ++h2)
                        {
                            int val = g_heatmapCounts[d][h2];
                            double ratio = (double)val / (double)maxVal;

                            BYTE r = (BYTE)(50  + ratio * 205);
                            BYTE g = (BYTE)(30  + ratio * 40);
                            BYTE b = (BYTE)(200 - ratio * 180);

                            HBRUSH cellBrush = CreateSolidBrush(RGB(r, g, b));
                            HBRUSH oldBrush  = (HBRUSH)SelectObject(hdc, cellBrush);

                            int x0 = plotRect.left + h2 * cellW;
                            int y0 = plotRect.top  + d * cellH;
                            int x1 = x0 + cellW;
                            int y1 = y0 + cellH;

                            Rectangle(hdc, x0, y0, x1, y1);

                            SelectObject(hdc, oldBrush);
                            DeleteObject(cellBrush);
                        }
                    }

                    SelectObject(hdc, oldPen);
                    DeleteObject(gridPen);

                    // Weekday labels
                    const wchar_t* weekdays[7] = {
                        L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"
                    };

                    for (int d = 0; d < rows; ++d)
                    {
                        int cy = plotRect.top + d * cellH + cellH / 2;
                        RECT txtRc{ heatRect.left, cy - 8,
                                    plotRect.left - 4, cy + 8 };
                        DrawTextW(hdc, weekdays[d], -1, &txtRc,
                                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    }

                    // Hour labels
                    for (int h2 = 0; h2 < cols; ++h2)
                    {
                        int cx = plotRect.left + h2 * cellW + cellW / 2;
                        wchar_t buf[8];
                        wsprintfW(buf, L"%d", h2);
                        RECT txtRc{ cx - 10, plotRect.bottom + 2,
                                    cx + 10, plotRect.bottom + 18 };
                        DrawTextW(hdc, buf, -1, &txtRc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
            }

            y += chartHeight + chartGap;
        }

        // =========================================================
        // Line legend (key for per-user colors)
        // =========================================================
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(255, 220, 180));
            RECT keyTitle{ margin, y, margin + chartWidth, y + 18 };
            DrawTextW(hdc, L"Line Graph Key", -1,
                      &keyTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            int legendY   = y + 22;
            int boxSizeH  = 10;
            int boxSizeW  = 18;
            int xCursor   = margin;

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);

            for (size_t i = 0; i < g_chartUserNames.size(); ++i)
            {
                if (xCursor + 120 > margin + chartWidth)
                {
                    legendY += 18;
                    xCursor  = margin;
                }

                COLORREF col = userColors[i % numUserColors];

                RECT box{ xCursor, legendY,
                          xCursor + boxSizeW, legendY + boxSizeH };
                HBRUSH b = CreateSolidBrush(col);
                FillRect(hdc, &box, b);
                DeleteObject(b);
                xCursor += boxSizeW + 6;

                std::wstring wname = Utf8ToWide(g_chartUserNames[i]);
                RECT txt{ xCursor, legendY - 2,
                          xCursor + 120, legendY + 14 };
                DrawTextW(hdc, wname.c_str(), -1, &txt,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                xCursor += 120 + 14;
            }

            y = legendY + 20 + chartGap;
        }

        // =========================================================
        // 2) Messages per month (per user only)
        // =========================================================
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(200, 235, 180));
            RECT titleRc{ margin, y, margin + chartWidth, y + 20 };
            DrawTextW(hdc, L"Messages per Month", -1,
                      &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);
            RECT descRc{ margin, y + 20, margin + chartWidth, y + 40 };
            DrawTextW(hdc,
                      L"Lines show each user's total messages per month.",
                      -1, &descRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            y += TITLE_H;

            RECT msgRect{ margin, y, margin + chartWidth, y + chartHeight };

            if (g_monthlyCountPoints.empty())
            {
                DrawTextW(hdc, L"No monthly volume data.", -1,
                          &msgRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                RECT plotRect{
                    msgRect.left + 60,
                    msgRect.top  + 10,
                    msgRect.right - 10,
                    msgRect.bottom - 30
                };

                long long maxVal = 0;
                for (const auto& p : g_monthlyCountPoints)
                    if (p.totalMessages > maxVal)
                        maxVal = p.totalMessages;

                if (maxVal == 0)
                {
                    DrawTextW(hdc, L"No messages with timestamps.", -1,
                              &msgRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    long long minVal = 0;
                    double range = (double)(maxVal - minVal);
                    if (range <= 0.0) range = 1.0;

                    int n = (int)g_monthlyCountPoints.size();
                    int plotH = plotRect.bottom - plotRect.top;

                    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 140));
                    HPEN oldPen  = (HPEN)SelectObject(hdc, axisPen);

                    // Axes
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.right,   plotRect.bottom);
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.left,    plotRect.top);

                    RECT axisTitle{ msgRect.left, msgRect.top - 18,
                                    plotRect.left - 6, msgRect.top - 2 };
                    DrawTextW(hdc, L"Messages", -1, &axisTitle,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    wchar_t buf[64];
                    RECT labelRc;

                    // Max label
                    wsprintfW(buf, L"%s", formatWithCommasW(maxVal).c_str());
                    labelRc = { msgRect.left, plotRect.top - 8,
                                plotRect.left - 6, plotRect.top + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Mid label
                    long long midVal = maxVal / 2;
                    wsprintfW(buf, L"%s", formatWithCommasW(midVal).c_str());
                    int yMid = plotRect.bottom -
                               (int)(((double)(midVal - minVal) / range) *
                                     (plotRect.bottom - plotRect.top));
                    labelRc = { msgRect.left, yMid - 8,
                                plotRect.left - 6, yMid + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Zero label
                    wsprintfW(buf, L"0");
                    labelRc = { msgRect.left, plotRect.bottom - 8,
                                plotRect.left - 6, plotRect.bottom + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    std::vector<int> xPositions = ComputeXPositions(n, plotRect);

                    // Month labels
                    for (int i = 0; i < n; ++i)
                    {
                        int xPos = xPositions[i];
                        std::wstring ml = MakeMonthLabel(
                            g_monthlyCountPoints[i].month,
                            g_monthlyCountPoints[i].year);
                        RECT mxRc{ xPos - 24, plotRect.bottom + 2,
                                   xPos + 24, plotRect.bottom + 18 };
                        DrawTextW(hdc, ml.c_str(), -1, &mxRc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }

                    // Build per-user maps for quick lookup: (year,month) → messages
                    std::vector<std::map<std::pair<int,int>, long long>> userMaps;
                    userMaps.resize(g_chartUserNames.size());
                    for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                    {
                        if (ui >= g_userMonthlyCountSeries.size()) break;
                        const auto& series = g_userMonthlyCountSeries[ui];
                        auto& mp = userMaps[ui];
                        for (const auto& p : series)
                            mp[{p.year, p.month}] = p.totalMessages;
                    }

                    // Per-user lines
                    for (size_t ui = 0; ui < userMaps.size(); ++ui)
                    {
                        const auto& mp = userMaps[ui];
                        if (mp.empty()) continue;

                        COLORREF col = userColors[ui % numUserColors];
                        HPEN userPen = CreatePen(PS_SOLID, 1, col);
                        SelectObject(hdc, userPen);

                        bool havePrev = false;
                        int prevX = 0, prevY = 0;

                        for (int i = 0; i < n; ++i)
                        {
                            const auto& gp = g_monthlyCountPoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end())
                            {
                                havePrev = false;
                                continue;
                            }

                            double v = (double)it->second;
                            double yRatio = (v - (double)minVal) / range;
                            int xPos = xPositions[i];
                            int yPos = plotRect.bottom -
                                       (int)(yRatio * plotH);

                            if (havePrev)
                            {
                                MoveToEx(hdc, prevX, prevY, nullptr);
                                LineTo(hdc, xPos, yPos);
                            }
                            havePrev = true;
                            prevX = xPos;
                            prevY = yPos;

                            RECT ptRect{ xPos - 1, yPos - 1,
                                         xPos + 1, yPos + 1 };
                            HBRUSH ptBrush = CreateSolidBrush(col);
                            FillRect(hdc, &ptRect, ptBrush);
                            DeleteObject(ptBrush);
                        }

                        DeleteObject(userPen);
                    }

                    // Value label per month: show highest user's value and color it
                    for (int i = 0; i < n; ++i)
                    {
                        long long bestVal   = -1;
                        COLORREF  bestColor = defaultText;
                        int       bestY     = 0;
                        int       xPos      = xPositions[i];

                        for (size_t ui = 0; ui < userMaps.size(); ++ui)
                        {
                            const auto& mp = userMaps[ui];
                            if (mp.empty()) continue;

                            const auto& gp = g_monthlyCountPoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end()) continue;

                            long long val = it->second;
                            if (val > bestVal)
                            {
                                bestVal   = val;
                                bestColor = userColors[ui % numUserColors];

                                double v = (double)val;
                                double yRatio = (v - (double)minVal) / range;
                                bestY = plotRect.bottom -
                                        (int)(yRatio * plotH);
                            }
                        }

                        if (bestVal >= 0)
                        {
                            wchar_t valBuf[32];
                            wsprintfW(valBuf, L"%s", formatWithCommasW(bestVal).c_str());
                            RECT vRc{ xPos - 32, bestY - 22,
                                      xPos + 32, bestY - 6 };
                            SetTextColor(hdc, bestColor);
                            DrawTextW(hdc, valBuf, -1, &vRc,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            SetTextColor(hdc, defaultText);
                        }
                    }

                    SelectObject(hdc, oldPen);
                    DeleteObject(axisPen);
                }
            }

            y += chartHeight + chartGap;
        }

        // =========================================================
        // 3) Average response time per month (per user only)
        // =========================================================
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(255, 210, 180));
            RECT respTitleRc{ margin, y, margin + chartWidth, y + 20 };
            DrawTextW(hdc, L"Average Response Time per Month", -1,
                      &respTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);
            RECT respDescRc{ margin, y + 20, margin + chartWidth, y + 40 };
            DrawTextW(hdc,
                      L"Lines show each user's average reply time in minutes for months where they replied after someone else.",
                      -1, &respDescRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            y += TITLE_H;

            RECT respRect{ margin, y, margin + chartWidth, y + chartHeight };

            if (g_monthlyResponsePoints.empty())
            {
                DrawTextW(hdc, L"No monthly response-time data.", -1,
                          &respRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                RECT plotRect{
                    respRect.left + 60,
                    respRect.top  + 10,
                    respRect.right - 10,
                    respRect.bottom - 30
                };

                // Determine overall max across global and per-user response time
                double maxVal = 0.0;
                for (const auto& p : g_monthlyResponsePoints)
                    if (p.avgMinutes > maxVal) maxVal = p.avgMinutes;

                for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                {
                    if (ui >= g_userMonthlyResponseSeries.size()) break;
                    const auto& series = g_userMonthlyResponseSeries[ui];
                    for (const auto& up : series)
                        if (up.avgMinutes > maxVal) maxVal = up.avgMinutes;
                }

                if (maxVal <= 0.0)
                {
                    DrawTextW(hdc, L"No response-time data.", -1,
                              &respRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    double minVal = 0.0;
                    double range  = maxVal - minVal;
                    if (range <= 0.0) range = 1.0;

                    int n = (int)g_monthlyResponsePoints.size();
                    int plotH = plotRect.bottom - plotRect.top;

                    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 140));
                    HPEN oldPen  = (HPEN)SelectObject(hdc, axisPen);

                    // Axes
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.right,   plotRect.bottom);
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.left,    plotRect.top);

                    RECT axisTitle{ respRect.left, respRect.top - 18,
                                    plotRect.left - 6, respRect.top - 2 };
                    DrawTextW(hdc,
                              L"Avg response time (minutes)",
                              -1, &axisTitle,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    wchar_t buf[64];
                    RECT labelRc;

                    // Max label
                    swprintf_s(buf, L"%.2f", maxVal);
                    labelRc = { respRect.left, plotRect.top - 8,
                                plotRect.left - 6, plotRect.top + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Mid label
                    double midVal = maxVal / 2.0;
                    swprintf_s(buf, L"%.2f", midVal);
                    int yMid = plotRect.bottom -
                               (int)(((midVal - minVal) / range) *
                                     (plotRect.bottom - plotRect.top));
                    labelRc = { respRect.left, yMid - 8,
                                plotRect.left - 6, yMid + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Zero label
                    swprintf_s(buf, L"0.00");
                    labelRc = { respRect.left, plotRect.bottom - 8,
                                plotRect.left - 6, plotRect.bottom + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    std::vector<int> xPositions = ComputeXPositions(n, plotRect);

                    // Month labels
                    for (int i = 0; i < n; ++i)
                    {
                        int xPos = xPositions[i];
                        std::wstring ml = MakeMonthLabel(
                            g_monthlyResponsePoints[i].month,
                            g_monthlyResponsePoints[i].year);
                        RECT mxRc{ xPos - 24, plotRect.bottom + 2,
                                   xPos + 24, plotRect.bottom + 18 };
                        DrawTextW(hdc, ml.c_str(), -1, &mxRc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }

                    // Build per-user maps: (year,month) → avgMinutes
                    std::vector<std::map<std::pair<int,int>, double>> userMaps;
                    userMaps.resize(g_chartUserNames.size());
                    for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                    {
                        if (ui >= g_userMonthlyResponseSeries.size()) break;
                        const auto& series = g_userMonthlyResponseSeries[ui];
                        auto& mp = userMaps[ui];
                        for (const auto& p : series)
                            mp[{p.year, p.month}] = p.avgMinutes;
                    }

                    // Per-user lines
                    for (size_t ui = 0; ui < userMaps.size(); ++ui)
                    {
                        const auto& mp = userMaps[ui];
                        if (mp.empty()) continue;

                        COLORREF col = userColors[ui % numUserColors];
                        HPEN userPen = CreatePen(PS_SOLID, 1, col);
                        SelectObject(hdc, userPen);

                        bool havePrev = false;
                        int prevX = 0, prevY = 0;

                        for (int i = 0; i < n; ++i)
                        {
                            const auto& gp = g_monthlyResponsePoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end())
                            {
                                havePrev = false;
                                continue;
                            }

                            double v = it->second;
                            double yRatio = (v - minVal) / range;
                            int xPos = xPositions[i];
                            int yPos = plotRect.bottom -
                                       (int)(yRatio * plotH);

                            if (havePrev)
                            {
                                MoveToEx(hdc, prevX, prevY, nullptr);
                                LineTo(hdc, xPos, yPos);
                            }
                            havePrev = true;
                            prevX = xPos;
                            prevY = yPos;

                            RECT ptRect{ xPos - 1, yPos - 1,
                                         xPos + 1, yPos + 1 };
                            HBRUSH ptBrush = CreateSolidBrush(col);
                            FillRect(hdc, &ptRect, ptBrush);
                            DeleteObject(ptBrush);
                        }

                        DeleteObject(userPen);
                    }

                    // Per-month label: user with highest avgMinutes
                    for (int i = 0; i < n; ++i)
                    {
                        double  bestVal   = -1.0;
                        COLORREF bestColor = defaultText;
                        int     bestY     = 0;
                        int     xPos      = xPositions[i];

                        for (size_t ui = 0; ui < userMaps.size(); ++ui)
                        {
                            const auto& mp = userMaps[ui];
                            if (mp.empty()) continue;

                            const auto& gp = g_monthlyResponsePoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end()) continue;

                            double val = it->second;
                            if (val > bestVal)
                            {
                                bestVal   = val;
                                bestColor = userColors[ui % numUserColors];

                                double yRatio = (val - minVal) / range;
                                bestY = plotRect.bottom -
                                        (int)(yRatio * plotH);
                            }
                        }

                        if (bestVal >= 0.0)
                        {
                            wchar_t valBuf[32];
                            swprintf_s(valBuf, L"%.2f", bestVal);
                            RECT vRc{ xPos - 32, bestY - 22,
                                      xPos + 32, bestY - 6 };
                            SetTextColor(hdc, bestColor);
                            DrawTextW(hdc, valBuf, -1, &vRc,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            SetTextColor(hdc, defaultText);
                        }
                    }

                    SelectObject(hdc, oldPen);
                    DeleteObject(axisPen);
                }
            }

            y += chartHeight + chartGap;
        }

        // =========================================================
        // 4) Romantic messages per month (per user only)
        // =========================================================
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(255, 190, 210));
            RECT romTitleRc{ margin, y, margin + chartWidth, y + 20 };
            DrawTextW(hdc, L"Romantic Messages per Month", -1,
                      &romTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);
            RECT romDescRc{ margin, y + 20, margin + chartWidth, y + 40 };
            DrawTextW(hdc,
                      L"Lines show each user's romantic messages per month.",
                      -1, &romDescRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            y += TITLE_H;

            RECT romRect{ margin, y, margin + chartWidth, y + chartHeight };

            if (g_monthlyRomanticPoints.empty())
            {
                DrawTextW(hdc, L"No romantic messages flagged.", -1,
                          &romRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                RECT plotRect{
                    romRect.left + 60,
                    romRect.top  + 10,
                    romRect.right - 10,
                    romRect.bottom - 30
                };

                long long maxVal = 0;
                for (const auto& p : g_monthlyRomanticPoints)
                    if (p.romanticMessages > maxVal)
                        maxVal = p.romanticMessages;

                if (maxVal == 0)
                {
                    DrawTextW(hdc, L"No romantic messages flagged.", -1,
                              &romRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    long long minVal = 0;
                    double range = (double)(maxVal - minVal);
                    if (range <= 0.0) range = 1.0;

                    int n = (int)g_monthlyRomanticPoints.size();
                    int plotH = plotRect.bottom - plotRect.top;

                    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 140));
                    HPEN oldPen  = (HPEN)SelectObject(hdc, axisPen);

                    // Axes
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.right,   plotRect.bottom);
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.left,    plotRect.top);

                    RECT axisTitle{ romRect.left, romRect.top - 18,
                                    plotRect.left - 6, romRect.top - 2 };
                    DrawTextW(hdc, L"Romantic messages", -1, &axisTitle,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    wchar_t buf[64];
                    RECT labelRc;

                    // Max label
                    wsprintfW(buf, L"%s", formatWithCommasW(maxVal).c_str());
                    labelRc = { romRect.left, plotRect.top - 8,
                                plotRect.left - 6, plotRect.top + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Mid label
                    long long midVal = maxVal / 2;
                    wsprintfW(buf, L"%s", formatWithCommasW(midVal).c_str());
                    int yMid = plotRect.bottom -
                               (int)(((double)(midVal - minVal) / range) *
                                     (plotRect.bottom - plotRect.top));
                    labelRc = { romRect.left, yMid - 8,
                                plotRect.left - 6, yMid + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Zero label
                    wsprintfW(buf, L"0");
                    labelRc = { romRect.left, plotRect.bottom - 8,
                                plotRect.left - 6, plotRect.bottom + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    std::vector<int> xPositions = ComputeXPositions(n, plotRect);

                    // Month labels
                    for (int i = 0; i < n; ++i)
                    {
                        int xPos = xPositions[i];
                        std::wstring ml = MakeMonthLabel(
                            g_monthlyRomanticPoints[i].month,
                            g_monthlyRomanticPoints[i].year);
                        RECT mxRc{ xPos - 24, plotRect.bottom + 2,
                                   xPos + 24, plotRect.bottom + 18 };
                        DrawTextW(hdc, ml.c_str(), -1, &mxRc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }

                    // Build per-user maps: (year,month) → romanticMessages
                    std::vector<std::map<std::pair<int,int>, long long>> userMaps;
                    userMaps.resize(g_chartUserNames.size());
                    for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                    {
                        if (ui >= g_userMonthlyRomanticSeries.size()) break;
                        const auto& series = g_userMonthlyRomanticSeries[ui];
                        auto& mp = userMaps[ui];
                        for (const auto& p : series)
                            mp[{p.year, p.month}] = p.romanticMessages;
                    }

                    // Per-user lines
                    for (size_t ui = 0; ui < userMaps.size(); ++ui)
                    {
                        const auto& mp = userMaps[ui];
                        if (mp.empty()) continue;

                        COLORREF col = userColors[ui % numUserColors];
                        HPEN userPen = CreatePen(PS_SOLID, 1, col);
                        SelectObject(hdc, userPen);

                        bool havePrev = false;
                        int prevX = 0, prevY = 0;

                        for (int i = 0; i < n; ++i)
                        {
                            const auto& gp = g_monthlyRomanticPoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end())
                            {
                                havePrev = false;
                                continue;
                            }

                            double v = (double)it->second;
                            double yRatio = (v - (double)minVal) / range;
                            int xPos = xPositions[i];
                            int yPos = plotRect.bottom -
                                       (int)(yRatio * plotH);

                            if (havePrev)
                            {
                                MoveToEx(hdc, prevX, prevY, nullptr);
                                LineTo(hdc, xPos, yPos);
                            }
                            havePrev = true;
                            prevX = xPos;
                            prevY = yPos;

                            RECT ptRect{ xPos - 1, yPos - 1,
                                         xPos + 1, yPos + 1 };
                            HBRUSH ptBrush = CreateSolidBrush(col);
                            FillRect(hdc, &ptRect, ptBrush);
                            DeleteObject(ptBrush);
                        }

                        DeleteObject(userPen);
                    }

                    // Per-month labels: user with highest romantic count
                    for (int i = 0; i < n; ++i)
                    {
                        long long bestVal   = -1;
                        COLORREF  bestColor = defaultText;
                        int       bestY     = 0;
                        int       xPos      = xPositions[i];

                        for (size_t ui = 0; ui < userMaps.size(); ++ui)
                        {
                            const auto& mp = userMaps[ui];
                            if (mp.empty()) continue;

                            const auto& gp = g_monthlyRomanticPoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end()) continue;

                            long long val = it->second;
                            if (val > bestVal)
                            {
                                bestVal   = val;
                                bestColor = userColors[ui % numUserColors];

                                double v = (double)val;
                                double yRatio = (v - (double)minVal) / range;
                                bestY = plotRect.bottom -
                                        (int)(yRatio * plotH);
                            }
                        }

                        if (bestVal >= 0)
                        {
                            wchar_t valBuf[32];
                            wsprintfW(valBuf, L"%s", formatWithCommasW(bestVal).c_str());
                            RECT vRc{ xPos - 32, bestY - 22,
                                      xPos + 32, bestY - 6 };
                            SetTextColor(hdc, bestColor);
                            DrawTextW(hdc, valBuf, -1, &vRc,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            SetTextColor(hdc, defaultText);
                        }
                    }

                    SelectObject(hdc, oldPen);
                    DeleteObject(axisPen);
                }
            }

            y += chartHeight + chartGap;
        }

        // =========================================================
        // 5) Average message length per month (per user only)
        // =========================================================
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(200, 235, 200));
            RECT lenTitleRc{ margin, y, margin + chartWidth, y + 20 };
            DrawTextW(hdc, L"Average Message Length per Month", -1,
                      &lenTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);
            RECT lenDescRc{ margin, y + 20, margin + chartWidth, y + 40 };
            DrawTextW(hdc,
                      L"Lines show each user's monthly average words per message.",
                      -1, &lenDescRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            y += TITLE_H;

            RECT lenRect{ margin, y, margin + chartWidth, y + chartHeight };

            if (g_monthlyAvgLengthPoints.empty())
            {
                DrawTextW(hdc, L"No monthly message-length data.", -1,
                          &lenRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                RECT plotRect{
                    lenRect.left + 60,
                    lenRect.top  + 10,
                    lenRect.right - 10,
                    lenRect.bottom - 30
                };

                double maxVal = 0.0;
                for (const auto& p : g_monthlyAvgLengthPoints)
                    if (p.avgWords > maxVal) maxVal = p.avgWords;

                if (maxVal <= 0.0)
                {
                    DrawTextW(hdc, L"No monthly message-length data.", -1,
                              &lenRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                else
                {
                    double minVal = 0.0;
                    double range  = maxVal - minVal;
                    if (range <= 0.0) range = 1.0;

                    int n = (int)g_monthlyAvgLengthPoints.size();
                    int plotH = plotRect.bottom - plotRect.top;

                    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 140));
                    HPEN oldPen  = (HPEN)SelectObject(hdc, axisPen);

                    // Axes
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.right,   plotRect.bottom);
                    MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                    LineTo(hdc, plotRect.left,    plotRect.top);

                    RECT axisTitle{ lenRect.left, lenRect.top - 18,
                                    plotRect.left - 6, lenRect.top - 2 };
                    DrawTextW(hdc, L"Avg message length (words)", -1,
                              &axisTitle,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    wchar_t buf[64];
                    RECT labelRc;

                    // Max label
                    swprintf_s(buf, L"%.1f", maxVal);
                    labelRc = { lenRect.left, plotRect.top - 8,
                                plotRect.left - 6, plotRect.top + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Mid label
                    double midVal = maxVal / 2.0;
                    swprintf_s(buf, L"%.1f", midVal);
                    int yMid = plotRect.bottom -
                               (int)(((midVal - minVal) / range) *
                                     (plotRect.bottom - plotRect.top));
                    labelRc = { lenRect.left, yMid - 8,
                                plotRect.left - 6, yMid + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    // Zero label
                    swprintf_s(buf, L"0.0");
                    labelRc = { lenRect.left, plotRect.bottom - 8,
                                plotRect.left - 6, plotRect.bottom + 8 };
                    DrawTextW(hdc, buf, -1, &labelRc,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                    std::vector<int> xPositions = ComputeXPositions(n, plotRect);

                    // Month labels
                    for (int i = 0; i < n; ++i)
                    {
                        int xPos = xPositions[i];
                        std::wstring ml = MakeMonthLabel(
                            g_monthlyAvgLengthPoints[i].month,
                            g_monthlyAvgLengthPoints[i].year);
                        RECT mxRc{ xPos - 24, plotRect.bottom + 2,
                                   xPos + 24, plotRect.bottom + 18 };
                        DrawTextW(hdc, ml.c_str(), -1, &mxRc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }

                    // Build per-user maps: (year,month) → avgWords
                    std::vector<std::map<std::pair<int,int>, double>> userMaps;
                    userMaps.resize(g_chartUserNames.size());
                    for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                    {
                        if (ui >= g_userMonthlyAvgLengthSeries.size()) break;
                        const auto& series = g_userMonthlyAvgLengthSeries[ui];
                        auto& mp = userMaps[ui];
                        for (const auto& p : series)
                            mp[{p.year, p.month}] = p.avgWords;
                    }

                    // Per-user lines
                    for (size_t ui = 0; ui < userMaps.size(); ++ui)
                    {
                        const auto& mp = userMaps[ui];
                        if (mp.empty()) continue;

                        COLORREF col = userColors[ui % numUserColors];
                        HPEN userPen = CreatePen(PS_SOLID, 1, col);
                        SelectObject(hdc, userPen);

                        bool havePrev = false;
                        int prevX = 0, prevY = 0;

                        for (int i = 0; i < n; ++i)
                        {
                            const auto& gp = g_monthlyAvgLengthPoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end())
                            {
                                havePrev = false;
                                continue;
                            }

                            double v = it->second;
                            double yRatio = (v - minVal) / range;
                            int xPos = xPositions[i];
                            int yPos = plotRect.bottom -
                                       (int)(yRatio * plotH);

                            if (havePrev)
                            {
                                MoveToEx(hdc, prevX, prevY, nullptr);
                                LineTo(hdc, xPos, yPos);
                            }
                            havePrev = true;
                            prevX = xPos;
                            prevY = yPos;

                            RECT ptRect{ xPos - 1, yPos - 1,
                                         xPos + 1, yPos + 1 };
                            HBRUSH ptBrush = CreateSolidBrush(col);
                            FillRect(hdc, &ptRect, ptBrush);
                            DeleteObject(ptBrush);
                        }

                        DeleteObject(userPen);
                    }

                    // Per-month labels: user with highest average length
                    for (int i = 0; i < n; ++i)
                    {
                        double  bestVal   = -1.0;
                        COLORREF bestColor = defaultText;
                        int     bestY     = 0;
                        int     xPos      = xPositions[i];

                        for (size_t ui = 0; ui < userMaps.size(); ++ui)
                        {
                            const auto& mp = userMaps[ui];
                            if (mp.empty()) continue;

                            const auto& gp = g_monthlyAvgLengthPoints[i];
                            auto key = std::make_pair(gp.year, gp.month);
                            auto it  = mp.find(key);
                            if (it == mp.end()) continue;

                            double val = it->second;
                            if (val > bestVal)
                            {
                                bestVal   = val;
                                bestColor = userColors[ui % numUserColors];

                                double yRatio = (val - minVal) / range;
                                bestY = plotRect.bottom -
                                        (int)(yRatio * plotH);
                            }
                        }

                        if (bestVal >= 0.0)
                        {
                            wchar_t valBuf[32];
                            swprintf_s(valBuf, L"%.1f", bestVal);
                            RECT vRc{ xPos - 32, bestY - 22,
                                      xPos + 32, bestY - 6 };
                            SetTextColor(hdc, bestColor);
                            DrawTextW(hdc, valBuf, -1, &vRc,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            SetTextColor(hdc, defaultText);
                        }
                    }

                    SelectObject(hdc, oldPen);
                    DeleteObject(axisPen);
                }
            }

            y += chartHeight + chartGap;
        }

        // =========================================================
        // 6) Monthly emotional intensity (VADER compound, per user)
        //     Negative values are clamped to 0 for drawing so the graph
        //     always sits above the X-axis and is easier to read.
// =========================================================
// Above Might no longer be needed due to reformatting / bug fixing.
        {
            SelectObject(hdc, headingFont);
            SetTextColor(hdc, RGB(160, 215, 255));
            RECT emoTitleRc{ margin, y, margin + chartWidth, y + 20 };
            DrawTextW(hdc, L"Monthly Emotional Intensity (VADER compound)", -1,
                      &emoTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, bodyFont);
            SetTextColor(hdc, defaultText);
            RECT emoDescRc{ margin, y + 20, margin + chartWidth, y + 40 };
            DrawTextW(hdc,
                      L"Lines show each user's average VADER compound per month (negative values are drawn at 0).",
                      -1, &emoDescRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            y += TITLE_H;

            RECT emoRect{ margin, y, margin + chartWidth, y + chartHeight };

            // We still use g_monthlyEmotionPoints for the list of months (X axis),
            // but we only draw per-user series (no global/total line).
            if (g_monthlyEmotionPoints.empty() || g_chartUserNames.empty())
            {
                DrawTextW(hdc, L"No monthly sentiment data.", -1,
                          &emoRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                RECT plotRect{
                    emoRect.left + 60,
                    emoRect.top  + 10,
                    emoRect.right - 10,
                    emoRect.bottom - 30
                };

                // Find max across all users' monthly averages
                double maxVal = 0.0;
                for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                {
                    if (ui >= g_userMonthlyEmotionSeries.size()) break;
                    const auto& series = g_userMonthlyEmotionSeries[ui];
                    for (const auto& p : series)
                    {
                        if (p.avgCompound > maxVal)
                            maxVal = p.avgCompound;
                    }
                }

                // If everything is non-positive, give a small positive range
                if (maxVal < 0.1)
                    maxVal = 0.1;

                double minVal = 0.0; // negatives are clamped to 0
                double range  = maxVal - minVal;
                if (range <= 0.0) range = 0.1;

                int n     = (int)g_monthlyEmotionPoints.size();
                int plotH = plotRect.bottom - plotRect.top;

                HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 140));
                HPEN oldPen  = (HPEN)SelectObject(hdc, axisPen);

                // Axes
                MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                LineTo(hdc, plotRect.right,   plotRect.bottom);
                MoveToEx(hdc, plotRect.left,  plotRect.bottom, nullptr);
                LineTo(hdc, plotRect.left,    plotRect.top);

                RECT axisTitle{ emoRect.left, emoRect.top - 18,
                                plotRect.left - 6, emoRect.top - 2 };
                DrawTextW(hdc,
                          L"Avg emotion (VADER compound, negatives clamped to 0)",
                          -1, &axisTitle,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                wchar_t buf[64];
                RECT labelRc;

                // Max label
                swprintf_s(buf, L"%.2f", maxVal);
                labelRc = { emoRect.left, plotRect.top - 8,
                            plotRect.left - 6, plotRect.top + 8 };
                DrawTextW(hdc, buf, -1, &labelRc,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                // Mid label
                double midVal = maxVal / 2.0;
                swprintf_s(buf, L"%.2f", midVal);
                int yMid = plotRect.bottom -
                           (int)(((midVal - minVal) / range) *
                                 (plotRect.bottom - plotRect.top));
                labelRc = { emoRect.left, yMid - 8,
                            plotRect.left - 6, yMid + 8 };
                DrawTextW(hdc, buf, -1, &labelRc,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                // Zero label
                swprintf_s(buf, L"0.00");
                labelRc = { emoRect.left, plotRect.bottom - 8,
                            plotRect.left - 6, plotRect.bottom + 8 };
                DrawTextW(hdc, buf, -1, &labelRc,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

                std::vector<int> xPositions = ComputeXPositions(n, plotRect);

                // Month labels
                for (int i = 0; i < n; ++i)
                {
                    int xPos = xPositions[i];
                    std::wstring ml = MakeMonthLabel(
                        g_monthlyEmotionPoints[i].month,
                        g_monthlyEmotionPoints[i].year);
                    RECT mxRc{ xPos - 24, plotRect.bottom + 2,
                               xPos + 24, plotRect.bottom + 18 };
                    DrawTextW(hdc, ml.c_str(), -1, &mxRc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }

                // Build per-user maps: (year,month) → avgCompound
                std::vector<std::map<std::pair<int,int>, double>> userMaps;
                userMaps.resize(g_chartUserNames.size());
                for (size_t ui = 0; ui < g_chartUserNames.size(); ++ui)
                {
                    if (ui >= g_userMonthlyEmotionSeries.size()) break;
                    const auto& series = g_userMonthlyEmotionSeries[ui];
                    auto& mp = userMaps[ui];
                    for (const auto& p : series)
                        mp[{p.year, p.month}] = p.avgCompound;
                }

                // Per-user lines (clamping negatives to 0 on the graph)
                for (size_t ui = 0; ui < userMaps.size(); ++ui)
                {
                    const auto& mp = userMaps[ui];
                    if (mp.empty()) continue;

                    COLORREF col = userColors[ui % numUserColors];
                    HPEN userPen = CreatePen(PS_SOLID, 1, col);
                    SelectObject(hdc, userPen);

                    bool havePrev = false;
                    int prevX = 0, prevY = 0;

                    for (int i = 0; i < n; ++i)
                    {
                        const auto& gp = g_monthlyEmotionPoints[i];
                        auto key = std::make_pair(gp.year, gp.month);
                        auto it  = mp.find(key);
                        if (it == mp.end())
                        {
                            havePrev = false;
                            continue;
                        }

                        double raw      = it->second;
                        double vClamped = (raw < 0.0) ? 0.0 : raw;
                        double yRatio   = (vClamped - minVal) / range;
                        int xPos        = xPositions[i];
                        int yPos        = plotRect.bottom -
                                          (int)(yRatio * plotH);

                        if (havePrev)
                        {
                            MoveToEx(hdc, prevX, prevY, nullptr);
                            LineTo(hdc, xPos, yPos);
                        }
                        havePrev = true;
                        prevX    = xPos;
                        prevY    = yPos;

                        RECT ptRect{ xPos - 1, yPos - 1,
                                     xPos + 1, yPos + 1 };
                        HBRUSH ptBrush = CreateSolidBrush(col);
                        FillRect(hdc, &ptRect, ptBrush);
                        DeleteObject(ptBrush);
                    }

                    DeleteObject(userPen);
                }

                // Per-month labels: highest user value (raw, not clamped)
                for (int i = 0; i < n; ++i)
                {
                    double  bestVal   = -1.0;
                    COLORREF bestColor = defaultText;
                    int     bestY     = 0;
                    int     xPos      = xPositions[i];

                    const auto& base = g_monthlyEmotionPoints[i];
                    auto key = std::make_pair(base.year, base.month);

                    for (size_t ui = 0; ui < userMaps.size(); ++ui)
                    {
                        const auto& mp = userMaps[ui];
                        if (mp.empty()) continue;

                        auto it = mp.find(key);
                        if (it == mp.end()) continue;

                        double val = it->second;
                        if (val > bestVal)
                        {
                            bestVal   = val;
                            bestColor = userColors[ui % numUserColors];

                            double vClamped = (val < 0.0) ? 0.0 : val;
                            double yRatio   = (vClamped - minVal) / range;
                            bestY = plotRect.bottom -
                                    (int)(yRatio * plotH);
                        }
                    }

                    if (bestVal > -1.0)
                    {
                        wchar_t valBuf[32];
                        swprintf_s(valBuf, L"%.2f", bestVal);
                        RECT vRc{ xPos - 32, bestY - 22,
                                  xPos + 32, bestY - 6 };
                        SetTextColor(hdc, bestColor);
                        DrawTextW(hdc, valBuf, -1, &vRc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        SetTextColor(hdc, defaultText);
                    }
                }

                SelectObject(hdc, oldPen);
                DeleteObject(axisPen);
            }

            y += chartHeight + chartGap;
        }

        // ---------------------- Update scroll range ----------------------
        {
            // Estimated logical content height; add padding so last chart is usable
            // Might no longer be needed due to bugfixing / reformatting. 
            int contentHeight = y + s_scrollPos + margin + 80;
            if (contentHeight < height)
                contentHeight = height;

            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin   = 0;
            si.nMax   = (contentHeight > 0) ? (contentHeight - 1) : 0;
            si.nPage  = height;

            int maxPos = si.nMax - (int)si.nPage + 1;
            if (maxPos < 0) maxPos = 0;
            if (s_scrollPos > maxPos) s_scrollPos = maxPos;

            si.nPos = s_scrollPos;
            SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        }

        SelectObject(hdc, oldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// Main window procedure
// ============================================================================

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
                         WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // Shared dark background brush
        g_hBgBrush = CreateSolidBrush(RGB(20, 20, 28));

        // UI fonts
        g_hUIFont = CreateFontW(
            -16, 0, 0, 0,
            FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );
        g_hHeadingFont = CreateFontW(
            -20, 0, 0, 0,
            FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        // RichEdit control
        LoadLibraryW(L"Msftedit.dll");

        // Top buttons
        g_hBtnFile = CreateWindowW(
            L"BUTTON", L"Select File",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,0,0,0,
            hWnd, (HMENU)IDC_BTN_FILE, g_hInst, nullptr);

        g_hBtnFolder = CreateWindowW(
            L"BUTTON", L"Select Folder",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,0,0,0,
            hWnd, (HMENU)IDC_BTN_FOLDER, g_hInst, nullptr);

        g_hBtnDiscord = CreateWindowW(
            L"BUTTON", L"Discord File Convert",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,0,0,0,
            hWnd, (HMENU)IDC_BTN_DISCORD, g_hInst, nullptr);

        
        g_hBtnWhatsApp = CreateWindowW(                    
            L"BUTTON", L"WhatsApp File Convert",                   
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,0,0,0,
            hWnd, (HMENU)IDC_BTN_WHATSAPP, g_hInst, nullptr);

        g_hBtnImessage = CreateWindowW(
            L"BUTTON", L"iMessage File Convert",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,0,0,0,
            hWnd, (HMENU)IDC_BTN_IMESSAGE, g_hInst, nullptr);

        g_hBtnAndroid = CreateWindowW(
            L"BUTTON", L"Android SMS Convert",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            hWnd, (HMENU)IDC_BTN_ANDROID, g_hInst, nullptr);
        SendMessageW(g_hBtnAndroid, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);

        g_hBtnRun = CreateWindowW(
            L"BUTTON", L"Run Analysis",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,0,0,0,
            hWnd, (HMENU)IDC_BTN_RUN, g_hInst, nullptr);

        // Tabs
        g_hTab = CreateWindowExW(
            0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0,0,0,0,
            hWnd,
            (HMENU)IDC_TAB_MAIN,
            g_hInst,
            nullptr);

        SendMessageW(g_hTab, TCM_SETPADDING, 0, MAKELPARAM(18, 4));

        TCITEMW tie{};
        tie.mask = TCIF_TEXT;

        tie.pszText = const_cast<wchar_t*>(L"Summary");
        SendMessageW(g_hTab, TCM_INSERTITEMW, 0, (LPARAM)&tie);
        tie.pszText = const_cast<wchar_t*>(L"Visuals");
        SendMessageW(g_hTab, TCM_INSERTITEMW, 1, (LPARAM)&tie);

        // Summary output
        g_hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"RICHEDIT50W",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0,0,0,0,
            hWnd, (HMENU)IDC_EDIT_OUT, g_hInst, nullptr);

        // Visual charts canvas
        g_hVisualCanvas = CreateWindowExW(
            0,
            L"ChatVisualsCanvas",
            L"",
            WS_CHILD | WS_VSCROLL,
            0,0,0,0,
            hWnd, (HMENU)IDC_VISUAL_CANVAS, g_hInst, nullptr);

        // Status bar
        g_hStatus = CreateWindowW(
            L"STATIC", L"Status: Idle",
            WS_CHILD | WS_VISIBLE,
            0,0,0,0,
            hWnd, (HMENU)IDC_STATUS, g_hInst, nullptr);

        // Apply fonts
        SendMessageW(g_hBtnFile,    WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hBtnFolder,  WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hBtnDiscord,  WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hBtnWhatsApp, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hBtnImessage, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hBtnRun,     WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hStatus,     WM_SETFONT, (WPARAM)g_hUIFont, TRUE);
        SendMessageW(g_hTab,        WM_SETFONT, (WPARAM)g_hUIFont, TRUE);

        HFONT hEditFont = CreateFontW(
        18, 0, 0, 0,                          
        FW_NORMAL, FALSE, FALSE, FALSE,      
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,            
        L"Consolas"                           
        );
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)hEditFont, TRUE);

        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)hEditFont, TRUE);
        
        SendMessageW(g_hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(20, 20, 20));

        // Default to Summary tab
        SendMessageW(g_hTab, TCM_SETCURSEL, 0, 0);
        ShowWindow(g_hVisualCanvas, SW_HIDE);

        // Intro text
        {
            std::string intro =
                "Welcome to the Chat Analysis Tool.\n\n"
                "How to Use\n"
                "--------------------------------------------------------------\n"
                "1. Request and download your chat history from the messaging app of your choice (Instagram, WhatsApp, Discord, Android SMS, Iphone SMS)\n"
                "- Must be in JSON format.\n"
                "1b. For Discord, use the 'Discord Convert' button after exporting with Discrub.\n"
                "1c. For WhatsApp, use the 'WhatsApp Convert' button after exporting the chat as a .txt file.\n"
                "1c. For Instagram, follow Meta's process to request your data archive which includes chat data.\n"
                "1d. For Android SMS, Download your backup using the app 'SMS Backup & Restore' Here --> click the 'Android SMS' button and select the XML file it created.\n"
                "1e. For Imessage SMS, "
                "2. Click 'Select File' or 'Select Folder' to point this app at your exported chat files.\n"
                "3. Click 'Run Analysis' to compute message statistics and visualizations.\n\n"
                "- Use a folder if your export is split into multiple JSON files. \n"
                "- Larger exports can take a bit; Read the bar at the bottom"
                "- Check the Summary Tab for detailed numbers and the Visuals Tab for charts.\n\n\n"
                "Further Discord Instructions: \n"
                "------------------------------\n"
                "- If you're using discord, download Discrub from the Google Web Store and use the web browser to export your chat data in *JSON* format. Default Settings.\n"
                "- Then click the above 'Discord Use' button and select the folder containing your export from Discrub.\n"
                "- The tool will convert it into a format suitable for analysis and Click 'Run Analysis'\n\n\n"
                "Further Whatsapp Instructions: \n"
                "------------------------------\n"
                "- If you're using Whatsapp, Within each individual chat theres an option at the bottom to export the chat as a .txt file.\n"
                "--> I haven't tested with images or media, so choose the option without media for best results. \n"
                "- After exporting the chat, click the above 'WhatsApp Use' and select the .txt file you exported.\n"
                "- The tool will convert it into a format suitable for analysis and Click 'Run Analysis'\n\n\n\n\n\n\n\n\n"
                "Some Facts & Stuff:\n"
                "--------------------------------------------------------------\n"
                "- I've only tested this tool like. Five Times. It might not work on group chats or Other platforms and it surely has some bugs.\n"
                "- I bet the lexicon code is a little off. \n"
                "- I know coding this in python makes 10x more sense considering the visuals. But w/ c++ you can have so many more lexicon rule word checks without compromising compute time & exe is self contained\n"
                "- The reason for no Imessage export is that it requires backups or super inconvenient steps to get the data out. \n"
                "- Instagram took about 6 hrs~ to get the data back from Meta. Discord took about 20~ minutes using the tool and the official export takes daays(which it's not tested on). \n\n\n\n"
                "- It's kind of funny that you can download entire chat logs from people.. Analyze it, Feel either terrible or...awful and they dont even get a notification about it. Like....Shrug\n"
                "- I'm curious if anyone will actually use this tool and like.. feel any joy.\n"
                "- I feel like this tool can be incredibly misused since it doesn't add context or reasons behind the numbers. Some people are just naturally more verbose or perhaps you were doing xy or z that month and etc. Also could be bugged.\n"
                "\n\n"
                "- Feel free to send any Code Issues on Github.\n";
                // "- Feel free to send anything else to me at: GithubAccountNameFirstWord|is|GithubAccountNameSecondWord|GithubNumber at g|m|a|i|l|.com\n";
            SetEditTextFormatted(g_hEdit, intro);
        }

        DoLayout(hWnd);
        return 0;
    }

    case WM_SIZE:
        DoLayout(hWnd);
        return 0;

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_BTN_DISCORD:
        {
            // 1) Pick folder containing Discord export
            std::wstring discordFolder = PickFolderModern(hWnd, L"Select Discord export folder");
            if (discordFolder.empty())
                break;

            // 2) Output subfolder: "<chosen>\discord_converted"
            std::wstring outFolder = discordFolder + L"\\discord_converted";

            // 3) Convert paths to UTF-8 for the conversion routine
            std::string inUtf8  = WideToUtf8(discordFolder);
            std::string outUtf8 = WideToUtf8(outFolder);

            // 4) Chat title based on folder name
            std::wstring folderName = discordFolder;
            std::size_t  pos        = folderName.find_last_of(L"\\/");
            if (pos != std::wstring::npos && pos + 1 < folderName.size())
                folderName = folderName.substr(pos + 1);

            std::string titleUtf8 = WideToUtf8(folderName);

            // 5) Run conversion
            std::string error;
            bool ok = ConvertDiscordToInstagramFolder(
                inUtf8,
                outUtf8,
                titleUtf8,
                error
            );

            if (!ok)
            {
                std::wstring wErr      = Utf8ToWide(error);
                std::wstring dialogText = L"Discord conversion failed:\n" + wErr;
                MessageBoxW(hWnd, dialogText.c_str(),
                L"Conversion Error",
                MB_OK | MB_ICONERROR);

            }
            else
            {
                // Point analysis at the converted folder
                g_selectedPath = outFolder;

                std::wstring status =
                    L"Discord converted. Selected folder: " + outFolder;
                SetWindowTextW(g_hStatus, status.c_str());
            }

            break;
        }

            case IDC_BTN_WHATSAPP:
        {
            // 1) Pick WhatsApp _chat.txt file
            std::wstring chatTxt = PickWhatsAppTxtFile(hWnd);
            if (chatTxt.empty())
                break;

            // 2) Determine parent folder and output folder: "<parent>\whatsapp_converted"
            std::wstring folderPath = chatTxt;
            std::size_t pos = folderPath.find_last_of(L"\\/");
            if (pos != std::wstring::npos)
                folderPath = folderPath.substr(0, pos);
            else
                folderPath = L".";

            std::wstring outFolder = folderPath + L"\\whatsapp_converted";

            // 3) Derive chat title from file name (without extension)
            std::wstring fileName = chatTxt;
            std::size_t posName = fileName.find_last_of(L"\\/");
            if (posName != std::wstring::npos && posName + 1 < fileName.size())
                fileName = fileName.substr(posName + 1);

            std::size_t dotPos = fileName.find_last_of(L'.');
            if (dotPos != std::wstring::npos)
                fileName = fileName.substr(0, dotPos);

            std::string inUtf8   = WideToUtf8(chatTxt);
            std::string outUtf8  = WideToUtf8(outFolder);
            std::string titleUtf8 = WideToUtf8(fileName);

            // 4) Run conversion
            std::string error;
            bool ok = ConvertWhatsAppToInstagramFolder(
                inUtf8,
                outUtf8,
                titleUtf8,
                error
            );

            if (!ok)
            {
                std::wstring wErr       = Utf8ToWide(error);
                std::wstring dialogText = L"WhatsApp conversion failed:\n" + wErr;
                MessageBoxW(hWnd, dialogText.c_str(),
                L"Conversion Error",
                MB_OK | MB_ICONERROR);
            }
            else
            {
                // Point analysis at the converted folder
                g_selectedPath = outFolder;

                std::wstring status =
                    L"WhatsApp converted. Selected folder: " + outFolder;
                SetWindowTextW(g_hStatus, status.c_str());
            }

            break;
        }

        case IDC_BTN_IMESSAGE:
        {
            // 1) User picks backup root (iOS backup folder) or a folder that contains chat.db
            std::wstring backupFolder = PickFolderModern(hWnd, L"Select iMessage backup folder (or folder containing chat.db)");
            if (backupFolder.empty())
                break;

            std::string backupUtf8 = WideToUtf8(backupFolder);

            // 2) Discover available chats in this backup / DB
            std::vector<ImessageChatInfo> chats;
            std::string error;
            if (!GetImessageChats(backupUtf8, chats, error))
            {
                std::wstring wErr       = Utf8ToWide(error);
                std::wstring dialogText = L"Failed to read iMessage chats:\n" + wErr;
                MessageBoxW(hWnd, dialogText.c_str(),
                L"iMessage Error",
                MB_OK | MB_ICONERROR);
                break;
            }

            if (chats.empty())
            {
                MessageBoxW(hWnd,
                    L"No iMessage chats were found in this backup / database.",
                    L"iMessage",
                    MB_OK | MB_ICONINFORMATION);
                break;
            }

            // 3) Let the user choose a chat via a simple Yes/No sequence
            ImessageChatInfo chosen;
            bool haveChoice = false;

            for (size_t i = 0; i < chats.size(); ++i)
            {
                const auto& chat = chats[i];

                std::wstring chatPrompt = L"Chat: ";
                chatPrompt += Utf8ToWide(chat.displayName.empty()
                                        ? chat.guid
                                        : chat.displayName);
                chatPrompt += L"\n";

                if (chat.isGroup)
                    chatPrompt += L"(Group chat)\n";
                else
                    chatPrompt += L"(1:1 chat)\n";

                if (!chat.participants.empty())
                {
                    chatPrompt += L"\nParticipants:\n";
                    for (const auto& p : chat.participants)
                    {
                        chatPrompt += L"  - ";
                        chatPrompt += Utf8ToWide(p);
                        chatPrompt += L"\n";
                    }
                }

                chatPrompt += L"\nUse this chat for analysis?";

                int res = MessageBoxW(
                    hWnd,
                    chatPrompt.c_str(),
                    L"Select iMessage Chat",
                    MB_YESNOCANCEL | MB_ICONQUESTION
                );

                if (res == IDYES)
                {
                    chosen     = chat;
                    haveChoice = true;
                    break;
                }
                else if (res == IDCANCEL)
                {
                    haveChoice = false;
                    break;
                }
            }
            if (!haveChoice)
            {
                // User cancelled or said "No" to all chats
                break;
            }

            // Output folder: "<backupFolder>\\imessage_converted"
            std::wstring outFolderW = backupFolder + L"\\imessage_converted";
            std::string  outFolderUtf8 = WideToUtf8(outFolderW);

            //Export the chosen chat in Instagram-style JSON format
            bool ok = ConvertImessageChatToInstagramFolder(
                backupUtf8,
                chosen.guid,
                outFolderUtf8,
                error
            );

            if (!ok)
            {
            std::wstring wErr       = Utf8ToWide(error);
            std::wstring dialogText = L"iMessage conversion failed:\n" + wErr;
            MessageBoxW(hWnd, dialogText.c_str(),
            L"Conversion Error",
            MB_OK | MB_ICONERROR);

            }
            else
            {
                // Point analysis at the converted folder
                g_selectedPath = outFolderW;

                std::wstring status =
                    L"iMessage converted. Selected folder: " + outFolderW;
                SetWindowTextW(g_hStatus, status.c_str());
            }

            break;
        }

        case IDC_BTN_ANDROID:
            {
                // 1) Let user choose the file
                std::wstring xmlPathW = PickXmlOrAnyFile(hWnd);
                if (xmlPathW.empty())
                    break;

                // 2) Discover contacts in the XML
                std::vector<AndroidContactInfo> contacts;
                std::wstring contactsErr;
                if (!GetAndroidContactsFromXml(xmlPathW, contacts, contactsErr))
                {
                    std::wstring dialogText = L"Failed to read Android SMS contacts:\n" + contactsErr;
                    MessageBoxW(hWnd, dialogText.c_str(), L"Android SMS Error", MB_OK | MB_ICONERROR);

                    break;
                }

                if (contacts.empty())
                {
                    MessageBoxW(hWnd,
                        L"No contacts found in the Android SMS XML file.",
                        L"Android SMS",
                        MB_OK | MB_ICONINFORMATION);
                    break;
                }

                // 3) Build a popup menu listing all contacts
                HMENU hMenu = CreatePopupMenu();
                if (!hMenu)
                {
                    MessageBoxW(hWnd,
                        L"Failed to create contact menu.",
                        L"Android SMS",
                        MB_OK | MB_ICONERROR);
                    break;
                }

                for (size_t i = 0; i < contacts.size(); ++i)
                {
                    const auto& c = contacts[i];

                    std::wstring item;

                    if (!c.contactName.empty())
                    {
                        item += Utf8ToWide(c.contactName);
                        if (!c.address.empty())
                        {
                            item += L"  (";
                            item += Utf8ToWide(c.address);
                            item += L")";
                        }
                    }
                    else if (!c.address.empty())
                    {
                        item += Utf8ToWide(c.address);
                    }
                    else
                    {
                        item += L"(Unknown)";
                    }

                    UINT cmdId = IDM_CONTACT_BASE + static_cast<UINT>(i);
                    AppendMenuW(hMenu, MF_STRING, cmdId, item.c_str());
                }

                // Position: near the Android button (or at cursor)
                POINT pt{};
                RECT rcBtn{};
                if (g_hBtnAndroid && GetWindowRect(g_hBtnAndroid, &rcBtn))
                {
                    pt.x = rcBtn.left;
                    pt.y = rcBtn.bottom;
                }
                else
                {
                    GetCursorPos(&pt);
                }

                // 4) Show the popup and get the user's choice
                UINT chosenCmd = TrackPopupMenu(
                    hMenu,
                    TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                    pt.x, pt.y,
                    0,
                    hWnd,
                    nullptr
                );

                DestroyMenu(hMenu);

                if (chosenCmd == 0)
                {
                    // User clicked away / canceled the menu
                    break;
                }

                size_t chosenIndex = static_cast<size_t>(chosenCmd - IDM_CONTACT_BASE);
                if (chosenIndex >= contacts.size())
                    break;

                AndroidContactInfo chosen = contacts[chosenIndex];

                // 5) Decide what to pass as the filter
                std::string targetFilter;
                if (!chosen.contactName.empty())
                    targetFilter = chosen.contactName;
                else
                    targetFilter = chosen.address;

                if (targetFilter.empty())
                {
                    MessageBoxW(hWnd,
                        L"Selected contact has no usable name or address.",
                        L"Android SMS",
                        MB_OK | MB_ICONERROR);
                    break;
                }

                // 6) Choose output folder: same directory as the XML + "\android_sms_converted"
                fs::path xmlPath = fs::u8path(WideToUtf8(xmlPathW));
                fs::path outDir  = xmlPath.parent_path() / "android_sms_converted";

                std::wstring outFolderW = Utf8ToWide(outDir.u8string());
                std::string  outFolder  = outDir.u8string();
                std::string  xmlPathUtf8 = WideToUtf8(xmlPathW);

                std::string error;
                bool ok = ConvertAndroidSmsXmlToInstagramFolder(
                    xmlPathUtf8,
                    targetFilter,
                    outFolder,
                    error
                );

                if (!ok)
                {
                    std::wstring wErr       = Utf8ToWide(error);
                    std::wstring dialogText = L"Android SMS conversion failed:\n" + wErr;
                    MessageBoxW(hWnd, dialogText.c_str(),
                    L"Conversion Error",
                    MB_OK | MB_ICONERROR);
                }
                else
                {
                    g_selectedPath = outFolderW;
                    std::wstring status =
                        L"Android SMS converted. Selected folder: " + outFolderW;
                    SetWindowTextW(g_hStatus, status.c_str());
                }

                break;
            }



        case IDC_BTN_FILE:
        {
            std::wstring path = PickJsonFile(hWnd);
            if (!path.empty())
            {
                g_selectedPath = path;
                std::wstring status = L"Selected file: " + path;
                SetWindowTextW(g_hStatus, status.c_str());
            }
            break;
        }

        case IDC_BTN_FOLDER:
        {
            std::wstring path = PickFolderModern(hWnd, L"Select folder containing chat JSON files");
            if (!path.empty())
            {
                g_selectedPath = path;
                std::wstring status = L"Selected folder: " + path;
                SetWindowTextW(g_hStatus, status.c_str());
            }
            break;
        }

        case IDC_BTN_RUN:
        {
            if (g_selectedPath.empty())
            {
                MessageBoxW(hWnd, L"Please select a file or folder first.",
                            L"No input", MB_OK | MB_ICONWARNING);
            }
            else
            {
                std::thread t(RunAnalysisThread, hWnd, g_selectedPath);
                t.detach();
            }
            break;
        }
        }
        return 0;
    }

    case WM_NOTIFY:
    {
        LPNMHDR pHdr = (LPNMHDR)lParam;
        if (pHdr->idFrom == IDC_TAB_MAIN && pHdr->code == TCN_SELCHANGE)
            UpdateTabVisibility();
        return 0;
    }

    case WM_APP_ANALYSIS_COMPLETE:
    {
        // Ownership of heapReport is transferred here
        std::string* pReport = reinterpret_cast<std::string*>(wParam);
        if (pReport)
        {
            SetEditTextFormatted(g_hEdit, *pReport);
            delete pReport;
        }

        EnableWindow(g_hBtnFile,    TRUE);
        EnableWindow(g_hBtnFolder,  TRUE);
        EnableWindow(g_hBtnDiscord, TRUE);
        EnableWindow(g_hBtnWhatsApp, TRUE);
        EnableWindow(g_hBtnImessage, TRUE);
        EnableWindow(g_hBtnAndroid, TRUE);
        EnableWindow(g_hBtnRun,     TRUE);
        SetWindowTextW(g_hStatus, L"Status: Done");

        UpdateTabVisibility();
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(210, 210, 220));
        SetBkColor(hdc, RGB(20, 20, 28));
        return (LRESULT)g_hBgBrush;
    }

    case WM_CTLCOLORBTN:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(230, 230, 235));
        SetBkColor(hdc, RGB(20, 20, 28));
        return (LRESULT)g_hBgBrush;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc2;
        GetClientRect(hWnd, &rc2);
        FillRect(hdc, &rc2, g_hBgBrush);
        return 1;
    }

    case WM_DESTROY:
        if (g_hBgBrush)      DeleteObject(g_hBgBrush);
        if (g_hUIFont)       DeleteObject(g_hUIFont);
        if (g_hHeadingFont)  DeleteObject(g_hHeadingFont);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// WinMain
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    // Main window class
    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"ChatAnalysisToolWindowClass";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // Visual canvas class
    WNDCLASSW wcCanvas{};
    wcCanvas.lpfnWndProc   = VisualsWndProc;
    wcCanvas.hInstance     = hInstance;
    wcCanvas.lpszClassName = L"ChatVisualsCanvas";
    wcCanvas.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcCanvas.hbrBackground = nullptr;
    RegisterClassW(&wcCanvas);

    HWND hWnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Chat Analysis Tool",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        900, 700,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 0;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return (int)msg.wParam;
}
