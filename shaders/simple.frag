#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 in_color;

layout(location = 0) out vec4 out_fragColor;

void main()
{
	out_fragColor = vec4(in_color, 1.0);
}
