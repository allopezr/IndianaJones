#include "stdafx.h"
#include "Group3D.h"

#include "Geometry/3D/Ray3D.h"
#include "Geometry/3D/TriangleMesh.h"
#include "Geometry/3D/Intersections3D.h"
#include "Graphics/Application/Renderer.h"
#include "Graphics/Application/RenderingParameters.h"
#include "Graphics/Core/OpenGLUtilities.h"
#include "Graphics/Core/ShaderList.h"
#include "Graphics/Core/VAO.h"
#include "Utilities/ChronoUtilities.h"

/// [Public methods]

Group3D::Group3D(const mat4& modelMatrix):
	Model3D(modelMatrix, 1)		
{
}

Group3D::~Group3D()
{
	for (Model3D* object : _objects)
	{
		delete object;
	}
}

void Group3D::addComponent(Model3D* object)
{
	_objects.push_back(object);
}

Model3D::ModelComponent* Group3D::getModelComponent(unsigned id)
{
	return _globalModelComp[id];
}

bool Group3D::load(const mat4& modelMatrix)
{
	bool success = true;
	const mat4& mMatrix = modelMatrix * _modelMatrix;

	for (Model3D* model : _objects)
	{
		success &= model->load(mMatrix);
	}

	return success;
}

void Group3D::registerModelComponent(ModelComponent* modelComp)
{
	unsigned id = _globalModelComp.size();

	_globalModelComp.push_back(modelComp);
	modelComp->_id = id;
	modelComp->assignModelCompIDFaces();
}

void Group3D::registerModelComponentGroup(Group3D* group)
{
	for (Model3D* model : _objects)
	{
		model->registerModelComponentGroup(group);
	}
}

void Group3D::registerScene()
{
	for (Model3D* model : _objects)
	{
		model->registerModelComponentGroup(this);
	}
}

// --------------------------- Rendering ----------------------------------

void Group3D::drawAsLines(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix)
{
	this->setShaderUniforms(shader, shaderType, matrix);

	for (Model3D* object : _objects)
	{
		object->drawAsLines(shader, shaderType, matrix);
	}
}

void Group3D::drawAsPoints(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix)
{
	this->setShaderUniforms(shader, shaderType, matrix);

	for (Model3D* object : _objects)
	{
		object->drawAsPoints(shader, shaderType, matrix);
	}
}

void Group3D::drawAsTriangles(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix)
{
	Material* material = _modelComp[0]->_material;
	this->setShaderUniforms(shader, shaderType, matrix);

	for (Model3D* object : _objects)
	{
		if (material)
		{
			material->applyMaterial(shader);
		}

		object->drawAsTriangles(shader, shaderType, matrix);
	}
}

void Group3D::drawAsTriangles4Shadows(RenderingShader* shader, const RendEnum::RendShaderTypes shaderType, std::vector<mat4>& matrix)
{
	this->setShaderUniforms(shader, shaderType, matrix);

	for (Model3D* object : _objects)
	{
		object->drawAsTriangles4Shadows(shader, shaderType, matrix);
	}
}