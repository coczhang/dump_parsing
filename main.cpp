#include "main_window.h"

#include <QApplication>
#include <QIcon>

#ifdef _WIN32
#include "resource.h"

#include <shobjidl.h>
#include <windows.h>

namespace {

void setWindowsTaskbarIcon(QWidget &window)
{
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());
    if (!hwnd) {
        return;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    static HICON bigIcon = static_cast<HICON>(LoadImageW(instance,
                                                         MAKEINTRESOURCEW(IDI_APP_ICON),
                                                         IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXICON),
                                                         GetSystemMetrics(SM_CYICON),
                                                         LR_DEFAULTCOLOR));
    static HICON smallIcon = static_cast<HICON>(LoadImageW(instance,
                                                           MAKEINTRESOURCEW(IDI_APP_ICON),
                                                           IMAGE_ICON,
                                                           GetSystemMetrics(SM_CXSMICON),
                                                           GetSystemMetrics(SM_CYSMICON),
                                                           LR_DEFAULTCOLOR));

    if (bigIcon) {
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(bigIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }
    if (smallIcon) {
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(smallIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL2, reinterpret_cast<LPARAM>(smallIcon));
    }
}

} // namespace
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetCurrentProcessExplicitAppUserModelID(L"CocZhang.DumpParsing");
#endif

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(QStringLiteral(":/resources/app_icon.ico")));
    MainWindow w;
    w.show();
#ifdef _WIN32
    setWindowsTaskbarIcon(w);
#endif
    return QCoreApplication::exec();
}
