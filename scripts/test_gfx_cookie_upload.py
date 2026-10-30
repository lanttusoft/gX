#ai: compile the actual atlas rebuild against real raylib image conversion and a bounds/byte-checking GPU upload stub.
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]


class CookieUpload(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        audit.SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="cookie_upload_", dir=audit.SCRATCH)
        cls.directory = Path(cls.temporary.name)
        source = (ROOT / "src/renderer/gfx/shader.c").read_text()
        start = source.index("gX_I0 gX_RendererGFX_CookieAtlas_Rebuild(")
        end = source.index("//opencode: delete the cookie atlas", start)
        prelude = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "RAYLIB_HEADER"
#include "TYPES_HEADER"
#include "ENUM_HEADER"
#include "RHI_ENUMS_HEADER"
#define gX_Image Image
#define gX_Rectangle Rectangle
#define GX_COOKIE_ATLAS_CELLS 8
#define GX_COOKIE_ATLAS_CELL 256
#define gX_GFX_MAX_LIGHTS 32
#define gX_GFX_MAX_LIGHTS_SSBO 128
typedef struct {int cookieEnabled;char cookiePath[256];} gX_RendererGFX_Light;
typedef struct {unsigned cookieAtlasTex;int cookieAtlasDirty;Rectangle cookieRects[128];} Common;
typedef struct {int USED;} Meta;
typedef struct {unsigned amount;void *data;Meta *meta;} Heap;
typedef struct {Common common;Heap lights;} gX_RendererGFX;
static int Mode,Logs,Loads,Unloads,Conversions,CellUploads;
static unsigned LastX,LastY,LastW,LastH;
static size_t SourceBytes;
static unsigned char *Atlas;
static const unsigned char Pixels[8]={10,20,30,7,200,150,100,250};
static void *gX_Calloc(unsigned size){return calloc(1,size);}
static void gX_Free(void *data){free(data);}
#define gX_TraceLog_GFX(...) ((void)Logs++)
static Image gX_Image_Load(const char *path)
{
    Loads++;
    if(Mode==0)
    {
        Image image=LoadImage(path);SourceBytes=(size_t)GetPixelDataSize(image.width,image.height,image.format);return image;
    }
    Image image={NULL,2,1,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    if(Mode==1 || Mode==10 || Mode==11)image.format=PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
    if(Mode==2)image.format=PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA;
    if(Mode==3)image.format=PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    if(Mode==5)image.width=257;
    if(Mode==6)image.height=257;
    if(Mode==7)image.width=0;
    if(Mode==8)image.height=-1;
    if(Mode==12)image.format=PIXELFORMAT_COMPRESSED_DXT1_RGB;
    if(Mode==13)image.format=0;
    if(Mode==9)return image;
    SourceBytes=(Mode==7 || Mode==8 || Mode==12 || Mode==13) ? 16 : (size_t)GetPixelDataSize(image.width,image.height,image.format);
    image.data=malloc(SourceBytes);assert(image.data);memset(image.data,255,SourceBytes);
    if(Mode>=1 && Mode<=4)memcpy(image.data,Pixels,SourceBytes);
    return image;
}
static void gX_Image_Format(Image *image,int format)
{
    Conversions++;
    if(Mode==10)return;
    if(Mode==11){free(image->data);image->data=NULL;return;}
    ImageFormat(image,format);SourceBytes=(size_t)GetPixelDataSize(image->width,image->height,image->format);
}
static void gX_Image_Unload(Image image){Unloads++;UnloadImage(image);}
static unsigned gX_RHI_Tex_Create2D(unsigned w,unsigned h,unsigned a,unsigned b,unsigned c,void *data,const char *name)
{(void)a;(void)b;(void)c;(void)data;(void)name;assert(w==2048 && h==2048);return 1;}
static void gX_RHI_Tex_Param(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;}
static void gX_RHI_Tex_Upload(unsigned tex,unsigned target,int level,int x,int y,unsigned w,unsigned h,unsigned format,unsigned type,const void *data)
{
    assert(tex && target==gX_RHI_TEXTURE_2D && level==0 && format==gX_RHI_RGBA && type==gX_RHI_UNSIGNED_BYTE);
    assert(x>=0 && y>=0 && (unsigned)x+w<=2048 && (unsigned)y+h<=2048);
    if(w==2048 && h==2048){memcpy(Atlas,data,2048u*2048u*4u);return;}
    assert(SourceBytes>=(size_t)w*h*4);
    CellUploads++;LastX=(unsigned)x;LastY=(unsigned)y;LastW=w;LastH=h;
    for(unsigned row=0;row<h;row++)memcpy(Atlas+(((unsigned)y+row)*2048u+(unsigned)x)*4,(const unsigned char *)data+row*w*4,w*4);
}
'''
        body = r'''
int main(int argc,char **argv)
{
    assert(argc==3);Mode=atoi(argv[1]);
    Atlas=malloc(2048u*2048u*4u);assert(Atlas);memset(Atlas,0,2048u*2048u*4u);
    gX_RendererGFX gfx={0};gX_RendererGFX_Light lights[65]={0};Meta meta[65]={0};gfx.lights.data=lights;gfx.lights.meta=meta;gfx.common.cookieAtlasDirty=1;
    unsigned index=3;
    if(Mode==14){index=63;gfx.lights.amount=65;lights[64].cookieEnabled=1;meta[64].USED=1;strcpy(lights[64].cookiePath,"ignored");gfx.common.cookieRects[64].x=-1;}
    else gfx.lights.amount=index+1;
    meta[index].USED=1;lights[index].cookieEnabled=1;snprintf(lights[index].cookiePath,sizeof(lights[index].cookiePath),"%s",argv[2]);
    if(Mode==15)meta[index].USED=0;
    if(Mode==16)lights[index].cookieEnabled=0;
    if(Mode==17)lights[index].cookiePath[0]=0;
    gX_RendererGFX_CookieAtlas_Rebuild(&gfx);
    if(Mode==0)
    {
        assert(CellUploads==1 && Conversions==1 && LastW==256 && LastH==256);
        for(unsigned i=0;i<2048u*2048u*4u;i++)assert(Atlas[i]==255);
    }
    else if(Mode>=1 && Mode<=4)
    {
        const unsigned char expected[4][8]={{10,10,10,255,20,20,20,255},{10,10,10,20,30,30,30,7},{10,20,30,255,7,200,150,255},{10,20,30,7,200,150,100,250}};
        assert(CellUploads==1 && LastX==768 && LastY==0 && LastW==2 && LastH==1);
        assert(Conversions==(Mode==4 ? 0 : 1));
        assert(!memcmp(Atlas+768*4,expected[Mode-1],8));
        assert(Atlas[(768+2)*4]==255 && Atlas[(2048+768)*4]==255);
    }
    else if(Mode==14)
    {
        assert(CellUploads==1 && Loads==1 && LastX==1792 && LastY==1792 && gfx.common.cookieRects[64].x==-1);
    }
    else
    {
        assert(CellUploads==0);
        for(unsigned i=0;i<2048u*2048u*4u;i++)assert(Atlas[i]==255);
        if(Mode>=15)assert(Loads==0 && Unloads==0 && Logs==0);
        else {assert(Loads==1 && Unloads==1 && Logs>0);assert(Conversions==((Mode==10 || Mode==11) ? 1 : 0));}
    }
    if(Mode<15)assert(Loads==Unloads);
    assert(!gfx.common.cookieAtlasDirty);free(Atlas);return 0;
}
'''
        prelude = (prelude.replace("RAYLIB_HEADER", str(ROOT / "external/raylib/src/raylib.h"))
                   .replace("TYPES_HEADER", str(ROOT / "src/common/types.h"))
                   .replace("ENUM_HEADER", str(ROOT / "src/common/enum.h"))
                   .replace("RHI_ENUMS_HEADER", str(ROOT / "src/rhi/rhi_enums.h")))
        path = cls.directory / "probe.c"
        path.write_text(prelude + source[start:end] + body)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        cls.executables = {}
        for compute in (False, True):
            executable = cls.directory / ("compute" if compute else "forward")
            command = compiler + ["-std=gnu11", "-O0", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function"]
            if os.environ.get("GX_COOKIE_TEST_SANITIZE"):
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if compute:
                command += ["-DGX_COMPUTE"]
            command += [str(path), str(ROOT / "build/raylib_build/raylib/libraylib.a"), "-lGL", "-lX11", "-lm", "-lpthread", "-ldl", "-lrt", "-o", str(executable)]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.executables[compute] = executable

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, mode, compute=False):
        result = subprocess.run([str(self.executables[compute]), str(mode), str(ROOT / "resources/texture/cookie_window.png")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_owned_native_grayscale_cookie(self):
        self.run_case(0)

    def test_grayscale_gray_alpha_rgb_rgba(self):
        for mode in range(1, 5):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_oversized_dimensions_rejected_without_resizing(self):
        for mode in (5, 6):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_missing_or_invalid_dimensions_keep_white_cell(self):
        for mode in (7, 8, 9):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_failed_conversion_never_uploads_old_or_missing_bytes(self):
        for mode in (10, 11):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_unsupported_formats_are_not_converted_or_uploaded(self):
        for mode in (12, 13):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_last_compute_cell_and_capacity_boundary(self):
        self.run_case(14, compute=True)

    def test_unused_disabled_or_empty_path_is_not_loaded(self):
        for mode in (15, 16, 17):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_compute_uses_same_format_and_rejection_contract(self):
        for mode in range(14):
            with self.subTest(mode=mode):
                self.run_case(mode, compute=True)


if __name__ == "__main__":
    unittest.main()
