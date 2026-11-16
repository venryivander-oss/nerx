#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "raylib.h"
#include "nrx_3d.h"
#include "nrx_3d_crt.inc"

static int opened=0, in3d=0, inFrame=0, depth=0;
static Camera3D cam;

// rlgl (ikut di libraylib.a, header-nya tidak disertakan)
void rlBegin(int mode);
void rlEnd(void);
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void rlVertex3f(float x, float y, float z);
void rlPushMatrix(void);
void rlPopMatrix(void);
void rlTranslatef(float x, float y, float z);
void rlRotatef(float angle, float x, float y, float z);
void rlScalef(float x, float y, float z);
bool rlCheckRenderBatchLimit(int vCount);
#define RL_TRIANGLES 0x0004

static Color shade(Color c, float f){
    return (Color){(unsigned char)(c.r*f),(unsigned char)(c.g*f),(unsigned char)(c.b*f),c.a};
}

// kubus dengan sisi diarsir (atas paling terang, bawah paling gelap), supaya bentuknya terbaca tanpa lampu
static void shadedCube(Vector3 p, float w, float h, float d, Color c){
    float x=p.x, y=p.y, z=p.z;
    w/=2; h/=2; d/=2;
    rlCheckRenderBatchLimit(36);
    rlBegin(RL_TRIANGLES);
    #define V(a,b,e) rlVertex3f(a,b,e)
    #define COL(f) { Color s=shade(c,f); rlColor4ub(s.r,s.g,s.b,s.a); }
    COL(0.88f) // depan
    V(x-w,y-h,z+d); V(x+w,y-h,z+d); V(x-w,y+h,z+d);
    V(x+w,y+h,z+d); V(x-w,y+h,z+d); V(x+w,y-h,z+d);
    COL(0.74f) // belakang
    V(x-w,y-h,z-d); V(x-w,y+h,z-d); V(x+w,y-h,z-d);
    V(x+w,y+h,z-d); V(x+w,y-h,z-d); V(x-w,y+h,z-d);
    COL(1.0f) // atas
    V(x-w,y+h,z-d); V(x-w,y+h,z+d); V(x+w,y+h,z+d);
    V(x+w,y+h,z-d); V(x-w,y+h,z-d); V(x+w,y+h,z+d);
    COL(0.6f) // bawah
    V(x-w,y-h,z-d); V(x+w,y-h,z+d); V(x-w,y-h,z+d);
    V(x+w,y-h,z-d); V(x+w,y-h,z+d); V(x-w,y-h,z-d);
    COL(0.8f) // kanan
    V(x+w,y-h,z-d); V(x+w,y+h,z-d); V(x+w,y+h,z+d);
    V(x+w,y-h,z+d); V(x+w,y-h,z-d); V(x+w,y+h,z+d);
    COL(0.7f) // kiri
    V(x-w,y-h,z-d); V(x-w,y+h,z+d); V(x-w,y+h,z-d);
    V(x-w,y-h,z+d); V(x-w,y+h,z+d); V(x-w,y-h,z-d);
    #undef V
    #undef COL
    rlEnd();
}

static void needOpen(const char *fn){
    if(!opened) nrxFail("%s: buka window dulu dengan open3d(judul, lebar, tinggi)",fn);
}

static float num(Value v, const char *fn){
    double d;
    if(!numLike(v,&d)) nrxFail("%s: argumen harus angka",fn);
    return (float)d;
}

static void argc(const char *fn, int n, int want, const char *how){
    if(n!=want) nrxFail("%s butuh %d argumen: %s",fn,want,how);
}

static Color colorOf(Value v, const char *fn){
    char *s=valToStr(v);
    static const struct { const char *n; Color c; } names[]={
        {"red",RED},{"green",GREEN},{"blue",BLUE},{"white",WHITE},{"black",BLACK},{"gray",GRAY},
        {"darkgray",DARKGRAY},{"yellow",YELLOW},{"orange",ORANGE},{"purple",PURPLE},{"pink",PINK},
        {"brown",BROWN},{"skyblue",SKYBLUE}};
    for(size_t i=0;i<sizeof names/sizeof *names;i++){
        if(!strcmp(s,names[i].n)){
            free(s);
            return names[i].c;
        }
    }
    unsigned r,g,b,al;
    // #rrggbbaa: dengan transparansi (00 = tembus, ff = penuh)
    if(s[0]=='#' && strlen(s)==9 && sscanf(s+1,"%2x%2x%2x%2x",&r,&g,&b,&al)==4){
        free(s);
        return (Color){(unsigned char)r,(unsigned char)g,(unsigned char)b,(unsigned char)al};
    }
    if(s[0]=='#' && strlen(s)==7 && sscanf(s+1,"%2x%2x%2x",&r,&g,&b)==3){
        free(s);
        return (Color){(unsigned char)r,(unsigned char)g,(unsigned char)b,255};
    }
    nrxFail("%s: warna %s tidak dikenal (contoh: \"red\" atau \"#ff8800\")",fn,s);
    return BLACK;
}

