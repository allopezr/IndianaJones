#version 450

#extension GL_ARB_compute_variable_group_size : enable
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_NV_shader_atomic_int64 : require
#extension GL_NV_gpu_shader5 : enable

#include <Assets/Shaders/Compute/Templates/modelStructs.glsl>

layout (local_size_variable) in;

layout (std430, binding = 0) buffer DepthBuffer		{ uint			depthBuffer[]; };
layout (std430, binding = 1) buffer Color01Buffer	{ uint64_t		colorBuffer01[]; };
layout (std430, binding = 2) buffer Color02Buffer	{ uint64_t		colorBuffer02[]; };
layout (std430, binding = 3) buffer PointBuffer		{ PointModel	points[]; };

uniform mat4	cameraMatrix;
uniform float	distanceThreshold;
uniform uint	maxClassId;
uniform uint	numPoints;
uniform uvec2	windowSize;

uniform vec2		minMaxHeight, minMaxColor;
uniform sampler2D	paletteTexture;

subroutine vec3 colorType(uint index);
subroutine uniform colorType colorUniform;

subroutine(colorType)
vec3 rgbColor(uint index)
{
	//vec2 returnIds = unpackHalf2x16(points[index].returnData);
	//float pointReturnFactor = returnIds.x / returnIds.y;

	//return vec3(pointReturnFactor) * 255.0f;
	return unpackUnorm4x8(points[index].rgb).rgb * 255.0f;
}

subroutine(colorType)
vec3 rgbNormalizedColor(uint index)
{
	return vec3((points[index].rgb - minMaxColor.x) / (minMaxColor.y - minMaxColor.x)) * 255.0f;
}

subroutine(colorType)
vec3 normalColor(uint index)
{
	return texture(paletteTexture, vec2(.5f, abs(dot(vec3(.0f, 1.0f, .0f), points[index].normal)))).rgb * 255.0f;
}

subroutine(colorType)
vec3 heightColor(uint index)
{
	return texture(paletteTexture, vec2(.5f, (points[index].point.z - minMaxHeight.x) / (minMaxHeight.y - minMaxHeight.x))).rgb * 255.0f;
}

subroutine(colorType)
vec3 classColor(uint index)
{
	return texture(paletteTexture, vec2(.5f, unpackUnorm4x8(points[index].returnClassData).z * 256.0f / maxClassId)).rgb * 255.0f;
}


void main()
{
	const uint index = gl_GlobalInvocationID.x;
	if (index >= numPoints) return;

	vec4 projectedPoint = cameraMatrix * vec4(points[index].point, 1.0f);
	projectedPoint.xyz /= projectedPoint.w;

	if (projectedPoint.w <= 0.0 || projectedPoint.x < -1.0 || projectedPoint.x > 1.0 || projectedPoint.y < -1.0 || projectedPoint.y > 1.0)
	{
		return;
	}

	ivec2 windowPosition	= ivec2((projectedPoint.xy * 0.5f + 0.5f) * windowSize);
	int pointIndex			= int(windowPosition.y * windowSize.x + windowPosition.x);
	float depth				= projectedPoint.w;
	float depthInBuffer		= uintBitsToFloat(depthBuffer[pointIndex]);
	uvec3 rgbColor			= uvec3(colorUniform(index));

	if (depth < depthInBuffer * distanceThreshold)			// Same surface
	{
		uint64_t rg = (uint64_t(rgbColor.r) << 32) | rgbColor.g;
		uint64_t ba = (uint64_t(rgbColor.b) << 32) | 1;

		atomicAdd(colorBuffer01[pointIndex], rg);
		atomicAdd(colorBuffer02[pointIndex], ba);
	}
}