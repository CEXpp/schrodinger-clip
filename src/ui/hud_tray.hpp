#pragma once
#include "../common.hpp"
#include "../observer.hpp"
#include "../classifier.hpp"

namespace sclip {

constexpr UINT WM_SCLIP_TRAY         = WM_APP + 10;
constexpr UINT WM_SCLIP_ARM_SYNC     = WM_APP + 11;
constexpr UINT WM_SCLIP_PREVIEW_SYNC = WM_APP + 12;
constexpr UINT WM_SCLIP_DISARM_SYNC  = WM_APP + 13;
constexpr UINT WM_SCLIP_SHUTDOWN     = WM_APP + 14;

constexpr int HOTKEY_TOGGLE_BYPASS   = 0x5101; // Alt + Shift + V
constexpr int HOTKEY_OPEN_CONSOLE    = 0x5102; // Alt + Shift + O

constexpr UINT IDM_TRAY_OPEN_UI      = 4001;
constexpr UINT IDM_TRAY_TOGGLE_RAW   = 4002;
constexpr UINT IDM_TRAY_TOGGLE_HUD   = 4003;
constexpr UINT IDM_TRAY_DISARM       = 4004;
constexpr UINT IDM_TRAY_EXIT         = 4005;

// Translucent Cyber Desktop Micro-HUD Pill (never steals focus: WS_EX_NOACTIVATE)
class DesktopMicroHud {
public:
    bool Create(HINSTANCE hInst, bool initiallyVisible) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HudWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SchrodingerClipMicroHudWnd";
        RegisterClassExW(&wc);

        RECT workArea = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        int w = 460;
        int h = 68;
        int x = workArea.right - w - 20;
        int y = workArea.bottom - h - 18;

        DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
        hwnd_ = CreateWindowExW(exStyle, wc.lpszClassName, L"SchrodingerClip HUD",
                                WS_POPUP, x, y, w, h, nullptr, nullptr, hInst, this);
        if (!hwnd_) return false;

        SetLayeredWindowAttributes(hwnd_, 0, 232, LWA_ALPHA);
        HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, 18, 18);
        SetWindowRgn(hwnd_, rgn, TRUE);

        visible_ = initiallyVisible;
        if (visible_) {
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        }
        return true;
    }

    void Destroy() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void ToggleVisible() {
        if (!hwnd_) return;
        visible_ = !visible_;
        ShowWindow(hwnd_, visible_ ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    bool IsVisible() const { return visible_; }

    void UpdateLines(const std::wstring& topLine, const std::wstring& bottomLine, bool isBypass) {
        if (topLine == topLine_ && bottomLine == bottomLine_ && isBypass == isBypass_) return;
        topLine_ = topLine;
        bottomLine_ = bottomLine;
        isBypass_ = isBypass;
        if (hwnd_ && visible_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

private:
    HWND hwnd_ = nullptr;
    bool visible_ = false;
    bool isBypass_ = false;
    std::wstring topLine_ = L"SCHRÖDINGER·CLIP // STANDBY (0 Bytes)";
    std::wstring bottomLine_ = L"Press Ctrl+C on URL / JSON / SVG / cURL / #HEX";

    static LRESULT CALLBACK HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        DesktopMicroHud* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<DesktopMicroHud*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<DesktopMicroHud*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_NCHITTEST:
                return HTCAPTION; // Allow effortless dragging on desktop without stealing focus
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (self) self->PaintHud(hwnd, hdc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void PaintHud(HWND hwnd, HDC hdc) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 11, 17, 32));
        g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

        Gdiplus::Color accent = isBypass_ ? Gdiplus::Color(255, 245, 158, 11)
                                          : Gdiplus::Color(255, 56, 189, 248);
        Gdiplus::SolidBrush barBr(accent);
        g.FillRectangle(&barBr, 0, 0, 5, rc.bottom);

        Gdiplus::Pen borderPen(accent, 1.5f);
        g.DrawRectangle(&borderPen, 1, 1, rc.right - 3, rc.bottom - 3);

        Gdiplus::Font fTop(L"Consolas", 11.5f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font fBot(L"Segoe UI", 12.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush topBr(accent);
        Gdiplus::SolidBrush botBr(Gdiplus::Color(255, 226, 232, 240));

        Gdiplus::RectF rTop(14.0f, 10.0f, static_cast<float>(rc.right - 22), 22.0f);
        Gdiplus::RectF rBot(14.0f, 35.0f, static_cast<float>(rc.right - 22), 24.0f);
        Gdiplus::StringFormat sf;
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        g.DrawString(topLine_.c_str(), -1, &fTop, rTop, &sf, &topBr);
        g.DrawString(bottomLine_.c_str(), -1, &fBot, rBot, &sf, &botBr);
    }
};

class SystemTrayController {
public:
    bool Add(HWND ownerHwnd, const std::wstring& tooltip) {
        owner_ = ownerHwnd;
        std::memset(&nid_, 0, sizeof(nid_));
        nid_.cbSize = sizeof(NOTIFYICONDATAW);
        nid_.hWnd = ownerHwnd;
        nid_.uID = 1;
        nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid_.uCallbackMessage = WM_SCLIP_TRAY;
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(nid_.szTip, tooltip.c_str(), _TRUNCATE);
        added_ = (Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE);
        return added_;
    }

    void Remove() {
        if (added_) {
            Shell_NotifyIconW(NIM_DELETE, &nid_);
            added_ = false;
        }
    }

    void ShowContextMenu(HWND hwnd, bool isRawBypass, bool isHudVisible) {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN_UI, L"Open Quantum Observatory (Alt+Shift+O)");
        AppendMenuW(hMenu, MF_STRING | (isRawBypass ? MF_CHECKED : 0),
                    IDM_TRAY_TOGGLE_RAW, L"Raw Bypass Passthrough Mode (Alt+Shift+V)");
        AppendMenuW(hMenu, MF_STRING | (isHudVisible ? MF_CHECKED : 0),
                    IDM_TRAY_TOGGLE_HUD, L"Show Desktop Target Radar HUD");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_DISARM, L"Disarm Active Superposition");
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit SchrodingerClip");

        POINT pt = {};
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);
    }

private:
    HWND owner_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    bool added_ = false;
};

} // namespace sclip
