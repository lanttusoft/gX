#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: execute the production diffraction shader on a synthetic bright point.
#   Pin the aperture shape (4/6/8 blades), threshold rejection, chromatic
#   dispersion and the empty-source sentinel without an engine fixture.

import ctypes
import os
import sys
from pathlib import Path

os.environ.setdefault("PYOPENGL_PLATFORM", "glx")
import numpy as np
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader

ROOT = Path(__file__).resolve().parents[1]
SHADER = ROOT / "resources/shader/gfx_diffraction.frag"
WIDTH = 320
HEIGHT = 320


def main():
    if not SHADER.is_file():
        print("DIFFRACTION_SHAPE: missing shader", file=sys.stderr)
        return 1
    try:
        glfw = ctypes.CDLL("libglfw.so.3")
        glfw.glfwCreateWindow.restype = ctypes.c_void_p
        glfw.glfwCreateWindow.argtypes = [ctypes.c_int, ctypes.c_int,
            ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]
        glfw.glfwMakeContextCurrent.argtypes = [ctypes.c_void_p]
        glfw.glfwDestroyWindow.argtypes = [ctypes.c_void_p]
        if not glfw.glfwInit():
            raise RuntimeError("GLFW init failed")
        glfw.glfwWindowHint(0x00020004, 0)
        glfw.glfwWindowHint(0x00022002, 3)
        glfw.glfwWindowHint(0x00022003, 3)
        glfw.glfwWindowHint(0x00022008, 0x00032001)
        window = glfw.glfwCreateWindow(16, 16, b"gX diffraction probe",
            None, None)
        if not window:
            raise RuntimeError("GLFW context creation failed")
        glfw.glfwMakeContextCurrent(window)

        vertex = """#version 330
out vec2 fragTexCoord;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    fragTexCoord = vec2(p.x, 1.0-p.y);
    gl_Position = vec4(p*2.0-1.0, 0, 1);
}"""
        program = compileProgram(
            compileShader(vertex, GL.GL_VERTEX_SHADER),
            compileShader(SHADER.read_text(), GL.GL_FRAGMENT_SHADER))
        vao = GL.glGenVertexArrays(1)
        GL.glBindVertexArray(vao)
        GL.glUseProgram(program)

        source = GL.glGenTextures(1)
        target = GL.glGenTextures(1)
        framebuffer = GL.glGenFramebuffers(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, target)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA32F,
            WIDTH, HEIGHT, 0, GL.GL_RGBA, GL.GL_FLOAT, None)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, framebuffer)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER,
            GL.GL_COLOR_ATTACHMENT0, GL.GL_TEXTURE_2D, target, 0)
        if GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER) \
                != GL.GL_FRAMEBUFFER_COMPLETE:
            raise RuntimeError("incomplete fixture framebuffer")

        def texture(unit, pixels, internal, fmt):
            ident = GL.glGenTextures(1)
            GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
            GL.glBindTexture(GL.GL_TEXTURE_2D, ident)
            GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, internal, WIDTH, HEIGHT,
                0, fmt, GL.GL_FLOAT, pixels)
            for param in (GL.GL_TEXTURE_MIN_FILTER, GL.GL_TEXTURE_MAG_FILTER):
                GL.glTexParameteri(GL.GL_TEXTURE_2D, param, GL.GL_LINEAR)
            for param in (GL.GL_TEXTURE_WRAP_S, GL.GL_TEXTURE_WRAP_T):
                GL.glTexParameteri(GL.GL_TEXTURE_2D, param, GL.GL_CLAMP_TO_EDGE)
            return ident

        y, x = np.mgrid[:HEIGHT, :WIDTH]
        uv = np.stack(((x + 0.5) / WIDTH, (y + 0.5) / HEIGHT), axis=-1)
        point = np.linalg.norm(uv - 0.5, axis=-1) < 0.008

        def render(brightness, threshold, blades, chromatic, taps=16):
            pixels = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float32)
            pixels[point] = brightness
            source_tex = texture(0, pixels, GL.GL_RGB32F, GL.GL_RGB)
            GL.glBindTexture(GL.GL_TEXTURE_2D, target)
            GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA32F,
                WIDTH, HEIGHT, 0, GL.GL_RGBA, GL.GL_FLOAT, None)
            GL.glViewport(0, 0, WIDTH, HEIGHT)
            GL.glClearColor(0, 0, 0, 1)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT)
            GL.glActiveTexture(GL.GL_TEXTURE0)
            GL.glBindTexture(GL.GL_TEXTURE_2D, source_tex)
            GL.glUseProgram(program)
            GL.glUniform1i(GL.glGetUniformLocation(program, "sceneColor"), 0)
            GL.glUniform2f(GL.glGetUniformLocation(program, "resolution"),
                WIDTH, HEIGHT)
            for key, value in {
                "threshold": threshold, "knee": 0.1, "intensity": 3.0,
                "lengthUV": 0.18, "chromatic": chromatic,
                "rotation": 0.0, "sparkle": 0.0, "time": 0.0,
            }.items():
                GL.glUniform1f(GL.glGetUniformLocation(program, key), value)
            GL.glUniform1i(GL.glGetUniformLocation(program, "blades"), blades)
            GL.glUniform1i(GL.glGetUniformLocation(program, "taps"), taps)
            GL.glDrawArrays(GL.GL_TRIANGLES, 0, 3)
            data = GL.glReadPixels(0, 0, WIDTH, HEIGHT,
                GL.GL_RGBA, GL.GL_FLOAT)
            image = np.frombuffer(data, dtype=np.float32).reshape(
                HEIGHT, WIDTH, 4)[::-1, :, :3].copy()
            GL.glDeleteTextures([source_tex])
            return image

        dx = x - WIDTH / 2
        dy = y - HEIGHT / 2
        radius = np.hypot(dx, dy)
        angle = (np.degrees(np.arctan2(dy, dx)) + 360.0) % 360.0

        def ray_mean(image, angle_deg, half=2.0):
            mask = (np.abs(((angle - angle_deg + 180.0) % 360.0) - 180.0)
                < half) & (radius > 40) & (radius < 120)
            return float(image[mask].mean())

        def check_star(blades, brightness, threshold, chromatic, label):
            image = render(brightness, threshold, blades, chromatic)
            step = 360.0 / float(blades)
            peaks = [ray_mean(image, k * step) for k in range(blades)]
            troughs = [ray_mean(image, (k + 0.5) * step)
                for k in range(blades)]
            peak = float(np.mean(peaks))
            trough = float(np.mean(troughs))
            if peak <= 0.0 or peak < trough * 3.0:
                raise RuntimeError(
                    f"{label}: no {blades}-ray star (peak {peak:.4f}, "
                    f"trough {trough:.4f})")
            if not np.isfinite(image).all() or image.max() > 10.0:
                raise RuntimeError(f"{label}: invalid output values")
            return image

        image6 = check_star(6, 8.0, 1.0, 0.0, "six")
        image8 = check_star(8, 8.0, 1.0, 0.0, "eight")
        if np.array_equal(image6, image8):
            raise RuntimeError("blade count did not change the star")

        hidden = render(8.0, 999.0, 6, 0.0)
        if float(hidden.max()) > 1e-4:
            raise RuntimeError("threshold above source still emitted spikes")
        midtone = render(0.5, 1.0, 6, 0.0)
        if float(midtone.max()) > 1e-4:
            raise RuntimeError("midtone source emitted spikes")

        chromatic = render(8.0, 1.0, 6, 0.5)
        plain = render(8.0, 1.0, 6, 0.0)
        if float(np.abs(chromatic - plain).mean()) < 1e-5:
            raise RuntimeError("chromatic dispersion had no effect")
        #opencode: the R/G/B samples use different ray lengths; the channel
        #   energy centroid must separate. Use the 0-degree ray.
        ray = (np.abs(angle) < 2.0) & (radius > 35) & (radius < 130)
        centroids = []
        for channel in range(3):
            energy = chromatic[:, :, channel][ray]
            weight = np.maximum(energy, 0.0)
            centroids.append(float(np.sum(radius[ray] * weight)
                / max(np.sum(weight), 1e-6)))
        if not (centroids[0] > centroids[1] > centroids[2]):
            raise RuntimeError(
                f"chromatic centroid order {centroids}")

        print("DIFFRACTION_SHAPE: PASS six/eight-ray stars, threshold "
              "rejection, chromatic dispersion")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"DIFFRACTION_SHAPE: FAIL — {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
