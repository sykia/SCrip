#include "windoweffects.h"
#include <QWindow>
#include <windows.h>
#include <dwmapi.h>

void WindowEffects::applyNative() {
    const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
    const bool top = SetWindowPos(hwnd, m_onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
    const int corners = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    // Windows 11 22H2+: documented Desktop Acrylic backdrop.
    const int backdrop = m_backdrop ? 3 /* DWMSBT_TRANSIENTWINDOW */ : 1 /* NONE */;
    bool blur = SUCCEEDED(DwmSetWindowAttribute(hwnd, 38 /* SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop)));
    const BOOL dark = m_backdrop == 2;
    DwmSetWindowAttribute(hwnd, 20 /* USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    MARGINS margins = m_backdrop ? MARGINS{-1, -1, -1, -1} : MARGINS{};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // Windows 10 fallback. Resolve at runtime: older Windows can omit this API.
    if (!blur) {
        struct AccentPolicy { int state; int flags; DWORD color; int animation; };
        struct CompositionData { int attribute; void *data; SIZE_T size; };
        using SetComposition = BOOL(WINAPI *)(HWND, CompositionData *);
        const auto setComposition = reinterpret_cast<SetComposition>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
        if (setComposition) {
            AccentPolicy policy{m_backdrop ? 3 : 0, 0, 0, 0};
            CompositionData data{19, &policy, sizeof(policy)};
            blur = setComposition(hwnd, &data) != FALSE;
        }
    }
    capabilities(blur, top, !blur && m_backdrop ? tr("Системное размытие недоступно в этой версии Windows") : QString{});
}
