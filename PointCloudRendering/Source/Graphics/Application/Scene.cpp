#include "stdafx.h"
#include "Scene.h"

#include "Graphics/Core/OpenGLUtilities.h"
#include "Graphics/Core/ShaderList.h"

/// [Public methods]

Scene::Scene():
	_cameraManager(std::unique_ptr<CameraManager>(new CameraManager())), _sceneGroup(nullptr),
	_nextFramebufferID(0)
{
}

Scene::~Scene()
{
	delete _sceneGroup;
}

void Scene::load()
{
	this->loadLights();
	this->loadModels();
	this->loadCameras();
}

void Scene::modifyNextFramebufferID(const GLuint fboID)
{
	_nextFramebufferID = fboID;
}

void Scene::modifySize(const uint16_t width, const uint16_t height)
{
	_cameraManager->getActiveCamera()->setRaspect(width, height);
}

void Scene::render(const mat4& mModel, RenderingParameters* rendParams)
{
	Camera* activeCamera = _cameraManager->getActiveCamera();

	this->bindDefaultFramebuffer(rendParams);
	this->drawAsPoints(mModel, rendParams);
}

// [Protected methods]

void Scene::drawAsPoints(const mat4& mModel, RenderingParameters* rendParams)
{
	Camera* activeCamera = _cameraManager->getActiveCamera(); if (!activeCamera) return;
	RenderingShader* shader = ShaderList::getInstance()->getRenderingShader(RendEnum::POINT_CLOUD_SHADER);
	std::vector<mat4> matrix(RendEnum::numMatricesTypes());

	matrix[RendEnum::MODEL_MATRIX] = mModel;
	matrix[RendEnum::VIEW_PROJ_MATRIX] = activeCamera->getViewProjMatrix();

	shader->use();
	shader->applyActiveSubroutines();

	this->drawSceneAsPoints(shader, RendEnum::POINT_CLOUD_SHADER, &matrix, rendParams);
}

void Scene::drawAsLines(const mat4& mModel, RenderingParameters* rendParams)
{
	Camera* activeCamera = _cameraManager->getActiveCamera(); if (!activeCamera) return;
	RenderingShader* shader = ShaderList::getInstance()->getRenderingShader(RendEnum::WIREFRAME_SHADER);
	std::vector<mat4> matrix(RendEnum::numMatricesTypes());

	matrix[RendEnum::MODEL_MATRIX] = mModel;
	matrix[RendEnum::VIEW_PROJ_MATRIX] = activeCamera->getViewProjMatrix();

	shader->use();
	shader->applyActiveSubroutines();

	this->drawSceneAsLines(shader, RendEnum::WIREFRAME_SHADER, &matrix, rendParams);
}

void Scene::drawAsTriangles(const mat4& mModel, RenderingParameters* rendParams)
{
	Camera* activeCamera = _cameraManager->getActiveCamera(); if (!activeCamera) return;
	this->drawAsTriangles(activeCamera, mModel, rendParams);
}

void Scene::drawAsTriangles(Camera* camera, const mat4& mModel, RenderingParameters* rendParams)
{
	RenderingShader* shader = ShaderList::getInstance()->getRenderingShader(RendEnum::TRIANGLE_MESH_SHADER);
	std::vector<mat4> matrix(RendEnum::numMatricesTypes());
	const mat4 bias = glm::translate(mat4(1.0f), vec3(0.5f)) * glm::scale(mat4(1.0f), vec3(0.5f));						// Proj: [-1, 1] => with bias: [0, 1]

	{
		matrix[RendEnum::MODEL_MATRIX] = mModel;
		matrix[RendEnum::VIEW_MATRIX] = camera->getViewMatrix();
		matrix[RendEnum::VIEW_PROJ_MATRIX] = camera->getViewProjMatrix();

		glDepthFunc(GL_LEQUAL);

		shader->use();
		shader->setUniform("materialScattering", rendParams->_materialScattering);										// Ambient lighting
	}

	{
		for (unsigned int i = 0; i < _lights.size(); ++i)																// Multipass rendering
		{
			if (i == 0)
			{
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else
			{
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			}

			if (_lights[i]->shouldCastShadows())
			{
				matrix[RendEnum::BIAS_VIEW_PROJ_MATRIX] = bias * _lights[i]->getCamera()->getViewProjMatrix();
			}

			_lights[i]->applyLight(shader, matrix[RendEnum::VIEW_MATRIX]);
			_lights[i]->applyShadowMapTexture(shader);
			shader->applyActiveSubroutines();

			this->drawSceneAsTriangles(shader, RendEnum::TRIANGLE_MESH_SHADER, &matrix, rendParams);
		}
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);					// Back to initial state
	glDepthFunc(GL_LESS);
}

void Scene::drawAsTriangles4Shadows(const mat4& mModel, RenderingParameters* rendParams)
{
	Camera* activeCamera = _cameraManager->getActiveCamera(); if (!activeCamera) return;
	RenderingShader* shader = ShaderList::getInstance()->getRenderingShader(RendEnum::SHADOWS_SHADER);
	const ivec2 canvasSize = rendParams->_viewportSize;
	std::vector<mat4> matrix(RendEnum::numMatricesTypes());

	{
		matrix[RendEnum::MODEL_MATRIX] = mModel;

		glEnable(GL_CULL_FACE);

		shader->use();
	}

	for (unsigned int i = 0; i < _lights.size(); ++i)
	{
		Light* light = _lights[i].get();

		if (light->shouldCastShadows())
		{
			ShadowMap* shadowMap = light->getShadowMap();
			const ivec2 size = shadowMap->getSize();

			{
				shadowMap->bindFBO();
				glClear(GL_DEPTH_BUFFER_BIT);
				glViewport(0, 0, size.x, size.y);
				shader->applyActiveSubroutines();
			}

			matrix[RendEnum::VIEW_PROJ_MATRIX] = light->getCamera()->getViewProjMatrix();

			_sceneGroup->drawAsTriangles4Shadows(shader, RendEnum::SHADOWS_SHADER, matrix);
		}
	}

	glViewport(0, 0, canvasSize.x, canvasSize.y);
	glDisable(GL_CULL_FACE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Scene::drawSceneAsPoints(RenderingShader* shader, RendEnum::RendShaderTypes shaderType, std::vector<mat4>* matrix, RenderingParameters* rendParams)
{
	_sceneGroup->drawAsPoints(shader, shaderType, *matrix);
}

void Scene::drawSceneAsLines(RenderingShader* shader, RendEnum::RendShaderTypes shaderType, std::vector<mat4>* matrix, RenderingParameters* rendParams)
{
	_sceneGroup->drawAsLines(shader, shaderType, *matrix);
}

void Scene::drawSceneAsTriangles(RenderingShader* shader, RendEnum::RendShaderTypes shaderType, std::vector<mat4>* matrix, RenderingParameters* rendParams)
{
	_sceneGroup->drawAsTriangles(shader, shaderType, *matrix);
}

void Scene::bindDefaultFramebuffer(RenderingParameters* rendParams)
{
	glBindFramebuffer(GL_FRAMEBUFFER, _nextFramebufferID);
	glClearColor(rendParams->_backgroundColor.x, rendParams->_backgroundColor.y, rendParams->_backgroundColor.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Scene::composeScene()
{
}

void Scene::loadCameras()
{
	ivec2 canvasSize = Window::getInstance()->getSize();
	Camera* camera = new Camera(canvasSize[0], canvasSize[1]);

	_cameraManager->insertCamera(camera);
}

void Scene::loadLights()
{
}
