#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: real production GLSL witness for stale HDR mips after SSR is disabled/preset switched.
import os
os.environ['PYOPENGL_PLATFORM'] = 'glx'
import argparse
import ctypes
import re
from pathlib import Path
import numpy as np
from PIL import Image
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--shader', type=Path, default=ROOT/'resources/shader/gfx_godray_gen.frag')
    parser.add_argument('--output', type=Path, default=Path('/tmp/opencode/godray_freshness'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    glfw = ctypes.CDLL('libglfw.so.3')
    glfw.glfwCreateWindow.restype = ctypes.c_void_p
    glfw.glfwCreateWindow.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
        ctypes.c_void_p, ctypes.c_void_p]
    glfw.glfwMakeContextCurrent.argtypes = [ctypes.c_void_p]
    glfw.glfwDestroyWindow.argtypes = [ctypes.c_void_p]
    if not glfw.glfwInit():
        raise RuntimeError('GLFW init failed; use the live NVIDIA DISPLAY')
    glfw.glfwWindowHint(0x00020004, 0)
    glfw.glfwWindowHint(0x00022002, 3)
    glfw.glfwWindowHint(0x00022003, 3)
    glfw.glfwWindowHint(0x00022008, 0x00032001)
    window = glfw.glfwCreateWindow(16, 16, b'gX godray freshness', None, None)
    if not window:
        raise RuntimeError('GLFW context failed')
    glfw.glfwMakeContextCurrent(window)
    print('GODRAY_GPU:', GL.glGetString(GL.GL_RENDERER).decode())
    vertex = '''#version 330
out vec2 fragTexCoord;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    fragTexCoord = vec2(p.x, 1.0-p.y);
    gl_Position = vec4(p*2.0-1.0, 0, 1);
}'''
    fragment = args.shader.read_text()
    program = compileProgram(compileShader(vertex, GL.GL_VERTEX_SHADER),
        compileShader(fragment, GL.GL_FRAGMENT_SHADER))
    #ai: retain the pre-bound stride as an executable high-density reference/low-density negative.
    reference_source, replaced = re.subn(r'float stepUV = [^;]+;',
        'float stepUV = dist * 4.0 / max(float(rayDensity), 1.0);', fragment)
    if replaced != 1:
        raise RuntimeError('Godray stride witness needs review after source change')
    reference = compileProgram(compileShader(vertex, GL.GL_VERTEX_SHADER),
        compileShader(reference_source, GL.GL_FRAGMENT_SHADER))
    vao = GL.glGenVertexArrays(1)
    GL.glBindVertexArray(vao)
    GL.glUseProgram(program)
    GL.glUniform1i(GL.glGetUniformLocation(program, 'sceneColor'), 0)
    for name, value in [('rayDecay', .9), ('rayWeight', .42), ('rayExposure', 1)]:
        GL.glUniform1f(GL.glGetUniformLocation(program, name), value)
    GL.glUniform1i(GL.glGetUniformLocation(program, 'rayDensity'), 64)
    GL.glUniform2f(GL.glGetUniformLocation(program, 'sunScreenPos'), .7, .8)
    failures = []
    for width, height, scale in [(640, 360, 2), (511, 317, 2), (640, 360, 4)]:
        source, target = GL.glGenTextures(2)
        framebuffer = GL.glGenFramebuffers(1)
        gw, gh = width//scale, height//scale
        GL.glActiveTexture(GL.GL_TEXTURE1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, target)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA32F, gw, gh, 0,
            GL.GL_RGBA, GL.GL_FLOAT, None)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, framebuffer)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
            GL.GL_TEXTURE_2D, target, 0)
        if GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER) != GL.GL_FRAMEBUFFER_COMPLETE:
            raise RuntimeError('Incomplete fixture FBO')
        GL.glViewport(0, 0, gw, gh)
        GL.glActiveTexture(GL.GL_TEXTURE0)
        GL.glBindTexture(GL.GL_TEXTURE_2D, source)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR_MIPMAP_LINEAR)
        GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
        for param in (GL.GL_TEXTURE_WRAP_S, GL.GL_TEXTURE_WRAP_T):
            GL.glTexParameteri(GL.GL_TEXTURE_2D, param, GL.GL_CLAMP_TO_EDGE)
        previous = np.zeros((height, width, 3), dtype=np.float32)
        previous[height//4:3*height//4, width//8:3*width//8] = [1, .8, .5]
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGB32F, width, height, 0,
            GL.GL_RGB, GL.GL_FLOAT, previous)
        GL.glGenerateMipmap(GL.GL_TEXTURE_2D)
        current = np.zeros_like(previous)

        def update(pixels):
            GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, width, height,
                GL.GL_RGB, GL.GL_FLOAT, pixels)

        def draw(tag):
            GL.glDrawArrays(GL.GL_TRIANGLES, 0, 3)
            pixels = np.frombuffer(GL.glReadPixels(0, 0, gw, gh, GL.GL_RGBA,
                GL.GL_FLOAT), dtype=np.float32).reshape(gh, gw, 4).copy()
            Image.fromarray((np.clip(pixels[::-1,:,:3], 0, 1)*255).astype(np.uint8)).save(
                args.output/f'{width}x{height}_scale{scale}_{tag}.png')
            if not np.isfinite(pixels).all():
                failures.append('Nonfinite output')
            return pixels

        update(current)
        stale = draw('stale_black')
        GL.glGenerateMipmap(GL.GL_TEXTURE_2D)
        fresh = draw('fresh_black')
        error = float(np.max(np.abs(stale-fresh)))
        print(f'GODRAY_FRESHNESS: {width}x{height} scale={scale} stale-vs-fresh={error:.8f} tolerance=0.00001')
        if error>1e-5 or np.max(np.abs(stale))>1e-5:
            failures.append('Previous scene survives in black current frame')
        current[height//2:3*height//4, 5*width//8:7*width//8] = [.5, .8, 1]
        update(current)
        moved = draw('moved_stale')
        GL.glGenerateMipmap(GL.GL_TEXTURE_2D)
        moved_fresh = draw('moved_fresh')
        error = float(np.max(np.abs(moved-moved_fresh)))
        print(f'GODRAY_SOURCE_MOVE: max-error={error:.8f} positive-pixels={(moved[:,:,0]>.01).sum()}')
        if error>1e-5 or (moved[:,:,0]>.01).sum()<50:
            failures.append('Current source ignored or stale mips affect moved source')
        GL.glUniform1f(GL.glGetUniformLocation(program, 'rayExposure'), 0)
        if np.max(np.abs(draw('exposure_zero')))>1e-6:
            failures.append('Zero exposure produces light')
        GL.glUniform1f(GL.glGetUniformLocation(program, 'rayExposure'), 1)
        #ai: bright screen rim is outside every interior pixel-to-sun segment.
        y, x = np.mgrid[:height, :width]
        rim = ((x+.5)/width<.06)|((x+.5)/width>.94)|((y+.5)/height<.06)|((y+.5)/height>.94)
        current[:] = 0
        current[rim] = [1, .8, .5]
        update(current)
        GL.glGenerateMipmap(GL.GL_TEXTURE_2D)
        py, px = np.mgrid[:gh, :gw]
        interior = ((px+.5)/gw>.15)&((px+.5)/gw<.85)&((py+.5)/gh>.15)&((py+.5)/gh<.85)

        def controls(prog, density):
            GL.glUseProgram(prog)
            GL.glUniform1i(GL.glGetUniformLocation(prog, 'sceneColor'), 0)
            GL.glUniform1i(GL.glGetUniformLocation(prog, 'rayDensity'), density)
            GL.glUniform2f(GL.glGetUniformLocation(prog, 'sunScreenPos'), .91, .89)
            for name, value in [('rayDecay', .9), ('rayWeight', .42), ('rayExposure', 1)]:
                GL.glUniform1f(GL.glGetUniformLocation(prog, name), value)

        controls(reference, 16)
        endpoint = draw('density_reference16')
        for density in (1, 4, 8, 12, 15, 16, 17, 32, 64, 128):
            controls(program, density)
            actual = draw(f'density{density}')
            controls(reference, density)
            original = draw(f'density{density}_original')
            expected = endpoint if density<16 else original
            error = float(np.max(np.abs(actual-expected)))
            leak = float(np.max(np.abs(actual[interior])))
            old_leak = float(np.max(np.abs(original[interior])))
            print(f'GODRAY_DENSITY: {width}x{height} scale={scale} density={density} '
                f'bounded-error={error:.8f} interior-leak={leak:.8f} old-leak={old_leak:.8f}')
            if error>1e-5 or leak>1e-5:
                failures.append(f'Density {density} overshoots sun or changes high-density response')
            if density==1 and old_leak<.01:
                failures.append('Overshoot negative fixture failed to expose screen-edge sampling')
        controls(program, 64)
        GL.glUniform2f(GL.glGetUniformLocation(program, 'sunScreenPos'), .7, .8)
        if GL.glGetError() != GL.GL_NO_ERROR:
            failures.append('GL error')
        GL.glDeleteTextures([source, target])
        GL.glDeleteFramebuffers(1, [framebuffer])
    GL.glDeleteVertexArrays(1, [vao])
    GL.glDeleteProgram(program)
    GL.glDeleteProgram(reference)
    glfw.glfwMakeContextCurrent(None)
    glfw.glfwDestroyWindow(window)
    glfw.glfwTerminate()
    if failures:
        raise RuntimeError('; '.join(failures))
    print('GODRAY_FRESHNESS: PASS — current frame independent of stale HDR mip history')
    print('GODRAY_DENSITY: PASS — four taps bounded to sun; density >=16 response preserved')


if __name__ == '__main__':
    main()
