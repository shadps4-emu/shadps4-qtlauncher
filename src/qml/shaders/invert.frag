// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float invert;
} ubuf;

void main() {
    vec4 color = texture(source, qt_TexCoord0);

    if (ubuf.invert > 0.5) {
        color.rgb = (1.0 - color.rgb) * color.a;
    }

    fragColor = color * ubuf.qt_Opacity;
}