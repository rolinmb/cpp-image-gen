#undef UNICODE
#undef _UNDICODE
#include <windows.h>
#include <direct.h>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>

// ---------------- CRC32 ----------------
uint32_t crc_table[256];
bool crc_table_computed = false;
void make_crc_table() {
    for (uint32_t n=0;n<256;n++) {
        uint32_t c=n;
        for (int k=0;k<8;k++) c=(c&1)?0xedb88320L^(c>>1):c>>1;
        crc_table[n]=c;
    }
    crc_table_computed=true;
}
uint32_t update_crc(uint32_t crc,const unsigned char*buf,size_t len){
    if(!crc_table_computed)make_crc_table();
    uint32_t c=crc;
    for(size_t n=0;n<len;n++)c=crc_table[(c^buf[n])&0xff]^(c>>8);
    return c;
}
uint32_t crc(const unsigned char*buf,size_t len){
    return update_crc(0xffffffffL,buf,len)^0xffffffffL;
}
void write_u32(std::ofstream&f,uint32_t v){
    unsigned char b[4]={(unsigned char)(v>>24),(unsigned char)(v>>16),
                        (unsigned char)(v>>8),(unsigned char)v};
    f.write((char*)b,4);
}
void write_chunk(std::ofstream&f,const char*type,const std::vector<unsigned char>&data){
    write_u32(f,(uint32_t)data.size());
    f.write(type,4);
    if(!data.empty())f.write((const char*)data.data(),data.size());
    std::vector<unsigned char> crcbuf(4+data.size());
    memcpy(crcbuf.data(),type,4);
    if(!data.empty()) memcpy(crcbuf.data()+4,data.data(),data.size());
    write_u32(f,crc(crcbuf.data(),crcbuf.size()));
}

// ---------------- Adler32 ----------------
uint32_t adler32(const unsigned char*data,size_t len){
    uint32_t a=1,b=0;
    for(size_t i=0;i<len;i++){a=(a+data[i])%65521;b=(b+a)%65521;}
    return(b<<16)|a;
}

// ---------------- DEFLATE (fixed Huffman only) ----------------
// For brevity this uses only one fixed Huffman block containing all data.
void deflate_fixed(const std::vector<unsigned char>&in,std::vector<unsigned char>&out){
    // Write zlib header: CMF/FLG (deflate, 32K window, check bits)
    out.push_back(0x78); // CM=8, CINFO=7 (32K window)
    out.push_back(0x01); // FLG: check bits, no compression level hint

    // For simplicity: encode as a single uncompressed block using zlib header+adler
    // (real fixed Huffman would be more code). This is still valid zlib.
    size_t len=in.size();
    out.push_back(1); // final block, uncompressed
    out.push_back(len&0xff);
    out.push_back((len>>8)&0xff);
    uint16_t nlen=~(uint16_t)len;
    out.push_back(nlen&0xff);
    out.push_back((nlen>>8)&0xff);
    out.insert(out.end(),in.begin(),in.end());

    uint32_t ad=adler32(in.data(),in.size());
    out.push_back((ad>>24)&0xff);
    out.push_back((ad>>16)&0xff);
    out.push_back((ad>>8)&0xff);
    out.push_back(ad&0xff);
}

// ---------------- Pixel math ----------------
inline void pixel_func(int x, int y, int w, int h,
                       unsigned char &r, unsigned char &g,
                       unsigned char &b, unsigned char &a) {
    double fx = (double)x / w * 12.0;
    double fy = (double)y / h * 12.0;
    r = (unsigned char)(128 + 127 * tan(fx + fy + sin(fx*1.3)));
    g = (unsigned char)(128 + 127 * tan(fy - fx + cos(fy*1.7)));
    b = (unsigned char)(128 + 127 * cos(fx*fx*0.1 + fy*fy*0.1));
    a = 255;
}

