#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: execute the production fragment shader on synthetic textures, without author assets.
import os
os.environ['PYOPENGL_PLATFORM'] = 'glx'
import argparse
import ctypes
from pathlib import Path
import numpy as np
from PIL import Image
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--shader', type=Path, default=ROOT/'resources/shader/gfx_lensflare.frag')
    parser.add_argument('--output', type=Path, default=Path('/tmp/opencode/lensflare_shape'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    glfw = ctypes.CDLL('libglfw.so.3')
    glfw.glfwCreateWindow.restype = ctypes.c_void_p
    glfw.glfwCreateWindow.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
        ctypes.c_void_p, ctypes.c_void_p]
    glfw.glfwMakeContextCurrent.argtypes = [ctypes.c_void_p]
    glfw.glfwDestroyWindow.argtypes = [ctypes.c_void_p]
    if not glfw.glfwInit():
        raise RuntimeError('GLFW init failed; run with the live NVIDIA DISPLAY')
    glfw.glfwWindowHint(0x00020004, 0)
    glfw.glfwWindowHint(0x00022002, 3)
    glfw.glfwWindowHint(0x00022003, 3)
    glfw.glfwWindowHint(0x00022008, 0x00032001)
    window = glfw.glfwCreateWindow(16, 16, b'gX lensflare shader probe', None, None)
    if not window:
        raise RuntimeError('GLFW context creation failed')
    glfw.glfwMakeContextCurrent(window)
    print('LENSFLARE_GPU:', GL.glGetString(GL.GL_RENDERER).decode())
    vertex = '''#version 330
out vec2 fragTexCoord;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    fragTexCoord = vec2(p.x, 1.0-p.y);
    gl_Position = vec4(p*2.0-1.0, 0, 1);
}'''
    program = compileProgram(compileShader(vertex, GL.GL_VERTEX_SHADER),
        compileShader(args.shader.read_text(), GL.GL_FRAGMENT_SHADER))
    vao = GL.glGenVertexArrays(1)
    GL.glBindVertexArray(vao)
    GL.glUseProgram(program)
    GL.glUniform1i(GL.glGetUniformLocation(program, 'sceneColor'), 0)
    GL.glUniform1i(GL.glGetUniformLocation(program, 'sceneDepth'), 1)
    GL.glUniform1f(GL.glGetUniformLocation(program, 'dispersal'), 1)
    failures = []
    textures = []
    framebuffer = GL.glGenFramebuffers(1)

    def texture(unit, pixels, internal, format_):
        ident = GL.glGenTextures(1)
        textures.append(ident)
        GL.glActiveTexture(GL.GL_TEXTURE0+unit)
        GL.glBindTexture(GL.GL_TEXTURE_2D, ident)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, internal, pixels.shape[1],
            pixels.shape[0], 0, format_, GL.GL_FLOAT, pixels)
        for param in (GL.GL_TEXTURE_MIN_FILTER, GL.GL_TEXTURE_MAG_FILTER):
            GL.glTexParameteri(GL.GL_TEXTURE_2D, param, GL.GL_NEAREST)
        for param in (GL.GL_TEXTURE_WRAP_S, GL.GL_TEXTURE_WRAP_T):
            GL.glTexParameteri(GL.GL_TEXTURE_2D, param, GL.GL_CLAMP_TO_EDGE)
        return ident

    fixtures = [(640, 360, (.72, .68), 1), (360, 640, (.72, .68), 1),
        (511, 317, (.72, .68), 1), (640, 360, (.5, .5), 1),
        (640, 360, (.25, .6), 1), (640, 360, (.72, .68), 0),
        (640, 360, (.72, .68), 2)]
    for fixture, (width, height, sun_position, dispersal) in enumerate(fixtures):
        y, x = np.mgrid[:height, :width]
        uv = np.stack(((x+.5)/width, (y+.5)/height), axis=-1)
        aspect = width/height
        sun = np.array(sun_position)
        delta = (uv-sun)*[aspect, 1]
        source = np.zeros((height, width, 3), dtype=np.float32)
        source[np.linalg.norm(delta, axis=-1)<.03] = [1, .8, .5]
        #ai: asymmetric unrelated scene colors expose the old angular sampling rays.
        source[(uv[:,:,0]<.3)&(uv[:,:,1]>.75)] = [.4, 1, .2]
        source[(uv[:,:,0]>.8)&(uv[:,:,1]<.3)] = [.2, .4, 1]
        texture(0, source, GL.GL_RGB32F, GL.GL_RGB)
        depth = texture(1, np.ones((height, width), dtype=np.float32), GL.GL_R32F, GL.GL_RED)
        target = texture(2, np.zeros((height, width, 4), dtype=np.float32), GL.GL_RGBA32F, GL.GL_RGBA)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, framebuffer)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
            GL.GL_TEXTURE_2D, target, 0)
        if GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER) != GL.GL_FRAMEBUFFER_COMPLETE:
            raise RuntimeError('Incomplete fixture FBO')
        GL.glViewport(0, 0, width, height)
        GL.glUniform2f(GL.glGetUniformLocation(program, 'resolution'), width, height)
        GL.glUniform2f(GL.glGetUniformLocation(program, 'sunScreenPos'), *sun)
        GL.glUniform1f(GL.glGetUniformLocation(program, 'dispersal'), dispersal)

        def draw(intensity):
            GL.glUniform1f(GL.glGetUniformLocation(program, 'intensity'), intensity)
            GL.glDrawArrays(GL.GL_TRIANGLES, 0, 3)
            return np.frombuffer(GL.glReadPixels(0, 0, width, height,
                GL.GL_RGBA, GL.GL_FLOAT), dtype=np.float32).reshape(height, width, 4).copy()[:,:,:3]

        image = draw(1)
        Image.fromarray((np.clip(image[::-1], 0, 1)*255).astype(np.uint8)).save(
            args.output/f'{fixture}_{width}x{height}.png')
        centers = [sun] + [sun+(.5-sun)*(i+1)*.3*dispersal for i in range(5)]
        print(f'LENSFLARE_FIXTURE: {fixture} sun={sun_position} dispersal={dispersal}')
        #ai: finite float64 formula oracle also rejects wrong axis, inverted UVs and lost ghosts.
        expected = np.exp(-np.sum(delta*delta, axis=-1)*200)[:,:,None]*np.array([1, .9, .7])*.5
        expected += np.exp(-delta[:,:,0]**2*40-delta[:,:,1]**2*20000)[:,:,None]*np.array([.9, .8, .6])*.3
        luminance = np.dot([1, .8, .5], [.2126, .7152, .0722])
        for i, center in enumerate(centers[1:]):
            offset = (uv-center)*[aspect, 1]
            envelope = np.exp(-np.sum(offset*offset, axis=-1)/(.025+i*.008)**2)
            expected += envelope[:,:,None]*np.array([1-i*.1, .8-i*.05, .6+i*.1])*(luminance-.1)*.5*(1-i*.12)
        error = float(np.max(np.abs(image-expected)))
        print(f'LENSFLARE_ORACLE: {width}x{height} max-channel-error={error:.8f} tolerance=0.0002')
        if error>.0002:
            failures.append(f'{width}x{height}: finite formula/UV/axis witness mismatch')
        allowed = abs(uv[:,:,1]-sun[1])<.04
        for center in centers:
            allowed |= np.linalg.norm((uv-center)*[aspect, 1], axis=-1)<.16
        bright = image.max(axis=-1)>.01
        leak = int((bright&~allowed).sum())/max(int(bright.sum()), 1)
        print(f'LENSFLARE_SHAPE: {width}x{height} bright={bright.sum()} off-axis-fraction={leak:.6f}')
        if not np.isfinite(image).all() or bright.sum()<100 or leak>.01:
            failures.append(f'{width}x{height}: off-axis rays/nonfinite/missing flare')
        if np.max(np.abs(draw(.5)-image*.5))>1e-5 or np.max(np.abs(draw(0)))>1e-6:
            failures.append('Intensity is not linear/zero')
        GL.glActiveTexture(GL.GL_TEXTURE1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, depth)
        occluded = np.ones((height, width), dtype=np.float32)
        occluded[np.linalg.norm(delta, axis=-1)<.03] = .5
        GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, width, height,
            GL.GL_RED, GL.GL_FLOAT, occluded)
        if np.max(np.abs(draw(1)))>1e-6:
            failures.append('Occluded source still produces flare')
        if GL.glGetError() != GL.GL_NO_ERROR:
            failures.append('GL error')
    GL.glDeleteTextures(textures)
    GL.glDeleteFramebuffers(1, [framebuffer])
    GL.glDeleteVertexArrays(1, [vao])
    GL.glDeleteProgram(program)
    glfw.glfwMakeContextCurrent(None)
    glfw.glfwDestroyWindow(window)
    glfw.glfwTerminate()
    if failures:
        raise RuntimeError('; '.join(failures))
    print('LENSFLARE_SHAPE: PASS — bounded fixed-axis ghosts/streak, aspect, intensity and occlusion')


if __name__ == '__main__':
    main()
