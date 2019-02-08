#version 450

#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(push_constant) uniform PushConstant
{
    vec2 scale;
    vec2 translate;
} pc;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout(location = 0) out struct
{
    vec4 color;
    vec2 uv;
} Out;

void main()
{
    Out.color = color;
    Out.uv = uv;
    gl_Position = vec4(pos * pc.scale + pc.translate, 0, 1);
}