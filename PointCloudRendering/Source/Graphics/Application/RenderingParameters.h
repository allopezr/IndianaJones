#pragma once

#include "Graphics/Application/GraphicsAppEnumerations.h"

/**
*	@file RenderingParameters.h
*	@authors Alfonso López Ruiz (alr00048@red.ujaen.es)
*	@date 07/20/2019
*/

/**
*	@brief Wraps the rendering conditions of the application.
*/
struct RenderingParameters
{
public:
	enum PointCloudRendering
	{
		RGB, NORMAL, HEIGHT, CLASS
	};

public:
	// Application
	vec3							_backgroundColor;						//!< Clear color
	ivec2							_viewportSize;							//!< Viewport size (!= window)

	// Lighting
	float							_materialScattering;					//!< Ambient light substitute
	float							_occlusionMinIntensity;					//!< Mininum value for occlusion factor (max is 1 => no occlusion)

	// Screenshot
	float							_antialiasingMultiplier;				//!< 
	char							_screenshotFilenameBuffer[32];			//!< Location of screenshot
	float							_screenshotMultiplier;					//!< Multiplier of current size of GLFW window

	// Rendering type
	int								_visualizationMode;						//!< Only triangle mesh is defined here
	
	// Point cloud
	ivec2							_classRange;							//!<
	CSF								_csf;									//!<
	bool							_filterByGround;						//!<
	bool							_filterByHeight;						//!<
	bool							_normalizedColor;						//!<
	float							_returnFactor;							//!<
	float							_scenePointSize;						//!<
	vec3							_scenePointCloudColor;					//!< Color of point cloud which shows all the vertices

	// Triangle mesh
	bool							_ambientOcclusion;						//!< Boolean value to enable/disable occlusion
	bool							_renderSemanticConcept;					//!< Boolean value to indicate if rendering semantic concepts is needed

	// What to see		
	bool							_updateCamera;							//!< Updates camera accordingly to scene AABB

public:
	/**
	*	@brief Default constructor.
	*/
	RenderingParameters() :
		_viewportSize(1.0f, 1.0f),

		_backgroundColor(0.4f, 0.4f, 0.4f),

		_materialScattering(1.0f),

		_antialiasingMultiplier(3),
		_screenshotFilenameBuffer("Screenshot.png"),
		_screenshotMultiplier(1.0f),

		_visualizationMode(0),

		_classRange(0, 256),
		_filterByGround(false),
		_filterByHeight(false),
		_normalizedColor(true),
		_scenePointSize(2.0f),
		_scenePointCloudColor(1.0f, .0f, .0f),

		_ambientOcclusion(true),

		_updateCamera(true)
	{
		_csf.params.class_threshold = 0.5f;
		_csf.params.bSloopSmooth = true;
		_csf.params.cloth_resolution = 0.5f;
		_csf.params.rigidness = 3;
		_csf.params.interations = 500;
		_csf.params.time_step = .65f;
	}
};