static int keyOf(Value v){
    char *s=valToStr(v);
    int k=-1;
    if(strlen(s)==1 && isalpha((unsigned char)s[0])) k=KEY_A+(toupper((unsigned char)s[0])-'A');
    else if(strlen(s)==1 && isdigit((unsigned char)s[0])) k=KEY_ZERO+(s[0]-'0');
    else {
        static const struct { const char *n; int k; } names[]={
            {"space",KEY_SPACE},{"enter",KEY_ENTER},{"escape",KEY_ESCAPE},{"shift",KEY_LEFT_SHIFT},
            {"up",KEY_UP},{"down",KEY_DOWN},{"left",KEY_LEFT},{"right",KEY_RIGHT},
            {"backspace",KEY_BACKSPACE},{"tab",KEY_TAB},{"ctrl",KEY_LEFT_CONTROL}};
        for(size_t i=0;i<sizeof names/sizeof *names;i++) if(!strcmp(s,names[i].n)) k=names[i].k;
    }
    if(k<0) nrxFail("key3d: tombol %s tidak dikenal (huruf, angka, space, enter, escape, shift, up, down, left, right)",s);
    free(s);
    return k;
}

static Value nrx3dCall(const char *name, Value *a, int n){
    if(!strcmp(name,"open3d")){
        argc(name,n,3,"open3d(judul, lebar, tinggi)");
        if(opened) nrxFail("open3d: window 3D sudah terbuka");
        char *t=valToStr(a[0]);
        SetTraceLogLevel(LOG_WARNING);
        // NRX3D_HIDDEN=1: window tidak tampil (untuk tes otomatis dan screenshot3d)
        const char *hid=getenv("NRX3D_HIDDEN");
        if(hid && hid[0]=='1') SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow((int)num(a[1],name),(int)num(a[2],name),t);
        free(t);
        if(!IsWindowReady()) nrxFail("open3d: window 3D tidak bisa dibuka");
        SetTargetFPS(60);
        cam=(Camera3D){{6,6,6},{0,0,0},{0,1,0},45,CAMERA_PERSPECTIVE};
        opened=1;
        return valStr("");
    }
    if(!strcmp(name,"running3d")){
        argc(name,n,0,"running3d()");
        return valBool(opened && !WindowShouldClose());
    }
    if(!strcmp(name,"close3d")){
        argc(name,n,0,"close3d()");
        if(opened) CloseWindow();
        opened=in3d=inFrame=depth=0;
        return valStr("");
    }
    if(!strcmp(name,"fps3d")){
        argc(name,n,1,"fps3d(60)");
        SetTargetFPS((int)num(a[0],name));
        return valStr("");
    }
    needOpen(name);
    if(!strcmp(name,"frameTime")){
        argc(name,n,0,"frameTime()");
        return valDec(GetFrameTime());
    }
    if(!strcmp(name,"key3d")){
        argc(name,n,1,"key3d(\"w\")");
        return valBool(IsKeyDown(keyOf(a[0])));
    }
    if(!strcmp(name,"mouse3d")){
        argc(name,n,0,"mouse3d()");
        Vector2 m=GetMousePosition();
        List *l=listNew();
        listPush(l,valNum((int)m.x));
        listPush(l,valNum((int)m.y));
        return valList(l);
    }
    if(!strcmp(name,"click3d")){
        argc(name,n,0,"click3d()");
        return valBool(IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    }
    if(!strcmp(name,"wheel3d")){
        argc(name,n,0,"wheel3d()");
        return valDec(GetMouseWheelMove());
    }
    if(!strcmp(name,"ground3d")){
        // titik di lantai (y = 0) yang ditunjuk mouse: [x, z], atau [] kalau mouse menunjuk ke langit
        argc(name,n,0,"ground3d()");
        Ray r=GetScreenToWorldRay(GetMousePosition(),cam);
        List *l=listNew();
        if(r.direction.y<-1e-6f){
            float t=-r.position.y/r.direction.y;
            listPush(l,valDec(r.position.x+r.direction.x*t));
            listPush(l,valDec(r.position.z+r.direction.z*t));
        }
        return valList(l);
    }
    if(!strcmp(name,"pressed3d")){
        // sekali saat ditekan, lalu berulang kalau ditahan (seperti mengetik)
        argc(name,n,1,"pressed3d(\"enter\")");
        int k=keyOf(a[0]);
        return valBool(IsKeyPressed(k) || IsKeyPressedRepeat(k));
    }
    if(!strcmp(name,"typed3d")){
        // huruf yang diketik sejak frame lalu (UTF-8)
        argc(name,n,0,"typed3d()");
        char buf[256];
        int len=0, c;
        while((c=GetCharPressed())>0 && len<250){
            int sz=0;
            const char *u=CodepointToUTF8(c,&sz);
            memcpy(buf+len,u,sz);
            len+=sz;
        }
        buf[len]=0;
        return valStr(buf);
    }
    if(!strcmp(name,"toScreen3d")){
        // titik 3D -> posisi piksel di layar [x, y] (untuk label di atas benda)
        argc(name,n,3,"toScreen3d(x, y, z)");
        Vector2 p=GetWorldToScreen((Vector3){num(a[0],name),num(a[1],name),num(a[2],name)},cam);
        List *l=listNew();
        listPush(l,valNum((int)p.x));
        listPush(l,valNum((int)p.y));
        return valList(l);
    }
    if(!strcmp(name,"exitKey3d")){
        // tombol yang menutup window (awal: escape); "" = tidak ada, tutup lewat tombol X saja
        argc(name,n,1,"exitKey3d(\"escape\")");
        char *s=valToStr(a[0]);
        int none=!s[0];
        free(s);
        SetExitKey(none ? 0 : keyOf(a[0]));
        return valStr("");
    }
    if(!strcmp(name,"clipboard3d")){
        // clipboard3d(teks) mengisi clipboard, clipboard3d() membacanya
        if(n==1){
            char *t=valToStr(a[0]);
            SetClipboardText(t);
            free(t);
            return valStr("");
        }
        argc(name,n,0,"clipboard3d() / clipboard3d(teks)");
        const char *t=GetClipboardText();
        return valStr(t ? t : "");
    }
    if(!strcmp(name,"screenshot3d")){
        // simpan isi window terakhir ke file .png
        argc(name,n,1,"screenshot3d(\"layar.png\")");
        char *f=valToStr(a[0]);
        Image im=LoadImageFromScreen();
        ExportImage(im,f);
        UnloadImage(im);
        free(f);
        return valStr("");
    }
    if(!strcmp(name,"textWidth2d")){
        argc(name,n,2,"textWidth2d(teks, ukuran)");
        char *t=valToStr(a[0]);
        int w=MeasureText(t,(int)num(a[1],name));
        free(t);
        return valNum(w);
    }
    if(!strcmp(name,"camera3d")){
        argc(name,n,6,"camera3d(x, y, z, lihatX, lihatY, lihatZ)");
        cam.position=(Vector3){num(a[0],name),num(a[1],name),num(a[2],name)};
        cam.target=(Vector3){num(a[3],name),num(a[4],name),num(a[5],name)};
        return valStr("");
    }
    if(!strcmp(name,"begin3d")){
        // begin3d(warna) atau begin3d(warnaAtas, warnaBawah) untuk latar bergradasi
        if(n!=1 && n!=2) nrxFail("begin3d butuh 1 atau 2 argumen: begin3d(warna) atau begin3d(atas, bawah)");
        if(inFrame) nrxFail("begin3d: frame sebelumnya belum ditutup dengan end3d()");
        Color c=colorOf(a[0],name);
        BeginDrawing();
        ClearBackground(c);
        if(n==2) DrawRectangleGradientV(0,0,GetScreenWidth(),GetScreenHeight(),c,colorOf(a[1],name));
        BeginMode3D(cam);
        inFrame=in3d=1;
        return valStr("");
    }
    if(!strcmp(name,"end3d")){
        argc(name,n,0,"end3d()");
        if(!inFrame) nrxFail("end3d: belum ada begin3d(warna)");
        if(depth) nrxFail("end3d: push3d belum ditutup dengan pop3d");
        if(in3d) EndMode3D();
        EndDrawing();
        inFrame=in3d=0;
        return valStr("");
    }
    if(!inFrame) nrxFail("%s: gambar di antara begin3d(warna) dan end3d()",name);
    if(!strcmp(name,"text2d")){
        argc(name,n,5,"text2d(teks, x, y, ukuran, warna)");
        char *t=valToStr(a[0]);
        Color c=colorOf(a[4],name);
        if(depth) nrxFail("%s: tulis 2D sesudah pop3d()",name);
        // tulisan 2D digambar di luar mode 3D, lalu kembali ke 3D
        if(in3d) EndMode3D();
        DrawText(t,(int)num(a[1],name),(int)num(a[2],name),(int)num(a[3],name),c);
        if(in3d) BeginMode3D(cam);
        free(t);
        return valStr("");
    }
    if(!strcmp(name,"push3d")){
        // simpan posisi dan arah gambar sekarang; move3d/turn3d sesudahnya berlaku sampai pop3d
        argc(name,n,0,"push3d()");
        if(!in3d) nrxFail("push3d: tidak bisa dipakai di antara tulisan 2D");
        if(depth>=16) nrxFail("push3d: terlalu dalam (maksimal 16 tingkat)");
        rlPushMatrix();
        depth++;
        return valStr("");
    }
    if(!strcmp(name,"pop3d")){
        argc(name,n,0,"pop3d()");
        if(!depth) nrxFail("pop3d tanpa push3d");
        rlPopMatrix();
        depth--;
        return valStr("");
    }
    if(!strcmp(name,"move3d")){
        argc(name,n,3,"move3d(x, y, z)");
        if(!depth) nrxFail("%s: panggil push3d() dulu",name);
        rlTranslatef(num(a[0],name),num(a[1],name),num(a[2],name));
        return valStr("");
    }
    if(!strcmp(name,"turn3d")){
        // putar sekian derajat pada sumbu "x", "y", atau "z" di titik sekarang
        argc(name,n,2,"turn3d(\"y\", derajat)");
        if(!depth) nrxFail("%s: panggil push3d() dulu",name);
        char *s=valToStr(a[0]);
        float x=0,y=0,z=0;
        if(!strcmp(s,"x")) x=1;
        else if(!strcmp(s,"y")) y=1;
        else if(!strcmp(s,"z")) z=1;
        else { free(s); nrxFail("turn3d: sumbu harus \"x\", \"y\", atau \"z\""); }
        free(s);
        rlRotatef(num(a[1],name),x,y,z);
        return valStr("");
    }
    if(!strcmp(name,"cylinder")){
        argc(name,n,7,"cylinder(x, y, z, jariAtas, jariBawah, tinggi, warna)");
        // y = dasar tabung
        DrawCylinder((Vector3){num(a[0],name),num(a[1],name),num(a[2],name)},num(a[3],name),num(a[4],name),
            num(a[5],name),16,colorOf(a[6],name));
        return valStr("");
    }
    if(!strcmp(name,"rectGrad2d")){
        argc(name,n,6,"rectGrad2d(x, y, lebar, tinggi, warnaAtas, warnaBawah)");
        Color c1=colorOf(a[4],name), c2=colorOf(a[5],name);
        if(in3d) EndMode3D();
        DrawRectangleGradientV((int)num(a[0],name),(int)num(a[1],name),(int)num(a[2],name),(int)num(a[3],name),c1,c2);
        if(in3d) BeginMode3D(cam);
        return valStr("");
    }
    if(!strcmp(name,"rect2d")){
        argc(name,n,5,"rect2d(x, y, lebar, tinggi, warna)");
        Color c=colorOf(a[4],name);
        if(depth) nrxFail("%s: tulis 2D sesudah pop3d()",name);
        if(in3d) EndMode3D();
        DrawRectangle((int)num(a[0],name),(int)num(a[1],name),(int)num(a[2],name),(int)num(a[3],name),c);
        if(in3d) BeginMode3D(cam);
        return valStr("");
    }
    if(!strcmp(name,"grid")){
        argc(name,n,2,"grid(jumlah, jarak)");
        DrawGrid((int)num(a[0],name),num(a[1],name));
        return valStr("");
    }
    if(!strcmp(name,"sphere")){
        argc(name,n,5,"sphere(x, y, z, jari, warna)");
        DrawSphere((Vector3){num(a[0],name),num(a[1],name),num(a[2],name)},num(a[3],name),colorOf(a[4],name));
        return valStr("");
    }
    if(!strcmp(name,"plane")){
        argc(name,n,6,"plane(x, y, z, lebar, dalam, warna)");
        DrawPlane((Vector3){num(a[0],name),num(a[1],name),num(a[2],name)},(Vector2){num(a[3],name),num(a[4],name)},colorOf(a[5],name));
        return valStr("");
    }
    // cube / cubeWire
    argc(name,n,7,"cube(x, y, z, lebar, tinggi, dalam, warna)");
    Vector3 p={num(a[0],name),num(a[1],name),num(a[2],name)};
    float w=num(a[3],name), h=num(a[4],name), d=num(a[5],name);
    Color c=colorOf(a[6],name);
    if(!strcmp(name,"cube")) shadedCube(p,w,h,d,c);
    else DrawCubeWires(p,w,h,d,c);
    return valStr("");
}

void nrx3dInit(void){
    nrx3dCallPtr=nrx3dCall;
}
