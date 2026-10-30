#!/usr/bin/env python3
#ai: exact historical negatives plus guards/native witnesses for repaired routing.
import argparse
from fractions import Fraction as Q
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def require(condition,message):
    if not condition: raise RuntimeError(message)


def uv(fragment,extent):
    require(extent!=0,'zero normalization extent is outside this contract')
    return Q(fragment)/Q(extent)


def source_check(text=None,shader=None):
    if text is None: text=(ROOT/'src/renderer/postprocess.c').read_text()
    if shader is None: shader=(ROOT/'resources/shader/postprocess/blur_gauss.frag').read_text()
    for token in ['pp->texture.renderScale = 0.5f;', 'pp->state.injectCanvas = true;',
                  'gX_U32 curW = Target.texture.width;', 'gX_U32 curH = Target.texture.height;',
                  'gX_F32v2 Resolution = {(gX_F32)curW, (gX_F32)curH};',
                  'gX_RenderTexture Target = iterations % 2 == 0 ? pp->texture.ping : pp->texture.pong;',
                  'gX_RendererPostProcess_Present(*gX_RendererPostProcess_Input(pp, iterations), finalTarget);',
                  'if (Iterations == 0) return &Pp->texture.start.texture;',
                  '!HasPass && !pp->state.inject && !pp->state.feedback && !pp->state.injectCanvas',
                  'Input.id == Target.texture.id', 'pp->state.feedback = false;',
                  '.dst = {0, 0, (gX_F32)Target.texture.width, (gX_F32)Target.texture.height}',
                  '.dst = (gX_Rectangle){0.0f,0.0f,Width,Height}',
                  '(pp->state.injectCanvas || (!pp->state.inject && !pp->state.feedback))']:
        require(token in text,'postprocess source drift: '+token)
    start=text[text.index('static gX_I0 gX_RendererPostProcess_Draw_Start'):text.index('static gX_Texture *gX_RendererPostProcess_Input')]
    require(start.index('gX_Drawing_ClearBG(gX_BLACK)')<start.index('if ((pp->state.injectCanvas'),
            'start-buffer assembly clear/injection policy drift')
    draw=text[text.index('gX_I0 gX_RendererPostProcess_DrawTo'):]
    require(draw.index('if (pp->texture.start.texture.id == 0) return;')<draw.index('gX_GL_State_Push();'),
            'unloaded-input early return leaks a pushed state scope')
    require(draw.index('gX_Drawing_BeginShader(pp->shaders[i].shader)')<
            draw.index('gX_Shader_SetValue_V2(pp->shaders[i].shader, pp->shaders[i].locRes, Resolution)'),
            'per-pass resolution upload precedes shader activation')
    require('isFinal' not in draw,'authored-final-slot routing restored')
    require('gl_FragCoord.xy / res' in shader and 'vec2 px = 1.0 / res;' in shader,
            'gaussian base/texel denominator policy drift')
    shader_loader=(ROOT/'src/renderer/postprocess/shader.c').read_text()
    require('wrapMode   = gX_TEXTURE_WRAP_REPEAT' in shader_loader,'default wrap policy drift')
    policy=re.search(r'gX_Texture \*Input = gX_RendererPostProcess_Input\(pp, iterations\);.*?gX_F32v2 Resolution = .*?;',text,re.S)
    helper=re.search(r'static gX_Texture \*gX_RendererPostProcess_Input\(.*?^}',text,re.S|re.M)
    require(policy is not None and helper is not None,'native policy extraction failed')
    return helper[0],policy[0]


def native_check(output):
    helper,policy=source_check()
    output.mkdir(parents=True,exist_ok=True)
    source=output/'dimensions.c'
    source.write_text('''//ai: scratch witness embeds production dimension branches, not GL drawing.
#include <stdint.h>
#include <stdio.h>
typedef uint32_t gX_U32;
typedef uint16_t gX_U16;
typedef int32_t gX_I32;
typedef int gX_I1;
typedef float gX_F32;
typedef struct { float x,y; } gX_F32v2;
typedef struct { gX_U32 id; gX_I32 width,height; } gX_Texture;
typedef struct { gX_Texture texture; } gX_RenderTexture;
typedef struct { struct { gX_RenderTexture start,ping,pong; } texture; } gX_RendererPostProcess;
'''+helper+'''
static void probe(gX_U32 sw,gX_U32 sh,gX_U32 iterations,float fx,float fy)
{
    gX_RendererPostProcess data={{{{1,sw,sh}},{{2,sw,sh}},{{3,sw,sh}}}};
    gX_RendererPostProcess *pp=&data;
'''+policy+'''
    printf("%u %u %u %u %.9g %.9g %.9g %.9g\\n",Input->id,Target.texture.id,curW,curH,Resolution.x,Resolution.y,fx/Resolution.x,fy/Resolution.y);
}
int main(void)
{
    probe(1024,768,0,256,192);
    probe(1024,768,1,512,384);
    probe(768,576,2,576,432);
    probe(512,384,3,384.5f,288.5f);
    probe(768,576,4,192,144);
    probe(512,384,5,128,96);
    return 0;
}
''')
    binary=output/'dimensions'
    compile_run=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],capture_output=True,text=True)
    (output/'compile.log').write_text(compile_run.stdout+compile_run.stderr)
    require(compile_run.returncode==0,'native policy compile failed: '+compile_run.stderr)
    run=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
    (output/'dimensions.txt').write_text(run.stdout)
    expected=[(1,2,1024,768,1024,768,Q(1,4),Q(1,4)),
              (2,3,1024,768,1024,768,Q(1,2),Q(1,2)),
              (3,2,768,576,768,576,Q(3,4),Q(3,4)),
              (2,3,512,384,512,384,Q(769,1024),Q(577,768)),
              (3,2,768,576,768,576,Q(1,4),Q(1,4)),
              (2,3,512,384,512,384,Q(1,4),Q(1,4))]
    rows=[line.split() for line in run.stdout.splitlines()]
    require(len(rows)==len(expected),'native dimension fixture count changed')
    for row,witness in zip(rows,expected):
        require(tuple(map(Q,row[:6]))==witness[:6],'native source/target/dimension disagreement')
        require(row[0]!=row[1],'native pass aliases its source')
        require(all(abs(Q(actual)-ideal)<=Q(1,1000000) for actual,ideal in zip(row[6:],witness[6:])),
                'native float division misses the stated witness tolerance')
    return len(rows)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('/tmp/opencode/postprocess_contracts/native'))
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    count=native_check(args.output)
    report={'result':'pass','native_dimension_fixtures':count,
        'limits':'Extracted C source/target/dimension policy plus finite CPU division witnesses; not GLSL/rasterization or sampling certification. GPU routing gate supplies separate finite evidence; opaque scene transfer and nonoverflowing iteration counts remain premises.'}
    if args.report: args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('POSTPROCESS_CONTRACTS: PASS — six repaired native source/target policies; historical counterexamples retained')


if __name__=='__main__': main()
