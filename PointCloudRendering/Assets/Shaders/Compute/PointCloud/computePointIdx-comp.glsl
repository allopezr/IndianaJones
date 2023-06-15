#version 450

#extension GL_ARB_compute_variable_group_size: enable
#extension GL_ARB_gpu_shader_int64: require
#extension GL_NV_shader_atomic_int64: require
#extension GL_NV_shader_subgroup_partitioned: require

#include <Assets/Shaders/Compute/Templates/modelStructs.glsl>

layout (local_size_variable) in;

layout (std430, binding = 0) buffer DepthBuffer { uint64_t		depthBuffer[]; };
layout (std430, binding = 1) buffer PointBuffer { PointModel	points[]; };

uniform vec3	cellSize;
uniform vec3	minimumPoint;
uniform uint	numPoints;
uniform uint	shift;
uniform uvec2	windowSize;


void main()
{
	const uint index = gl_GlobalInvocationID.x;
	if (index >= numPoints) return;

	// Projection: 3D to 2D
	uvec3 gridCell = uvec3((points[index].point - minimumPoint) / vec3(cellSize.x, cellSize.y, 1));
	uint gridCellIdx = gridCell.y * windowSize.x + gridCell.x;

	if (gridCell.x < 0 || gridCell.y < 0 || gridCell.x >= windowSize.x || gridCell.y >= windowSize.y)
	{
		return;
	}

	uint distanceInt				= floatBitsToUint(points[index].point.z - minimumPoint.z);						// Another way: multiply distance by 10^x. It is more precise when x is larger
	const uint64_t depthDescription = (index + shift) | (uint64_t(distanceInt) << 32);				// Distance to most significant bits. w saves the point index (mainly for multiple batch methodology)
	const uint64_t currentDepth		= depthBuffer[gridCellIdx];

	uvec4 subgroup	= subgroupPartitionNV(gridCellIdx);
	uint minDepth	= subgroupPartitionedMinNV(distanceInt, subgroup);

	if (minDepth == distanceInt)
		atomicMin(depthBuffer[gridCellIdx], depthDescription);										// AtomicMin: inf vs distance + index for the atomicMin call in this index
}