#version 440

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float invert;
} ubuf;

void main() {
    vec4 color = texture(source, texCoord);

    if (ubuf.invert > 0.5) {
        color.rgb = (1.0 - color.rgb) * color.a;
    }

    fragColor = color * ubuf.qt_Opacity;
}