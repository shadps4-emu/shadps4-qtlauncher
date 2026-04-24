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
    vec4 c = texture(source, qt_TexCoord0);

    if (ubuf.invert > 0.5) {
        c.rgb = (vec3(1.0) - c.rgb) * c.a;
    }

    fragColor = c * ubuf.qt_Opacity;
}