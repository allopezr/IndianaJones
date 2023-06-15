#pragma once

#include "Graphics/Core/Model3D.h"
#include "tinyply/tinyply.h"

/**
*	@file Group3D.h
*	@authors Alfonso López Ruiz (alr00048@red.ujaen.es)
*	@date 07/09/2019
*/

#define BVH_NODE_INDICES 36					// 12 LINES x 2 VERTICES + 12 (RESTART_PRIMITIVE_INDEX)

/**
*	@brief Wrapper for several 3d models which inherites from Model3D.
*/
class Group3D: public Model3D
{
public:
	struct VolatileGPUData;
	struct VolatileGroupData;
	struct StaticGPUData;
		
protected:
	std::vector<ModelComponent*>		_globalModelComp;				//!< Wraps every model component from the group
	std::vector<Model3D*>				_objects;						//!< Elements which take part of the group

public:
	/**
	*	@brief Constructor.
	*	@param modelMatrix Transformation to be applied for the whole group.
	*/
	Group3D(const mat4& modelMatrix = mat4(1.0f));

	/**
	*	@brief Destructor.
	*/
	virtual ~Group3D();

	/**
	*	@brief Adds a new component in the group array.
	*/
	void addComponent(Model3D* object);

	/**
	*	@brief Loads all those components who belong to this group, applying the model matrix linked to such group.
	*/
	virtual bool load(const mat4& modelMatrix = mat4(1.0f));

	/**
	*	@brief Assigns an id for a model component and registers it into an array.
	*/
	void registerModelComponent(ModelComponent* modelComp);

	/**
	*	@brief Extension from registerScene for inner nodes which are also groups.
	*/
	virtual void registerModelComponentGroup(Group3D* group);

	/**
	*	@brief Iterates through the group content to assign an id for each model component.
	*/
	void registerScene();

	// ------------------------- Getters -------------------------------

	/**
	*	@return Model component which is identifier by id param.
	*/
	ModelComponent* getModelComponent(unsigned id);

	/**
	*	@return Pointer of vector with all registered model components.
	*/
	std::vector<ModelComponent*>* getRegisteredModelComponents() { return &_globalModelComp; }

	// ---------------------------- Rendering ---------------------------------
	
	/**
	*	@brief Renders the model as a set of lines.
	*	@param shader Shader which will draw the model.
	*	@param shaderType Index of shader at the list, so we can identify what type of uniform variables must be declared.
	*	@param matrix Vector of matrices which can be applied in the rendering process.
	*/
	virtual void drawAsLines(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix);

	/**
	*	@brief Renders the model as a set of points.
	*	@param shader Shader which will draw the model.
	*	@param shaderType Index of shader at the list, so we can identify what type of uniform variables must be declared.
	*	@param matrix Vector of matrices which can be applied in the rendering process.
	*/
	virtual void drawAsPoints(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix);

	/**
	*	@brief Renders the model as a set of triangles.
	*	@param shader Shader which will draw the model.
	*	@param shaderType Index of shader at the list, so we can identify what type of uniform variables must be declared.
	*	@param matrix Vector of matrices which can be applied in the rendering process.
	*/
	virtual void drawAsTriangles(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix);

	/**
	*	@brief Renders the model as a set of triangles with no texture as we only want to retrieve the depth.
	*	@param shader Shader which will draw the model.
	*	@param shaderType Index of shader at the list, so we can identify what type of uniform variables must be declared.
	*	@param matrix Vector of matrices which can be applied in the rendering process.
	*/
	virtual void drawAsTriangles4Shadows(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix);
};

