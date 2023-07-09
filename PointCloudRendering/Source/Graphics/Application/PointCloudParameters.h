#pragma once

/**
*	@file PointCloudParameters.h
*	@authors Alfonso López Ruiz (alr00048@red.ujaen.es)
*	@date 30/06/2021
*/

/**
*	@brief 
*/
struct PointCloudParameters
{
public:
	inline static bool		_buildDTM = true;					//!<
	inline static bool		_computeNormal = false;				//!<
	inline static float		_distanceThreshold = 1.01f;			//!<
	inline static bool		_enableHQR = true;					//!<
	inline static GLint		_knn = 8;							//!<
	inline static ivec2		_numGridSubdivisions = ivec2(100);	//!<
	inline static bool		_sortPointCloud = false;				//!<
	inline static bool		_reducePointCloud = false;			//!<
	inline static GLuint	_reduceIterations = 1;				//!<
};