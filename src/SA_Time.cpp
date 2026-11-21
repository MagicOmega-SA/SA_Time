#include <windows.h>
#include "MatchClock.h"

namespace
{
constexpr unsigned CurrentFrame = 0xA8ED84;
constexpr unsigned GameSpeed = 0xA8EB60;
constexpr unsigned GameMode = 0xA8B238;
constexpr unsigned ScenarioStarted = 0xA8ED5C;
constexpr unsigned LoadPhase = 0xA9FAB0;
constexpr unsigned SpecialDialog = 0xA8EDA0;
constexpr unsigned CompositeSurface = 0x88731C;
constexpr unsigned ViewBounds = 0x886FA0;
constexpr unsigned RedChannelShift = 0x8A0DD0;

constexpr unsigned SurfaceLock = 0x5C;
constexpr unsigned SurfaceUnlock = 0x60;
constexpr unsigned SurfaceBytesPerPixel = 0x70;
constexpr unsigned SurfacePitch = 0x74;
constexpr unsigned SurfaceWidth = 0x7C;
constexpr unsigned SurfaceHeight = 0x80;

constexpr int BitmapWidth = 384;
constexpr int BitmapHeight = 64;

struct Viewport { int x, y, width, height; };

MatchClock clock;
HDC textDC;
unsigned char* textPixels;
int textWidth;
int lineHeight;
unsigned renderedSecond = ~0u;

bool Enabled()
{
    static int enabled = -1;
    if (enabled >= 0) return enabled != 0;
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    while (length && path[length - 1] != L'\\' && path[length - 1] != L'/') --length;
    constexpr wchar_t file[] = L"SA_Time.ini";
    if (length + ARRAYSIZE(file) > MAX_PATH) return false;
    lstrcpyW(path + length, file);
    enabled = GetPrivateProfileIntW(L"MatchClock", L"Enable", 1, path) != 0;
    return enabled != 0;
}

bool InMatch()
{
    return *reinterpret_cast<const volatile unsigned char*>(ScenarioStarted) &&
        !*reinterpret_cast<const volatile int*>(LoadPhase);
}

void SampleFrame()
{
    const int frame = *reinterpret_cast<const volatile int*>(CurrentFrame);
    if (frame < 0) return;
    const int mode = *reinterpret_cast<const volatile int*>(GameMode);
    const int speed = *reinterpret_cast<const volatile int*>(GameSpeed);
    clock.Observe(static_cast<unsigned>(frame), mode == 3 || mode == 4, speed);
}

bool InitText()
{
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = BitmapWidth;
    bitmap.bmiHeader.biHeight = -BitmapHeight;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HFONT font = CreateFontW(-40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    TEXTMETRICW metrics;
    if (!image || !pixels || !font || !SelectObject(dc, image) || !SelectObject(dc, font) ||
        !GetTextMetricsW(dc, &metrics) || metrics.tmHeight < 1 || metrics.tmHeight + 2 > BitmapHeight)
    {
        DeleteDC(dc);
        if (image) DeleteObject(image);
        if (font) DeleteObject(font);
        return false;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    textDC = dc;
    textPixels = static_cast<unsigned char*>(pixels);
    lineHeight = metrics.tmHeight + 2;
    return true;
}

bool PrepareText(unsigned seconds)
{
    if (!textDC && !InitText()) return false;
    if (seconds == renderedSecond) return true;

    wchar_t text[16];
    const unsigned hours = seconds / 3600;
    const unsigned minutes = seconds / 60 % 60;
    const unsigned remainder = seconds % 60;
    const int length = hours ? wsprintfW(text, L"%02u:%02u:%02u", hours, minutes, remainder)
                             : wsprintfW(text, L"%02u:%02u", minutes, remainder);
    SIZE size;
    if (!GetTextExtentPoint32W(textDC, text, length, &size) || size.cx <= 0 ||
        size.cx + 4 > BitmapWidth)
        return false;
    if (!PatBlt(textDC, 0, 0, BitmapWidth, BitmapHeight, BLACKNESS) ||
        !ExtTextOutW(textDC, 0, 0, 0, nullptr, text, length, nullptr))
    {
        renderedSecond = ~0u;
        return false;
    }
    GdiFlush();
    textWidth = size.cx;
    renderedSecond = seconds;
    return true;
}

struct PixelFormat
{
    unsigned redShift;
    unsigned greenMax;

    unsigned short Color(unsigned r, unsigned g, unsigned b) const
    {
        return static_cast<unsigned short>((((r * 31 + 127) / 255) << redShift) |
            (((g * greenMax + 127) / 255) << 5) | ((b * 31 + 127) / 255));
    }

    unsigned short Blend(unsigned short back, unsigned short front, unsigned alpha) const
    {
        if (!alpha) return back;
        if (alpha == 255) return front;
        const unsigned inverse = 255 - alpha;
        const unsigned r = (((back >> redShift) & 31) * inverse +
            ((front >> redShift) & 31) * alpha + 127) / 255;
        const unsigned g = (((back >> 5) & greenMax) * inverse +
            ((front >> 5) & greenMax) * alpha + 127) / 255;
        const unsigned b = ((back & 31) * inverse + (front & 31) * alpha + 127) / 255;
        return static_cast<unsigned short>((r << redShift) | (g << 5) | b);
    }
};

unsigned Coverage(int x, int y)
{
    if (x < 0 || x >= textWidth + 2 || y < 0 || y >= lineHeight) return 0;
    return textPixels[(y * BitmapWidth + x) * 4 + 2];
}

unsigned ShadowCoverage(int x, int y)
{
    // The shadow combines four offset glyph masks without painting the panel.
    unsigned remaining = 255;
    for (int offsetY = 2; offsetY <= 3; ++offsetY)
    {
        for (int offsetX = 1; offsetX <= 2; ++offsetX)
        {
            const unsigned alpha = (Coverage(x - offsetX, y - offsetY) * 75 + 50) / 100;
            remaining = (remaining * (255 - alpha) + 127) / 255;
        }
    }
    return 255 - remaining;
}

void Paint(unsigned char* pixels, int pitch, int left, int top)
{
    const bool rgb555 = *reinterpret_cast<const unsigned*>(RedChannelShift) == 10;
    const PixelFormat format = rgb555 ? PixelFormat{10, 31} : PixelFormat{11, 63};
    const auto cream = format.Color(247, 235, 206);
    const auto gold = format.Color(168, 138, 82);
    for (int y = 0; y < lineHeight + 3; ++y)
    {
        auto* row = reinterpret_cast<unsigned short*>(pixels + (top + 8 + y) * pitch + (left + 30) * 2);
        for (int x = 0; x < textWidth + 4; ++x)
        {
            const unsigned alpha = Coverage(x, y);
            const unsigned shadow = alpha < 255 ? ShadowCoverage(x, y) : 0;
            if (alpha || shadow)
                row[x] = format.Blend(format.Blend(row[x], 0, shadow), cream, alpha);
        }
    }
    for (int y = lineHeight + 17; y < lineHeight + 20; ++y)
    {
        auto* row = reinterpret_cast<unsigned short*>(pixels + (top + y) * pitch + left * 2);
        for (int x = 0; x < textWidth + 60; ++x) row[x] = gold;
    }
}

void Draw()
{
    void* surf = *reinterpret_cast<void**>(CompositeSurface);
    if (!surf) return;
    auto* vtable = *reinterpret_cast<void***>(surf);
    if (!vtable) return;

    using GetInt = int (__thiscall*)(void*);
    using Lock = unsigned char* (__thiscall*)(void*, int, int);
    using Unlock = bool (__thiscall*)(void*);
    const int bytes = reinterpret_cast<GetInt>(vtable[SurfaceBytesPerPixel / 4])(surf);
    const int width = reinterpret_cast<GetInt>(vtable[SurfaceWidth / 4])(surf);
    const int height = reinterpret_cast<GetInt>(vtable[SurfaceHeight / 4])(surf);
    const int pitch = reinterpret_cast<GetInt>(vtable[SurfacePitch / 4])(surf);
    if (bytes != 2 || width <= 0 || height <= 0 || pitch <= 0 || width > pitch / 2) return;

    const auto& bounds = *reinterpret_cast<const Viewport*>(ViewBounds);
    if (bounds.x < 0 || bounds.width <= 0 || bounds.x >= width ||
        bounds.height <= 0 || bounds.y >= height)
        return;
    const int viewTop = bounds.y < 0 ? 0 : bounds.y;
    const int viewWidth = bounds.width < width - bounds.x ? bounds.width : width - bounds.x;
    const int viewHeight = bounds.height < height - viewTop ? bounds.height : height - viewTop;
    const int center = bounds.x + viewWidth / 2;
    if (!PrepareText(clock.Seconds())) return;
    const int panelWidth = textWidth + 60;
    const int panelHeight = lineHeight + 20;
    const int left = center - panelWidth / 2;
    if (panelWidth > viewWidth || panelHeight > viewHeight) return;
    const int offset = 2 * panelWidth;
    const int maxOffset = viewHeight - panelHeight;
    const int top = viewTop + (offset < maxOffset ? offset : maxOffset);

    auto* pixels = reinterpret_cast<Lock>(vtable[SurfaceLock / 4])(surf, 0, 0);
    if (!pixels) return;
    Paint(pixels, pitch, left, top);
    reinterpret_cast<Unlock>(vtable[SurfaceUnlock / 4])(surf);
}
}

extern "C" __declspec(dllexport) DWORD __cdecl SA_Time_Reset(void*)
{
    clock.Reset();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl SA_Time_Update(void*)
{
    if (InMatch() && Enabled()) SampleFrame();
    return 0;
}

extern "C" __declspec(dllexport) DWORD __cdecl SA_Time_Draw(void*)
{
    if (!InMatch() || !Enabled() || *reinterpret_cast<const volatile int*>(SpecialDialog)) return 0;
    SampleFrame();
    Draw();
    return 0;
}
