#undef UNICODE
#undef _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <direct.h>

// ----------------- PNG CREATION -----------------
uint32_t crc_table[256];
bool crc_table_computed = false;

void make_crc_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1) c = 0xedb88320L ^ (c >> 1);
            else       c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_computed = true;
}

uint32_t update_crc(uint32_t crc, unsigned char *buf, int len) {
    uint32_t c = crc;
    if (!crc_table_computed) make_crc_table();
    for (int n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

uint32_t crc(unsigned char *buf, int len) {
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

uint32_t adler32(const unsigned char *data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void write_u32(std::ofstream &f, uint32_t v) {
    unsigned char buf[4];
    buf[0] = (v >> 24) & 0xff;
    buf[1] = (v >> 16) & 0xff;
    buf[2] = (v >> 8) & 0xff;
    buf[3] = v & 0xff;
    f.write((char*)buf, 4);
}

void write_chunk(std::ofstream &f, const char *type, const std::vector<unsigned char> &data) {
    write_u32(f, data.size());
    f.write(type, 4);
    if (!data.empty()) f.write((const char*)data.data(), data.size());

    std::vector<unsigned char> crcbuf(4 + data.size());
    memcpy(crcbuf.data(), type, 4);
    if (!data.empty()) memcpy(crcbuf.data()+4, data.data(), data.size());
    write_u32(f, crc(crcbuf.data(), (int)crcbuf.size()));
}

void create_white_png(const char *filename, int width, int height) {
    _mkdir("img"); // make sure img/ exists

    std::ofstream f(filename, std::ios::binary);
    if (!f.is_open()) return;

    unsigned char signature[8] = {137,80,78,71,13,10,26,10};
    f.write((char*)signature, 8);

    std::vector<unsigned char> ihdr(13);
    ihdr[0] = (width >> 24) & 0xff;
    ihdr[1] = (width >> 16) & 0xff;
    ihdr[2] = (width >> 8) & 0xff;
    ihdr[3] = width & 0xff;
    ihdr[4] = (height >> 24) & 0xff;
    ihdr[5] = (height >> 16) & 0xff;
    ihdr[6] = (height >> 8) & 0xff;
    ihdr[7] = height & 0xff;
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 2;  // color type: truecolor RGB
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    write_chunk(f, "IHDR", ihdr);

    std::vector<unsigned char> raw;
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        for (int x = 0; x < width; x++) {
            raw.push_back(255);
            raw.push_back(255);
            raw.push_back(255);
        }
    }

    std::vector<unsigned char> zdata;
    zdata.push_back(0x78);
    zdata.push_back(0x01);

    size_t len = raw.size();
    zdata.push_back(1);
    zdata.push_back(len & 0xff);
    zdata.push_back((len >> 8) & 0xff);
    unsigned nlen = ~len;
    zdata.push_back(nlen & 0xff);
    zdata.push_back((nlen >> 8) & 0xff);
    zdata.insert(zdata.end(), raw.begin(), raw.end());

    uint32_t ad = adler32(raw.data(), raw.size());
    zdata.push_back((ad >> 24) & 0xff);
    zdata.push_back((ad >> 16) & 0xff);
    zdata.push_back((ad >> 8) & 0xff);
    zdata.push_back(ad & 0xff);

    write_chunk(f, "IDAT", zdata);

    std::vector<unsigned char> iend;
    write_chunk(f, "IEND", iend);

    f.close();
}

// ----------------- GUI -----------------
const char g_szClassName[] = "MyWindowClass";
#define ID_RUNSCRIPT 1
#define ID_EDITBOX   2

HWND hEditBox;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_RUNSCRIPT) {
                char buffer[256];
                GetWindowText(hEditBox, buffer, sizeof(buffer));
                if (strlen(buffer) == 0) {
                    MessageBox(hwnd, "Please enter a filename.", "Error", MB_OK);
                } else {
                    std::string path = "img\\";
                    path += buffer;
                    path += ".png";
                    create_white_png(path.c_str(), 64, 64);
                    std::string msg = "Created " + path;
                    MessageBox(hwnd, msg.c_str(), "Info", MB_OK);
                }
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc;
    HWND hwnd;
    MSG Msg;

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = g_szClassName;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        g_szClassName,
        "cpp-image-gen",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 200,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Text box
    hEditBox = CreateWindowEx(
        WS_EX_CLIENTEDGE, "EDIT", "output",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        50, 20, 200, 25,
        hwnd, (HMENU)ID_EDITBOX, hInstance, NULL);

    // Button
    CreateWindow(
        "BUTTON", "generate image",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        50, 60, 200, 40,
        hwnd, (HMENU)ID_RUNSCRIPT,
        hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&Msg, NULL, 0, 0) > 0) {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
    return Msg.wParam;
}
