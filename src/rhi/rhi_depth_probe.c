/**********************************************************************************************
*
*   [gX: src/rhi/rhi_depth_probe.c]
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/

//ai: empirical debug probe; allocations are queried live, samples use isolated matching storage.
#include "rhi_depth_probe.h"
#ifndef GX_LEAN_RELEASE
#include "rhi_gl.h"
#include "gl_state.h"
#include "rhi_state.h"
#include "../common/log.h"
#include <stdio.h>

//ai: shader failures are explicit, never successful empty measurements.
static gX_U32 gX_DepthProbe_Program(const gX_I8 *Fragment)
{
    const gX_I8 *Vertex = "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}";
    gX_U32 Shaders[2] = {glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};
    const gX_I8 *Sources[2] = {Vertex,Fragment};
    gX_I32 Success = 1;
    for (gX_U32 Index = 0; Index < 2; Index++)
    {
        glShaderSource(Shaders[Index],1,&Sources[Index],NULL);
        glCompileShader(Shaders[Index]);
        gX_I32 Status = 0;
        glGetShaderiv(Shaders[Index],GL_COMPILE_STATUS,&Status);
        if (!Status)
        {
            gX_I8 Log[1024];
            glGetShaderInfoLog(Shaders[Index],sizeof(Log),NULL,Log);
            gX_TraceLog(gX_LOG_ERROR,"DEPTH_PROBE shader: %s",Log);
            Success = 0;
        }
    }
    gX_U32 Program = glCreateProgram();
    glAttachShader(Program,Shaders[0]);
    glAttachShader(Program,Shaders[1]);
    glLinkProgram(Program);
    gX_I32 Linked = 0;
    glGetProgramiv(Program,GL_LINK_STATUS,&Linked);
    glDeleteShader(Shaders[0]);
    glDeleteShader(Shaders[1]);
    if (!Success || !Linked)
    {
        gX_I8 Log[1024];
        glGetProgramInfoLog(Program,sizeof(Log),NULL,Log);
        gX_TraceLog(gX_LOG_ERROR,"DEPTH_PROBE link: %s",Log);
        glDeleteProgram(Program);
        return 0;
    }
    return Program;
}

//ai: sample an isolated depth texture through the actual GLSL sampler path into RGBA32F.
static gX_I0 gX_DepthProbe_Sample(gX_U32 Program, gX_U32 Texture, gX_U32 Target,
    gX_U32 Output, gX_I32 Samples, gX_F32 Values[4])
{
    glBindFramebuffer(GL_FRAMEBUFFER,Output);
    glViewport(0,0,1,1);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(Program);
    glUniform1i(glGetUniformLocation(Program,"source"),0);
    gX_I32 CountLoc = glGetUniformLocation(Program,"count");
    if (CountLoc >= 0) glUniform1i(CountLoc,Samples);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(Target,Texture);
    glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,Values);
}

//ai: production textures are queried only; all writes and resolves target disposable matching storage.
gX_I0 gX_RHI_Depth_Probe(gX_U32 SceneDepth, gX_U32 MultisampleDepth)
{
    gX_GL_State State;
    gX_GL_State_Save(&State);
    gX_I32 VaoBefore = 0, TexBefore = 0, MSBefore = 0, SamplerBefore = 0, PackBefore = 0, UnpackBefore = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&VaoBefore);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&TexBefore);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&MSBefore);
    glGetIntegeri_v(GL_SAMPLER_BINDING,0,&SamplerBefore);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&PackBefore);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&UnpackBefore);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    const gX_U32 PackFields[] = {GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PACK_SWAP_BYTES};
    gX_I32 PackValues[5];
    for (gX_U32 Index = 0; Index < 5; Index++)
    {
        glGetIntegerv(PackFields[Index],&PackValues[Index]);
        glPixelStorei(PackFields[Index],Index == 0 ? 4 : 0);
    }
    const gX_U32 ExtraCaps[] = {GL_STENCIL_TEST,GL_RASTERIZER_DISCARD,GL_MULTISAMPLE,
        GL_SAMPLE_COVERAGE,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_MASK,GL_DITHER};
    gX_U8 Enabled[7];
    for (gX_U32 Index = 0; Index < 7; Index++)
    {
        Enabled[Index] = glIsEnabled(ExtraCaps[Index]);
        glDisable(ExtraCaps[Index]);
    }
    glEnable(GL_MULTISAMPLE);
    gX_F64 DepthRange[2];
    glGetDoublev(GL_DEPTH_RANGE,DepthRange);
    glDepthRange(0,1);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glBindSampler(0,0);
    gX_I32 SceneFormat = 0, SceneBits = 0, MSFormat = 0, MSBits = 0, Samples = 0;
    glBindTexture(GL_TEXTURE_2D,SceneDepth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&SceneFormat);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_DEPTH_SIZE,&SceneBits);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,MultisampleDepth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE,0,GL_TEXTURE_INTERNAL_FORMAT,&MSFormat);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE,0,GL_TEXTURE_DEPTH_SIZE,&MSBits);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE,0,GL_TEXTURE_SAMPLES,&Samples);
    printf("DEPTH_PROBE_DRIVER %s | %s | %s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
    printf("DEPTH_PROBE_FORMAT scene=%d bits=%d msaa=%d msbits=%d samples=%d\n",SceneFormat,SceneBits,MSFormat,MSBits,Samples);
    const gX_I8 *WriteSource = "#version 330 core\nuniform float value;void main(){gl_FragDepth=value;}";
    const gX_I8 *SampleSource = "#version 330 core\nuniform sampler2D source;out vec4 result;void main(){float d=texelFetch(source,ivec2(4),0).r;result=vec4(d);}";
    const gX_I8 *MSSource = "#version 330 core\nuniform sampler2DMS source;uniform int count;out vec4 result;void main(){float a=texelFetch(source,ivec2(4),0).r;float lo=a,hi=a,total=0.;for(int i=0;i<count;i++){float d=texelFetch(source,ivec2(4),i).r;lo=min(lo,d);hi=max(hi,d);total+=d;}result=vec4(a,lo,hi,total/float(count));}";
    gX_U32 Programs[3] = {gX_DepthProbe_Program(WriteSource),gX_DepthProbe_Program(SampleSource),gX_DepthProbe_Program(MSSource)};
    gX_U32 Textures[3] = {0}, Fbos[3] = {0}, Vao = 0;
    glGenTextures(3,Textures);
    glGenFramebuffers(3,Fbos);
    glGenVertexArrays(1,&Vao);
    glBindVertexArray(Vao);
    gX_I1 Complete = Programs[0] && Programs[1] && Programs[2] && SceneBits > 0 && MSBits > 0 && Samples > 0;
    for (gX_U32 Index = 0; Index < 3; Index++)
    {
        gX_U32 Target = Index == 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        glBindTexture(Target,Textures[Index]);
        if (Index == 1) glTexImage2DMultisample(Target,Samples,MSFormat,8,8,GL_TRUE);
        else
        {
            glTexImage2D(Target,0,Index == 0 ? SceneFormat : GL_RGBA32F,Index == 0 ? 8 : 1,
                Index == 0 ? 8 : 1,0,Index == 0 ? GL_DEPTH_COMPONENT : GL_RGBA,
                Index == 0 ? GL_UNSIGNED_INT : GL_FLOAT,NULL);
            glTexParameteri(Target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(Target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        glBindFramebuffer(GL_FRAMEBUFFER,Fbos[Index]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,Index == 2 ? GL_COLOR_ATTACHMENT0 : GL_DEPTH_ATTACHMENT,Target,Textures[Index],0);
        glDrawBuffer(Index == 2 ? GL_COLOR_ATTACHMENT0 : GL_NONE);
        glReadBuffer(Index == 2 ? GL_COLOR_ATTACHMENT0 : GL_NONE);
        Complete = Complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (Index < 2)
        {
            gX_I32 Bits = 0, Format = 0;
            glGetTexLevelParameteriv(Target,0,GL_TEXTURE_DEPTH_SIZE,&Bits);
            glGetTexLevelParameteriv(Target,0,GL_TEXTURE_INTERNAL_FORMAT,&Format);
            Complete = Complete && Bits == (Index == 0 ? SceneBits : MSBits)
                && Format == (Index == 0 ? SceneFormat : MSFormat);
            if (Index == 1)
            {
                gX_I32 ActualSamples = 0;
                glGetTexLevelParameteriv(Target,0,GL_TEXTURE_SAMPLES,&ActualSamples);
                Complete = Complete && ActualSamples == Samples;
            }
        }
    }
    if (Complete)
    {
        const gX_F64 Distances[] = {0.125,3.0,900.0};
        const gX_I8 *Names[] = {"near","mid","far"};
        for (gX_U32 Case = 0; Case < 3; Case++)
        {
            gX_F64 Ideal = 1000.0/999.9-100.0/(999.9*Distances[Case]);
            gX_F32 Input = (gX_F32)Ideal;
            gX_F32 Direct[4], Multi[4], Resolved[4], Readback = 0;
            for (gX_U32 Pass = 0; Pass < 2; Pass++)
            {
                glBindFramebuffer(GL_FRAMEBUFFER,Fbos[Pass]);
                glViewport(0,0,8,8);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_ALWAYS);
                glDepthMask(GL_TRUE);
                glUseProgram(Programs[0]);
                glUniform1f(glGetUniformLocation(Programs[0],"value"),Input);
                glDrawArrays(GL_TRIANGLES,0,3);
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER,Fbos[0]);
            glReadPixels(4,4,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&Readback);
            gX_DepthProbe_Sample(Programs[1],Textures[0],GL_TEXTURE_2D,Fbos[2],0,Direct);
            gX_DepthProbe_Sample(Programs[2],Textures[1],GL_TEXTURE_2D_MULTISAMPLE,Fbos[2],Samples,Multi);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,Fbos[1]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,Fbos[0]);
            glBlitFramebuffer(0,0,8,8,0,0,8,8,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
            gX_DepthProbe_Sample(Programs[1],Textures[0],GL_TEXTURE_2D,Fbos[2],0,Resolved);
            printf("DEPTH_PROBE_SAMPLE %s distance=%a ideal=%a input=%a direct=%a readback=%a msfirst=%a msmin=%a msmax=%a msmean=%a resolved=%a\n",
                Names[Case],Distances[Case],Ideal,(gX_F64)Input,(gX_F64)Direct[0],(gX_F64)Readback,
                (gX_F64)Multi[0],(gX_F64)Multi[1],(gX_F64)Multi[2],(gX_F64)Multi[3],(gX_F64)Resolved[0]);
        }
    }
    else gX_TraceLog(gX_LOG_ERROR,"DEPTH_PROBE incomplete programs/targets");
    gX_U32 Error = glGetError();
    printf("DEPTH_PROBE_RESULT complete=%d glerror=%u\n",Complete,Error);
    glDeleteFramebuffers(3,Fbos);
    glDeleteTextures(3,Textures);
    for (gX_U32 Index = 0; Index < 3; Index++) if (Programs[Index]) glDeleteProgram(Programs[Index]);
    glDeleteVertexArrays(1,&Vao);
    glBindVertexArray((gX_U32)VaoBefore);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,(gX_U32)TexBefore);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,(gX_U32)MSBefore);
    glBindSampler(0,(gX_U32)SamplerBefore);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(gX_U32)PackBefore);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(gX_U32)UnpackBefore);
    for (gX_U32 Index = 0; Index < 5; Index++) glPixelStorei(PackFields[Index],PackValues[Index]);
    glDepthRange(DepthRange[0],DepthRange[1]);
    for (gX_U32 Index = 0; Index < 7; Index++)
    {
        if (Enabled[Index]) glEnable(ExtraCaps[Index]);
        else glDisable(ExtraCaps[Index]);
    }
    gX_RHI_InvalidateCaches();
    gX_GL_State_Restore(&State);
    gX_GL_State After;
    gX_GL_State_Save(&After);
    gX_GL_State_Diff(&State,&After,"depth_probe");
    gX_I32 VaoAfter = 0, PackAfter = 0, UnpackAfter = 0, SamplerAfter = 0, TexAfter = 0, MSAfter = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&VaoAfter);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&PackAfter);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&UnpackAfter);
    glGetIntegeri_v(GL_SAMPLER_BINDING,0,&SamplerAfter);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&TexAfter);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE,&MSAfter);
    glActiveTexture((gX_U32)State.activeTexture);
    gX_I1 Restored = VaoAfter == VaoBefore && PackAfter == PackBefore && UnpackAfter == UnpackBefore
        && SamplerAfter == SamplerBefore && TexAfter == TexBefore && MSAfter == MSBefore;
    for (gX_U32 Index = 0; Index < 7; Index++) Restored = Restored && glIsEnabled(ExtraCaps[Index]) == Enabled[Index];
    for (gX_U32 Index = 0; Index < 5; Index++)
    {
        gX_I32 Value = 0;
        glGetIntegerv(PackFields[Index],&Value);
        Restored = Restored && Value == PackValues[Index];
    }
    printf("DEPTH_PROBE_RESTORE restored=%d glerror=%u\n",Restored,glGetError());
}
#endif
