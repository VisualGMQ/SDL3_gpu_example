#version 450

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D mySampler;

layout(set = 3, binding = 0) uniform MyColor {
    vec4 color;
} my_color;

void main() {
    outColor = texture(mySampler, fragUV) * vec4(my_color.color);
}
