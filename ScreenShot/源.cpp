#include <windows.h>
#include <vector>
#include <algorithm>
#include <string>

// 绘图工具枚举
enum DrawTool {
    TOOL_SELECT = 0,
    TOOL_PEN,
    TOOL_RECTANGLE,
    TOOL_ARROW,
    TOOL_TEXT,
    TOOL_BLUR
};

// 绘图命令结构
struct DrawCommand {
    DrawTool tool;
    std::vector<POINT> points;
    COLORREF color;
    int thickness;
    std::wstring text;
    bool filled;

    DrawCommand() : tool(TOOL_PEN), color(RGB(255, 0, 0)), thickness(2), filled(false) {}
};

class ExtendedScreenshot {
private:
    static ExtendedScreenshot* s_instance;

    // 选择阶段变量
    HWND m_overlayWnd = nullptr;
    HBITMAP m_screenBitmap = nullptr;
    bool m_selecting = false;
    POINT m_startPt = { 0 };
    POINT m_currentPt = { 0 };

    // 编辑阶段变量
    HWND m_editWnd = nullptr;
    HWND m_toolBar = nullptr;
    HWND m_colorBar = nullptr;
    HDC m_editDC = nullptr;
    HBITMAP m_editBitmap = nullptr;
    RECT m_selectionRect = { 0 };

    // 绘图状态
    std::vector<DrawCommand> m_commands;
    DrawTool m_currentTool = TOOL_PEN;
    COLORREF m_currentColor = RGB(255, 0, 0);
    int m_currentThickness = 2;
    bool m_drawing = false;
    POINT m_lastPoint = { 0 };

    // UI 元素 ID
    enum {
        ID_SAVE = 1001,
        ID_COPY,
        ID_CANCEL,
        ID_TOOL_SELECT,
        ID_TOOL_PEN,
        ID_TOOL_RECT,
        ID_TOOL_ARROW,
        ID_TOOL_TEXT,
        ID_TOOL_BLUR,
        ID_COLOR_RED,
        ID_COLOR_GREEN,
        ID_COLOR_BLUE,
        ID_COLOR_YELLOW,
        ID_COLOR_BLACK,
        ID_THICKNESS_1,
        ID_THICKNESS_2,
        ID_THICKNESS_4
    };

public:
    struct SelectionResult {
        RECT rect;
        bool success;
        std::wstring savedPath;
    };

    static SelectionResult CaptureAndEdit() {
        ExtendedScreenshot instance;
        return instance.DoFullProcess();
    }

private:
    SelectionResult DoFullProcess() {
        SelectionResult result = {};

        // 阶段1: 区域选择
        if (!DoSelection()) {
            return result;
        }

        // 阶段2: 编辑
        if (DoEditing()) {
            result.success = true;
            result.rect = m_selectionRect;
            result.savedPath = L"screenshot.bmp";
        }

        return result;
    }

    bool DoSelection() {
        CaptureScreen();
        CreateOverlay();

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_USER + 1) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        bool hasSelection = (abs(m_currentPt.x - m_startPt.x) > 10 &&
            abs(m_currentPt.y - m_startPt.y) > 10);

        if (hasSelection) {
            m_selectionRect = {
                (std::min)(m_startPt.x, m_currentPt.x),
                (std::min)(m_startPt.y, m_currentPt.y),
                (std::max)(m_startPt.x, m_currentPt.x),
                (std::max)(m_startPt.y, m_currentPt.y)
            };
        }

