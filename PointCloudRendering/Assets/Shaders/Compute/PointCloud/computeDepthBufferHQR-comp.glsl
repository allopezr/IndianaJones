#version 450

#extension GL_NV_gpu_shader5 : enable
#extension GL_ARB_compute_variable_group_size: enable
#extension GL_NV_shader_thread_group : require
#extension GL_NV_shader_thread_shuffle : require
#extension GL_ARB_shader_group_vote : require
#extension GL_ARB_shader_ballot : require

#include <Assets/Shaders/Compute/Templates/modelStructs.glsl>

layout (local_size_variable) in;

layout (std430, binding = 0) buffer DepthBuffer { uint			depthBuffer[]; };
layout (std430, binding = 1) buffer PointBuffer { PointModel	points[]; };
layout (std430, binding = 2) buffer VisibilityBuffer { uint8_t	visibility[]; };

uniform uint	calculatedVisibility;
uniform mat4	cameraMatrix;
uniform ivec2	classRange;
uniform float	maxReturns;
uniform uint	numPoints;
uniform float	returnFactor;
uniform uvec2	windowSize;

subroutine bool visibilityType(uint index);
subroutine uniform visibilityType visibilityUniform;

subroutine(visibilityType)
bool visibilityCheck(uint index)
{
	return calculatedVisibility == 1 && visibility[index] == uint8_t(0);
}

subroutine(visibilityType)
bool noVisibilityCheck(uint index)
{
	return false;
}


void main()
{
	const uint index = gl_GlobalInvocationID.x;
	if (index >= numPoints) return;

	// Projection: 3D to 2D
	vec4 projectedPoint	= cameraMatrix * vec4(points[index].point, 1.0f);
	projectedPoint.xyz /= projectedPoint.w;

	vec4 returnClassId = unpackUnorm4x8(points[index].returnClassData);
	float pointReturnFactor = returnClassId.x / returnClassId.y;

	if (projectedPoint.w <= 0.0 || projectedPoint.x < -1.0 || projectedPoint.x > 1.0 || projectedPoint.y < -1.0 || projectedPoint.y > 1.0 
		|| pointReturnFactor < returnFactor || returnClassId.z * 256.0f < classRange.x || returnClassId.z * 256.0f > classRange.y || visibilityUniform(index))
	{
		return;
	}

	ivec2 windowPosition	= ivec2((projectedPoint.xy * 0.5f + 0.5f) * windowSize);
	int pointIndex			= int(windowPosition.y * windowSize.x + windowPosition.x);
	uint depth				= floatBitsToUint(projectedPoint.w);							// Another way: multiply distance by 10^x. It is more precise when x is larger
	uint minDepth			= depth;

	int headID				= shuffleNV(pointIndex, 0, 32);
	uint haveLeaderID		= ballotThreadNV(headID == pointIndex);

	if (haveLeaderID == 0xffffffff) 
	{
		minDepth = min(minDepth, shuffleXorNV(minDepth, 16, 32));
		minDepth = min(minDepth, shuffleXorNV(minDepth, 8, 32));
		minDepth = min(minDepth, shuffleXorNV(minDepth, 4, 32));
		minDepth = min(minDepth, shuffleXorNV(minDepth, 2, 32));
		minDepth = min(minDepth, shuffleXorNV(minDepth, 1, 32));
	}

	if (minDepth == depth)
	{
		uint oldDepth = depthBuffer[pointIndex];

		if (oldDepth > depth)
			atomicMin(depthBuffer[pointIndex], depth);
	}
}