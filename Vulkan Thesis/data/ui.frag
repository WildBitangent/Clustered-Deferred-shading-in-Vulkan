#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outFrag;

layout(set=0, binding=0) uniform sampler2D fontText;

layout(location = 0) in struct
{
    vec4 color;
    vec2 uv;
} In;

void main()
{
    outFrag = In.color * texture(fontText, In.uv);
}