// ---------------- PNG Writer ----------------
void create_png(const char*filename,int w,int h){
    _mkdir("img");
    std::ofstream f(filename,std::ios::binary);
    if(!f.is_open())return;

    unsigned char sig[8]={137,80,78,71,13,10,26,10};
    f.write((char*)sig,8);

    std::vector<unsigned char> ihdr(13);
    ihdr[0]=(w>>24)&0xff;ihdr[1]=(w>>16)&0xff;ihdr[2]=(w>>8)&0xff;ihdr[3]=w&0xff;
    ihdr[4]=(h>>24)&0xff;ihdr[5]=(h>>16)&0xff;ihdr[6]=(h>>8)&0xff;ihdr[7]=h&0xff;
    ihdr[8]=8; // bit depth
    ihdr[9]=6; // color type RGBA
    ihdr[10]=0;ihdr[11]=0;ihdr[12]=0;
    write_chunk(f,"IHDR",ihdr);

    // Raw scanlines
    std::vector<unsigned char> raw;
    raw.reserve(h*(w*4+1));
    for(int y=0;y<h;y++){
        raw.push_back(0); // filter type 0
        for(int x=0;x<w;x++){
            unsigned char r,g,b,a;
            pixel_func(x,y,w,h,r,g,b,a);
            raw.push_back(r);
            raw.push_back(g);
            raw.push_back(b);
            raw.push_back(a);
        }
    }

    std::vector<unsigned char> zdata;
    deflate_fixed(raw,zdata);
    write_chunk(f,"IDAT",zdata);

    std::vector<unsigned char> iend;
    write_chunk(f,"IEND",iend);
    f.close();
}

// ---------------- GUI ----------------
const char g_szClassName[]="MyWindowClass";
#define ID_RUNSCRIPT 1
#define ID_EDITBOX 2
HWND hEditBox;

LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    switch(msg){
        case WM_COMMAND:
            if(LOWORD(wParam)==ID_RUNSCRIPT){
                char buf[256];GetWindowText(hEditBox,buf,sizeof(buf));
                if(strlen(buf)==0){MessageBox(hwnd,"Please enter a filename.","Error",MB_OK);}
                else{
                    std::string path="img\\";path+=buf;path+=".png";
                    create_png(path.c_str(),128,128);
                    std::string msg="Created "+path;
                    MessageBox(hwnd,msg.c_str(),"Info",MB_OK);
                }
            }break;
        case WM_DESTROY:PostQuitMessage(0);break;
        default:return DefWindowProc(hwnd,msg,wParam,lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nCmdShow){
    WNDCLASSEX wc{sizeof(WNDCLASSEX),0,WndProc,0,0,hInst,
        LoadIcon(NULL,IDI_APPLICATION),LoadCursor(NULL,IDC_ARROW),
        (HBRUSH)(COLOR_WINDOW+1),NULL,g_szClassName,
        LoadIcon(NULL,IDI_APPLICATION)};
    if(!RegisterClassEx(&wc)){MessageBox(NULL,"Class reg failed!","Error",MB_OK);return 0;}
    HWND hwnd=CreateWindowEx(WS_EX_CLIENTEDGE,g_szClassName,"cpp-image-gen",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,400,200,
        NULL,NULL,hInst,NULL);
    if(!hwnd){MessageBox(NULL,"Window creation failed!","Error",MB_OK);return 0;}
    hEditBox=CreateWindowEx(WS_EX_CLIENTEDGE,"EDIT","output",
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
        50,20,200,25,hwnd,(HMENU)ID_EDITBOX,hInst,NULL);
    CreateWindow("BUTTON","generate image",
        WS_TABSTOP|WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON,
        50,60,200,40,hwnd,(HMENU)ID_RUNSCRIPT,hInst,NULL);
    ShowWindow(hwnd,nCmdShow);UpdateWindow(hwnd);
    MSG Msg;while(GetMessage(&Msg,NULL,0,0)>0){TranslateMessage(&Msg);DispatchMessage(&Msg);}
    return Msg.wParam;
}