        CleanupOverlay();
        return hasSelection;
    }

    bool DoEditing() {
        CreateEditWindow();

        MSG msg;
        bool saveRequested = false;

        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_USER + 2) {
                saveRequested = (msg.wParam == 1);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (saveRequested) {
            SaveScreenshot();
        }

        CleanupEditor();
        return saveRequested;
    }

    void CaptureScreen() {
        int w = GetSystemMetrics(SM_CXSCREEN);
        int h = GetSystemMetrics(SM_CYSCREEN);

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        m_screenBitmap = CreateCompatibleBitmap(screenDC, w, h);

        SelectObject(memDC, m_screenBitmap);
        BitBlt(memDC, 0, 0, w, h, screenDC, 0, 0, SRCCOPY);

        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
    }

    void CreateOverlay() {
        WNDCLASS wc = {};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"ScreenSelector";
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        RegisterClass(&wc);

        s_instance = this;

        m_overlayWnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"ScreenSelector", L"选择截图区域 (ESC取消)",
            WS_POPUP,
            0, 0,
            GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN),
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );

        ShowWindow(m_overlayWnd, SW_SHOW);
        SetForegroundWindow(m_overlayWnd);
    }

    void CreateEditWindow() {
        int w = m_selectionRect.right - m_selectionRect.left;
        int h = m_selectionRect.bottom - m_selectionRect.top;

        // 注册编辑窗口类
        WNDCLASS wc = {};
        wc.lpfnWndProc = EditWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"ScreenEditor";
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        RegisterClass(&wc);

        // 创建主编辑窗口
        m_editWnd = CreateWindow(
            L"ScreenEditor", L"截图编辑器 - by hesphoros",
            WS_OVERLAPPEDWINDOW,
            100, 100, w + 50, h + 150,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );

        CreateToolbar();
        CreateCanvas(w, h);

        ShowWindow(m_editWnd, SW_SHOW);
        UpdateWindow(m_editWnd);
    }

    void CreateToolbar() {
        // 工具栏
        m_toolBar = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 800, 40, m_editWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        // 功能按钮
        CreateWindow(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE,
            10, 5, 50, 30, m_editWnd, (HMENU)ID_SAVE, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"复制", WS_CHILD | WS_VISIBLE,
            70, 5, 50, 30, m_editWnd, (HMENU)ID_COPY, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            130, 5, 50, 30, m_editWnd, (HMENU)ID_CANCEL, GetModuleHandle(nullptr), nullptr);

        // 工具按钮
        CreateWindow(L"BUTTON", L"选择", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            200, 5, 40, 30, m_editWnd, (HMENU)ID_TOOL_SELECT, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"画笔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            250, 5, 40, 30, m_editWnd, (HMENU)ID_TOOL_PEN, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"矩形", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            300, 5, 40, 30, m_editWnd, (HMENU)ID_TOOL_RECT, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"箭头", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            350, 5, 40, 30, m_editWnd, (HMENU)ID_TOOL_ARROW, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"文字", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            400, 5, 40, 30, m_editWnd, (HMENU)ID_TOOL_TEXT, GetModuleHandle(nullptr), nullptr);

        // 颜色按钮
        CreateColorButton(460, 5, RGB(255, 0, 0), ID_COLOR_RED);
        CreateColorButton(480, 5, RGB(0, 255, 0), ID_COLOR_GREEN);
        CreateColorButton(500, 5, RGB(0, 0, 255), ID_COLOR_BLUE);
        CreateColorButton(520, 5, RGB(255, 255, 0), ID_COLOR_YELLOW);
        CreateColorButton(540, 5, RGB(0, 0, 0), ID_COLOR_BLACK);

        // 粗细按钮
        CreateWindow(L"BUTTON", L"细", WS_CHILD | WS_VISIBLE,
            570, 5, 30, 30, m_editWnd, (HMENU)ID_THICKNESS_1, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"中", WS_CHILD | WS_VISIBLE,
            610, 5, 30, 30, m_editWnd, (HMENU)ID_THICKNESS_2, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"粗", WS_CHILD | WS_VISIBLE,
            650, 5, 30, 30, m_editWnd, (HMENU)ID_THICKNESS_4, GetModuleHandle(nullptr), nullptr);
    }

    void CreateColorButton(int x, int y, COLORREF color, int id) {
        HWND btn = CreateWindow(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            x, y, 18, 18, m_editWnd, (HMENU)id, GetModuleHandle(nullptr), nullptr);
        SetWindowLongPtr(btn, GWLP_USERDATA, (LONG_PTR)color);
    }

    void CreateCanvas(int w, int h) {
        // 创建编辑画布的位图
        HDC screenDC = GetDC(nullptr);
        m_editDC = CreateCompatibleDC(screenDC);
        m_editBitmap = CreateCompatibleBitmap(screenDC, w, h);
        SelectObject(m_editDC, m_editBitmap);

        // 复制选中区域到编辑画布
        HDC tempDC = CreateCompatibleDC(screenDC);
        SelectObject(tempDC, m_screenBitmap);
        BitBlt(m_editDC, 0, 0, w, h, tempDC,
            m_selectionRect.left, m_selectionRect.top, SRCCOPY);

        DeleteDC(tempDC);
        ReleaseDC(nullptr, screenDC);
    }

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        return s_instance ? s_instance->HandleOverlayMsg(hwnd, msg, wp, lp) :
            DefWindowProc(hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK EditWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        return s_instance ? s_instance->HandleEditMsg(hwnd, msg, wp, lp) :
            DefWindowProc(hwnd, msg, wp, lp);
    }

    LRESULT HandleOverlayMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                PostMessage(hwnd, WM_USER + 1, 0, 0);
            }
            break;

        case WM_LBUTTONDOWN:
            m_selecting = true;
            m_startPt = { LOWORD(lp), HIWORD(lp) };
            m_currentPt = m_startPt;
            SetCapture(hwnd);
            break;

        case WM_MOUSEMOVE:
            if (m_selecting) {
                m_currentPt = { LOWORD(lp), HIWORD(lp) };
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case WM_LBUTTONUP:
            if (m_selecting) {
                m_selecting = false;
                ReleaseCapture();
                PostMessage(hwnd, WM_USER + 1, 0, 0);
            }
            break;

        case WM_PAINT:
            OnOverlayPaint(hwnd);
            break;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        return 0;
    }

    LRESULT HandleEditMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_COMMAND:
            HandleCommand(LOWORD(wp));
            break;

        case WM_LBUTTONDOWN:
            if (HIWORD(lp) > 45) { // 避免工具栏区域
                StartDrawing({ LOWORD(lp), HIWORD(lp) - 45 });
            }
            break;

        case WM_MOUSEMOVE:
            if (m_drawing && HIWORD(lp) > 45) {
                ContinueDrawing({ LOWORD(lp), HIWORD(lp) - 45 });
            }
            break;

        case WM_LBUTTONUP:
            if (m_drawing) {
                EndDrawing();
            }
            break;

        case WM_PAINT:
            OnEditPaint(hwnd);
            break;

        case WM_DRAWITEM:
            OnDrawColorButton((DRAWITEMSTRUCT*)lp);
            break;

        case WM_CLOSE:
            PostMessage(hwnd, WM_USER + 2, 0, 0);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
        }
        return 0;
    }

    void HandleCommand(int id) {
        switch (id) {
        case ID_SAVE:
            PostMessage(m_editWnd, WM_USER + 2, 1, 0);
            break;

        case ID_COPY:
            CopyToClipboard();
            break;

        case ID_CANCEL:
            PostMessage(m_editWnd, WM_USER + 2, 0, 0);
            break;

        case ID_TOOL_SELECT: m_currentTool = TOOL_SELECT; break;
        case ID_TOOL_PEN: m_currentTool = TOOL_PEN; break;
        case ID_TOOL_RECT: m_currentTool = TOOL_RECTANGLE; break;
        case ID_TOOL_ARROW: m_currentTool = TOOL_ARROW; break;
        case ID_TOOL_TEXT: m_currentTool = TOOL_TEXT; break;
        case ID_TOOL_BLUR: m_currentTool = TOOL_BLUR; break;

        case ID_COLOR_RED: m_currentColor = RGB(255, 0, 0); break;
        case ID_COLOR_GREEN: m_currentColor = RGB(0, 255, 0); break;
        case ID_COLOR_BLUE: m_currentColor = RGB(0, 0, 255); break;
        case ID_COLOR_YELLOW: m_currentColor = RGB(255, 255, 0); break;
        case ID_COLOR_BLACK: m_currentColor = RGB(0, 0, 0); break;

        case ID_THICKNESS_1: m_currentThickness = 1; break;
        case ID_THICKNESS_2: m_currentThickness = 3; break;
        case ID_THICKNESS_4: m_currentThickness = 6; break;
        }

        UpdateToolbarSelection();
    }

    void UpdateToolbarSelection() {
        // 这里可以添加工具栏按钮状态更新逻辑
        InvalidateRect(m_editWnd, nullptr, TRUE);
    }

    void StartDrawing(POINT pt) {
        if (m_currentTool == TOOL_SELECT) return;

        m_drawing = true;
        m_lastPoint = pt;

        DrawCommand cmd;
        cmd.tool = m_currentTool;
        cmd.color = m_currentColor;
        cmd.thickness = m_currentThickness;
        cmd.points.push_back(pt);

        if (m_currentTool == TOOL_TEXT) {
            // 弹出文字输入对话框
            wchar_t text[256] = L"";
            if (InputTextDialog(text, 256)) {
                cmd.text = text;
                m_commands.push_back(cmd);
                RenderCommand(m_editDC, cmd);
            }
        }
        else {
            m_commands.push_back(cmd);
        }

        SetCapture(m_editWnd);
    }

    void ContinueDrawing(POINT pt) {
        if (!m_drawing || m_commands.empty()) return;

        auto& cmd = m_commands.back();

        if (m_currentTool == TOOL_PEN) {
            // 实时绘制画笔轨迹
            HPEN pen = CreatePen(PS_SOLID, m_currentThickness, m_currentColor);
            HPEN oldPen = (HPEN)SelectObject(m_editDC, pen);

            MoveToEx(m_editDC, m_lastPoint.x, m_lastPoint.y, nullptr);
            LineTo(m_editDC, pt.x, pt.y);

            SelectObject(m_editDC, oldPen);
            DeleteObject(pen);

            cmd.points.push_back(pt);
            m_lastPoint = pt;
        }
        else {
            // 其他工具只保存终点，在鼠标释放时绘制
            if (cmd.points.size() > 1) {
                cmd.points.back() = pt;
            }
            else {
                cmd.points.push_back(pt);
            }
        }

        InvalidateRect(m_editWnd, nullptr, FALSE);
    }

    void EndDrawing() {
        if (!m_drawing) return;

        m_drawing = false;
        ReleaseCapture();

        if (!m_commands.empty() && m_currentTool != TOOL_PEN) {
            RenderCommand(m_editDC, m_commands.back());
        }

        InvalidateRect(m_editWnd, nullptr, FALSE);
    }

    void RenderCommand(HDC hdc, const DrawCommand& cmd) {
        HPEN pen = CreatePen(PS_SOLID, cmd.thickness, cmd.color);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);

        switch (cmd.tool) {
        case TOOL_PEN:
            RenderPenStroke(hdc, cmd.points);
            break;
        case TOOL_RECTANGLE:
            RenderRectangle(hdc, cmd.points);
            break;
        case TOOL_ARROW:
            RenderArrow(hdc, cmd.points);
            break;
        case TOOL_TEXT:
            RenderText(hdc, cmd.points, cmd.text, cmd.color);
            break;
        }

        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    void RenderPenStroke(HDC hdc, const std::vector<POINT>& points) {
        if (points.size() < 2) return;

        MoveToEx(hdc, points[0].x, points[0].y, nullptr);
        for (size_t i = 1; i < points.size(); i++) {
            LineTo(hdc, points[i].x, points[i].y);
        }
    }

    void RenderRectangle(HDC hdc, const std::vector<POINT>& points) {
        if (points.size() >= 2) {
            HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
            Rectangle(hdc, points[0].x, points[0].y, points.back().x, points.back().y);
            SelectObject(hdc, oldBrush);
        }
    }

    void RenderArrow(HDC hdc, const std::vector<POINT>& points) {
        if (points.size() >= 2) {
            POINT start = points[0];
            POINT end = points.back();

            // 绘制箭头线
            MoveToEx(hdc, start.x, start.y, nullptr);
            LineTo(hdc, end.x, end.y);

            // 计算箭头头部
            double angle = atan2(end.y - start.y, end.x - start.x);
            int arrowLen = 15;
            double arrowAngle = 0.5;

            POINT arrow1 = {
                (LONG)(end.x - arrowLen * cos(angle - arrowAngle)),
                (LONG)(end.y - arrowLen * sin(angle - arrowAngle))
            };

            POINT arrow2 = {
                (LONG)(end.x - arrowLen * cos(angle + arrowAngle)),
                (LONG)(end.y - arrowLen * sin(angle + arrowAngle))
            };

            MoveToEx(hdc, end.x, end.y, nullptr);
            LineTo(hdc, arrow1.x, arrow1.y);
            MoveToEx(hdc, end.x, end.y, nullptr);
            LineTo(hdc, arrow2.x, arrow2.y);
        }
    }

    void RenderText(HDC hdc, const std::vector<POINT>& points, const std::wstring& text, COLORREF color) {
        if (points.empty() || text.empty()) return;

        SetTextColor(hdc, color);
        SetBkMode(hdc, TRANSPARENT);

        HFONT font = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT oldFont = (HFONT)SelectObject(hdc, font);

        TextOut(hdc, points[0].x, points[0].y, text.c_str(), text.length());

        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }

    bool InputTextDialog(wchar_t* buffer, int bufferSize) {
        // 简单的文本输入对话框
        return (InputBox(L"输入文字", L"请输入要添加的文字:", buffer, bufferSize) == IDOK);
    }

    int InputBox(const wchar_t* title, const wchar_t* prompt, wchar_t* buffer, int bufferSize) {
        // 创建简单的输入对话框
        HWND dlg = CreateWindow(L"#32770", title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            300, 300, 300, 120, m_editWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"STATIC", prompt, WS_CHILD | WS_VISIBLE,
            10, 10, 260, 20, dlg, nullptr, GetModuleHandle(nullptr), nullptr);

        HWND edit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            10, 35, 260, 25, dlg, nullptr, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE,
            50, 70, 60, 25, dlg, (HMENU)IDOK, GetModuleHandle(nullptr), nullptr);

        CreateWindow(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            150, 70, 60, 25, dlg, (HMENU)IDCANCEL, GetModuleHandle(nullptr), nullptr);

        ShowWindow(dlg, SW_SHOW);
        SetFocus(edit);

        MSG msg;
        int result = 0;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_COMMAND) {
                if (LOWORD(msg.wParam) == IDOK) {
                    GetWindowText(edit, buffer, bufferSize);
                    result = IDOK;
                    break;
                }
                else if (LOWORD(msg.wParam) == IDCANCEL) {
                    result = IDCANCEL;
                    break;
                }
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DestroyWindow(dlg);
        return result;
    }

    void OnOverlayPaint(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HDC memDC = CreateCompatibleDC(hdc);
        SelectObject(memDC, m_screenBitmap);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // 绘制暗化的截图
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

        // 半透明遮罩
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 100, 0 };

        // 绘制选择框
        if (m_selecting || (m_startPt.x != m_currentPt.x || m_startPt.y != m_currentPt.y)) {
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 120, 255));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

            Rectangle(hdc, m_startPt.x, m_startPt.y, m_currentPt.x, m_currentPt.y);

            // 显示尺寸信息
            int w = abs(m_currentPt.x - m_startPt.x);
            int h = abs(m_currentPt.y - m_startPt.y);
            if (w > 0 && h > 0) {
                wchar_t sizeText[64];
                swprintf_s(sizeText, L"%d × %d", w, h);

                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkColor(hdc, RGB(0, 120, 255));
                TextOut(hdc, m_currentPt.x + 5, m_currentPt.y - 20, sizeText, wcslen(sizeText));
            }

            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
        }

        DeleteObject(brush);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
    }

    void OnEditPaint(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 绘制编辑后的图像
        BitBlt(hdc, 0, 45,
            m_selectionRect.right - m_selectionRect.left,
            m_selectionRect.bottom - m_selectionRect.top,
            m_editDC, 0, 0, SRCCOPY);

        // 绘制正在进行的图形（实时预览）
        if (m_drawing && !m_commands.empty() && m_currentTool != TOOL_PEN) {
            HPEN pen = CreatePen(PS_SOLID, m_currentThickness, m_currentColor);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);

            auto& cmd = m_commands.back();
            std::vector<POINT> adjustedPoints;
            for (const auto& pt : cmd.points) {
                adjustedPoints.push_back({ pt.x, pt.y + 45 });
            }

            switch (m_currentTool) {
            case TOOL_RECTANGLE:
                if (adjustedPoints.size() >= 2) {
                    HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
                    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
                    Rectangle(hdc, adjustedPoints[0].x, adjustedPoints[0].y,
                        adjustedPoints.back().x, adjustedPoints.back().y);
                    SelectObject(hdc, oldBrush);
                }
                break;
            }

            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }

        EndPaint(hwnd, &ps);
    }

    void OnDrawColorButton(DRAWITEMSTRUCT* dis) {
        COLORREF color = (COLORREF)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);

        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dis->hDC, &dis->rcItem, brush);
        DeleteObject(brush);

        if (color == m_currentColor) {
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HPEN oldPen = (HPEN)SelectObject(dis->hDC, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));

            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                dis->rcItem.right, dis->rcItem.bottom);

            SelectObject(dis->hDC, oldPen);
            SelectObject(dis->hDC, oldBrush);
            DeleteObject(pen);
        }
    }

    void CopyToClipboard() {
        if (!OpenClipboard(m_editWnd)) return;

        EmptyClipboard();

        int w = m_selectionRect.right - m_selectionRect.left;
        int h = m_selectionRect.bottom - m_selectionRect.top;

        HBITMAP clipBitmap = CreateCompatibleBitmap(m_editDC, w, h);
        HDC clipDC = CreateCompatibleDC(m_editDC);
        SelectObject(clipDC, clipBitmap);
        BitBlt(clipDC, 0, 0, w, h, m_editDC, 0, 0, SRCCOPY);

        SetClipboardData(CF_BITMAP, clipBitmap);

        DeleteDC(clipDC);
        CloseClipboard();

        MessageBox(m_editWnd, L"图片已复制到剪贴板", L"提示", MB_OK | MB_ICONINFORMATION);
    }

    void SaveScreenshot() {
        // 获取当前时间作为文件名
        SYSTEMTIME st;
        GetLocalTime(&st);

        wchar_t filename[MAX_PATH];
        swprintf_s(filename, L"Screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        SaveBitmapToFile(m_editBitmap, filename);

        wchar_t msg[MAX_PATH + 50];
        swprintf_s(msg, L"截图已保存为: %s", filename);
        MessageBox(m_editWnd, msg, L"保存成功", MB_OK | MB_ICONINFORMATION);
    }

    bool SaveBitmapToFile(HBITMAP bitmap, const wchar_t* filename) {
        HDC hdc = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(hdc);

        BITMAP bm;
        GetObject(bitmap, sizeof(bm), &bm);

        BITMAPINFOHEADER bih = {};
        bih.biSize = sizeof(bih);
        bih.biWidth = bm.bmWidth;
        bih.biHeight = bm.bmHeight;
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = BI_RGB;

        int dataSize = ((bm.bmWidth * 3 + 3) & ~3) * bm.bmHeight;

        BITMAPFILEHEADER bfh = {};
        bfh.bfType = 0x4D42;
        bfh.bfSize = sizeof(bfh) + sizeof(bih) + dataSize;
        bfh.bfOffBits = sizeof(bfh) + sizeof(bih);

        std::vector<BYTE> data(dataSize);
        GetDIBits(hdc, bitmap, 0, bm.bmHeight, data.data(), (BITMAPINFO*)&bih, DIB_RGB_COLORS);

        HANDLE file = CreateFile(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(file, &bfh, sizeof(bfh), &written, nullptr);
            WriteFile(file, &bih, sizeof(bih), &written, nullptr);
            WriteFile(file, data.data(), dataSize, &written, nullptr);
            CloseHandle(file);

            DeleteDC(memDC);
            ReleaseDC(nullptr, hdc);
            return true;
        }

        DeleteDC(memDC);
        ReleaseDC(nullptr, hdc);
        return false;
    }

    void CleanupOverlay() {
        if (m_overlayWnd) {
            DestroyWindow(m_overlayWnd);
            m_overlayWnd = nullptr;
        }
        if (m_screenBitmap) {
            DeleteObject(m_screenBitmap);
            m_screenBitmap = nullptr;
        }
        UnregisterClass(L"ScreenSelector", GetModuleHandle(nullptr));
    }

    void CleanupEditor() {
        if (m_editWnd) {
            DestroyWindow(m_editWnd);
            m_editWnd = nullptr;
        }
        if (m_editBitmap) {
            DeleteObject(m_editBitmap);
            m_editBitmap = nullptr;
        }
        if (m_editDC) {
            DeleteDC(m_editDC);
            m_editDC = nullptr;
        }
        UnregisterClass(L"ScreenEditor", GetModuleHandle(nullptr));
        s_instance = nullptr;
    }
};

ExtendedScreenshot* ExtendedScreenshot::s_instance = nullptr;

// 主程序入口
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    auto result = ExtendedScreenshot::CaptureAndEdit();

    if (result.success) {
        wchar_t msg[512];
        swprintf_s(msg, L"截图编辑完成！\n区域: %d,%d - %dx%d\n文件: %s",
            result.rect.left, result.rect.top,
            result.rect.right - result.rect.left,
            result.rect.bottom - result.rect.top,
            result.savedPath.c_str());

        MessageBox(nullptr, msg, L"截图工具 - by hesphoros", MB_OK | MB_ICONINFORMATION);
    }

    return 0;
}