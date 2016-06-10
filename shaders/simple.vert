#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	out_color = in_color;
	gl_Position = vec4(in_position, 1.0);
}
