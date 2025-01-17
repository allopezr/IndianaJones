#include "stdafx.h"
#include "PointCloudAggregator.h"

#include "Graphics/Application/PointCloudParameters.h"
#include "Graphics/Application/Renderer.h"
#include "Graphics/Core/OpenGLUtilities.h"
#include "Graphics/Core/ShaderList.h"
#include "Interface/Window.h"

// [Public methods]

PointCloudAggregator::PointCloudAggregator() :
	_pointCloud(nullptr), _textureID(-1), _depthBufferSSBO(-1)
{
	ShaderList* shaderList	= ShaderList::getInstance();
	Window* window			= Window::getInstance();

	_renderingParameters	= Renderer::getInstance()->getRenderingParameters();

	_addColorsHQRShader		= shaderList->getComputeShader(RendEnum::ADD_COLORS_HQR);
	_resetDepthBufferShader = shaderList->getComputeShader(RendEnum::RESET_DEPTH_BUFFER_SHADER);
	_resetDepthBufferHQRShader = shaderList->getComputeShader(RendEnum::RESET_DEPTH_BUFFER_HQR_SHADER);
	_projectionShader		= shaderList->getComputeShader(RendEnum::PROJECTION_SHADER);
	_projectionFilterShader	= shaderList->getComputeShader(RendEnum::PROJECTION_FILTER_SHADER);
	_projectionHQRShader	= shaderList->getComputeShader(RendEnum::PROJECTION_HQR_SHADER);
	_storeTexture			= shaderList->getComputeShader(RendEnum::STORE_TEXTURE_SHADER);
	_storeHQRTexture		= shaderList->getComputeShader(RendEnum::STORE_TEXTURE_HQR_SHADER);
	_supportBuffer.resize(this->getAllowedNumberOfPoints());

	_windowSize				= window->getSize();

	_color01SSBO			= ComputeShader::setWriteBuffer(uint64_t(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);
	_color02SSBO			= ComputeShader::setWriteBuffer(uint64_t(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);
	_depthBufferSSBO		= ComputeShader::setWriteBuffer(uint64_t(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);
	_rawDepthBufferSSBO		= ComputeShader::setWriteBuffer(GLuint(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);

	// Window texture
	glGenTextures(1, &_textureID);
	glBindTexture(GL_TEXTURE_2D, _textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _windowSize.x, _windowSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glGenerateMipmap(GL_TEXTURE_2D);

	// Palette
	_inferno = new Texture("Assets/Textures/Inferno.png");
}

PointCloudAggregator::~PointCloudAggregator()
{
	this->deletePointCloudBuffers();
	glDeleteBuffers(1, &_color01SSBO);
	glDeleteBuffers(1, &_color02SSBO);
	glDeleteBuffers(1, &_depthBufferSSBO);
	glDeleteBuffers(1, &_rawDepthBufferSSBO);
	glDeleteTextures(1, &_textureID);
	delete _inferno;
}

void PointCloudAggregator::changedSize(const uint16_t width, const uint16_t height)
{
	_windowSize = ivec2(width, height);
	_changedWindowSize = true;
}

void PointCloudAggregator::filterByGround(const std::vector<GLint>& groundIndices)
{
	for (GLuint ssbo : _groundSSBO)
	{
		glDeleteBuffers(1, &ssbo);
	}
	_groundSSBO.clear();

	std::vector<std::vector<uint8_t>> ground;
	unsigned maxChunkSize = _pointCloudChunkSize[0], shift, chunk;

	for (int chunkIdx = 0; chunkIdx < _pointCloudSSBO.size(); ++chunkIdx)
	{
		std::vector<uint8_t> groundLocal(_pointCloudChunkSize[chunkIdx], 0);
		ground.push_back(groundLocal);
	}

	for (int pointIdx = 0; pointIdx < groundIndices.size(); ++pointIdx)
	{
		chunk = std::floor(groundIndices[pointIdx] / maxChunkSize);
		shift = (groundIndices[pointIdx] % maxChunkSize);
		ground[chunk][shift] = uint8_t(1);
	}

	for (int chunkIdx = 0; chunkIdx < _pointCloudSSBO.size(); ++chunkIdx)
	{
		_groundSSBO.push_back(ComputeShader::setReadBuffer(ground[chunkIdx]));
	}
}

void PointCloudAggregator::filterByHeight(const uvec2& subdivisions)
{
	for (GLuint ssbo : _visibilitySSBO)
	{
		glDeleteBuffers(1, &ssbo);
	}
	_visibilitySSBO.clear();

	int chunk = 0, accumSize = 0, numCells = subdivisions.x * subdivisions.y;
	const int numGroupsGrid = ComputeShader::getNumGroups(subdivisions.x * subdivisions.y);
	AABB aabb = _pointCloud->getAABB();
	vec3 cellSize = aabb.size() / vec3(subdivisions.x, subdivisions.y, 1);
	GLuint gridSSBO = ComputeShader::setWriteBuffer(uint64_t(), subdivisions.x * subdivisions.y, GL_DYNAMIC_DRAW);
	std::vector<std::vector<uint8_t>> visibility;

	// 1. Fill buffer of 64 bits with UINT64_MAX, i.e. the null index is UINT_MAX
	_resetDepthBufferShader->bindBuffers(std::vector<GLuint> { gridSSBO });
	_resetDepthBufferShader->use();
	_resetDepthBufferShader->setUniform("windowSize", subdivisions);
	_resetDepthBufferShader->execute(numGroupsGrid, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

	for (GLuint pointsSSBO : _pointCloudSSBO)
	{
		const unsigned numPoints = _pointCloudChunkSize[chunk];
		const int numGroupsPoints = ComputeShader::getNumGroups(numPoints);

		// 2. Transform points and use atomicMin to retrieve the nearest point
		_projectionFilterShader->bindBuffers(std::vector<GLuint> { gridSSBO, pointsSSBO });
		_projectionFilterShader->use();
		_projectionFilterShader->setUniform("cellSize", cellSize);
		_projectionFilterShader->setUniform("minimumPoint", aabb.min());
		_projectionFilterShader->setUniform("numPoints", numPoints);
		_projectionFilterShader->setUniform("shift", unsigned(accumSize));
		_projectionFilterShader->setUniform("windowSize", subdivisions);
		_projectionFilterShader->execute(numGroupsPoints, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

		accumSize += _pointCloudChunkSize[chunk++];
	}

	unsigned maxChunkSize = _pointCloudChunkSize[0], visiblePoint, shift;
	uint64_t* gridData = ComputeShader::readData(gridSSBO, uint64_t());

	for (int chunkIdx = 0; chunkIdx < _pointCloudSSBO.size(); ++chunkIdx)
	{
		std::vector<uint8_t> visibilityLocal(_pointCloudChunkSize[chunkIdx], 0);
		visibility.push_back(visibilityLocal);
	}

	for (int pointIdx = 0; pointIdx < numCells; ++pointIdx)
	{
		if (gridData[pointIdx] != 0xffffffffffffffff)
		{
			visiblePoint = gridData[pointIdx] & 0xffffffff;
			chunk = std::floor(visiblePoint / maxChunkSize);
			shift = visiblePoint % maxChunkSize;
			visibility[chunk][shift] = uint8_t(1);
		}
	}

	if (PointCloudParameters::_buildDTM)
	{
		float minHeight = FLT_MAX, maxHeight = FLT_MIN;
		std::vector<GLubyte> image(numCells * 4, 0);
		std::vector<float> height;
		std::vector<PointCloud::PointModel>* points = _pointCloud->getPoints();

		for (int pointIdx = 0; pointIdx < numCells; ++pointIdx)
		{
			if (gridData[pointIdx] != 0xffffffffffffffff)
			{
				visiblePoint = gridData[pointIdx] & 0xffffffff;
				height.push_back(points->at(visiblePoint)._point.z);
				minHeight = (std::min)(minHeight, points->at(visiblePoint)._point.z);
				maxHeight = (std::max)(maxHeight, points->at(visiblePoint)._point.z);
			}
			else
			{
				height.push_back(0);
			}
		}

		for (int pointIdx = 0; pointIdx < numCells; ++pointIdx)
		{
			image[pointIdx * 4 + 0] = glm::clamp((height[pointIdx] - minHeight) / (maxHeight - minHeight), .0f, 1.0f) * 255.0f;
			image[pointIdx * 4 + 1] = image[pointIdx * 4 + 0];
			image[pointIdx * 4 + 2] = image[pointIdx * 4 + 0];
			image[pointIdx * 4 + 3] = (height[pointIdx] > glm::epsilon<float>()) ? 255 : 0;
		}

		Image* imageWrapper = new Image(image.data(), subdivisions.x, subdivisions.y, 4);
		imageWrapper->saveImage("DTM.png");
	}

	for (int chunkIdx = 0; chunkIdx < _pointCloudSSBO.size(); ++chunkIdx)
	{
		_visibilitySSBO.push_back(ComputeShader::setReadBuffer(visibility[chunkIdx]));
	}

	glDeleteBuffers(1, &gridSSBO);
}

void PointCloudAggregator::render(const mat4& projectionMatrix)
{
	if (_changedWindowSize)
	{
		this->updateWindowBuffers();
		_changedWindowSize = false;
	}

	if (PointCloudParameters::_enableHQR)
	{
		this->projectPointCloudHQR(projectionMatrix);
		this->writeColorsTextureHQR();
	}
	else
	{
		this->projectPointCloud(projectionMatrix);
		this->writeColorsTexture();
	}
}

void PointCloudAggregator::setPointCloud(PointCloud* pointCloud)
{
	_pointCloud = pointCloud;

	this->deletePointCloudBuffers();
	this->writePointCloudGPU();
}

// [Protected methods]

unsigned PointCloudAggregator::getAllowedNumberOfPoints()
{
	unsigned pointSize = sizeof(PointCloud::PointModel);
	GLint limitedMemory;
	glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &limitedMemory);

	return std::floor(limitedMemory / pointSize);
}

void PointCloudAggregator::bindTexture()
{
	glBindImageTexture(0, _textureID, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
}

GLuint PointCloudAggregator::calculateMortonCodes(const GLuint pointsSSBO, unsigned numPoints)
{
	ComputeShader* computeMortonShader = ShaderList::getInstance()->getComputeShader(RendEnum::COMPUTE_MORTON_CODES_PCL);

	const int numGroups = ComputeShader::getNumGroups(numPoints);
	const GLuint mortonCodeBuffer = ComputeShader::setWriteBuffer(unsigned(), numPoints);

	computeMortonShader->bindBuffers(std::vector<GLuint> { pointsSSBO, mortonCodeBuffer });
	computeMortonShader->use();
	computeMortonShader->setUniform("arraySize", numPoints);
	computeMortonShader->setUniform("sceneMaxBoundary", _pointCloud->getAABB().max());
	computeMortonShader->setUniform("sceneMinBoundary", _pointCloud->getAABB().min());
	computeMortonShader->execute(numGroups, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

	return mortonCodeBuffer;
}

void PointCloudAggregator::deletePointCloudBuffers()
{
	for (GLuint ssbo : _groundSSBO)
	{
		glDeleteBuffers(1, &ssbo);
	}

	for (GLuint ssbo : _pointCloudSSBO)
	{
		glDeleteBuffers(1, &ssbo);
	}

	for (GLuint ssbo : _visibilitySSBO)
	{
		glDeleteBuffers(1, &ssbo);
	}

	_pointCloudSSBO.clear();
	_pointCloudChunkSize.clear();
	_visibilitySSBO.clear();
}

void PointCloudAggregator::projectPointCloud(const mat4& projectionMatrix)
{
	unsigned chunk = 0, accumSize = 0;
	const int numGroupsImage = ComputeShader::getNumGroups(_windowSize.x * _windowSize.y);
	
	// 1. Fill buffer of 64 bits with UINT64_MAX, i.e. the null index is UINT_MAX
	_resetDepthBufferShader->bindBuffers(std::vector<GLuint> { _depthBufferSSBO });
	_resetDepthBufferShader->use();
	_resetDepthBufferShader->setUniform("windowSize", _windowSize);
	_resetDepthBufferShader->execute(numGroupsImage, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);
	
	for (GLuint pointsSSBO: _pointCloudSSBO)
	{
		const unsigned numPoints = _pointCloudChunkSize[chunk];
		const int numGroupsPoints = ComputeShader::getNumGroups(numPoints);

		// 2. Transform points and use atomicMin to retrieve the nearest point
		_projectionShader->bindBuffers(std::vector<GLuint> { _depthBufferSSBO, pointsSSBO });
		_projectionShader->use();
		_projectionShader->setUniform("cameraMatrix", projectionMatrix);
		_projectionShader->setUniform("numPoints", numPoints);
		_projectionShader->setUniform("windowSize", _windowSize);
		_projectionShader->execute(numGroupsPoints, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

		accumSize += _pointCloudChunkSize[chunk++];
	}
}

void PointCloudAggregator::projectPointCloudHQR(const mat4& projectionMatrix)
{
	std::string colorUniform = "rgbColor", visibilityUniform = "visibilityCheck", groundUniform = "groundCheck";
	unsigned chunk = 0, accumSize = 0;
	const int numGroupsImage = ComputeShader::getNumGroups(_windowSize.x * _windowSize.y);

	// 1. Fill buffer of 32 bits with UINT_MAX
	_resetDepthBufferHQRShader->bindBuffers(std::vector<GLuint> { _rawDepthBufferSSBO, _color01SSBO, _color02SSBO });
	_resetDepthBufferHQRShader->use();
	_resetDepthBufferHQRShader->setUniform("windowSize", _windowSize);
	_resetDepthBufferHQRShader->execute(numGroupsImage, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

	for (GLuint pointsSSBO : _pointCloudSSBO)
	{
		const vec2 minMaxHeight = vec2(_pointCloud->getAABB().min().z, _pointCloud->getAABB().max().z);
		const unsigned numPoints = _pointCloudChunkSize[chunk];
		const int numGroupsPoints = ComputeShader::getNumGroups(numPoints);

		// 2. Transform points and use atomicMin to retrieve the nearest point

		if (!_renderingParameters->_filterByHeight || _visibilitySSBO.empty())
			visibilityUniform = "noVisibilityCheck";

		if (!_renderingParameters->_filterByGround || _groundSSBO.empty())
			groundUniform = "noGroundCheck";

		if (chunk >= _visibilitySSBO.size())
			if (chunk >= _groundSSBO.size())
				_projectionHQRShader->bindBuffers(std::vector<GLuint> { _rawDepthBufferSSBO, pointsSSBO });
			else
				_projectionHQRShader->bindBuffers(std::vector<GLuint> { _rawDepthBufferSSBO, pointsSSBO, 0, _groundSSBO[chunk] });
		else
			if (chunk >= _groundSSBO.size())
				_projectionHQRShader->bindBuffers(std::vector<GLuint> { _rawDepthBufferSSBO, pointsSSBO, _visibilitySSBO[chunk], 0 });
			else
				_projectionHQRShader->bindBuffers(std::vector<GLuint> { _rawDepthBufferSSBO, pointsSSBO, _visibilitySSBO[chunk], _groundSSBO[chunk] });

		_projectionHQRShader->use();
		_projectionHQRShader->setUniform("calculatedGround", unsigned(!_groundSSBO.empty()));
		_projectionHQRShader->setUniform("calculatedVisibility", unsigned(!_visibilitySSBO.empty()));
		_projectionHQRShader->setUniform("cameraMatrix", projectionMatrix);
		_projectionHQRShader->setUniform("classRange", _renderingParameters->_classRange);
		//_projectionHQRShader->setUniform("maxReturns", _pointCloud->getMaxReturns());
		_projectionHQRShader->setUniform("numPoints", numPoints);
		_projectionHQRShader->setUniform("windowSize", _windowSize);
		_projectionHQRShader->setUniform("returnFactor", _renderingParameters->_returnFactor);
		_projectionHQRShader->setSubroutineUniform(GL_COMPUTE_SHADER, "visibilityUniform", visibilityUniform);
		_projectionHQRShader->setSubroutineUniform(GL_COMPUTE_SHADER, "groundUniform", groundUniform);
		_projectionHQRShader->applyActiveSubroutines();
		_projectionHQRShader->execute(numGroupsPoints, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

		// 3. Accumulate colors once the minimum depth is defined
		_addColorsHQRShader->bindBuffers(std::vector<GLuint> { _rawDepthBufferSSBO, _color01SSBO, _color02SSBO, pointsSSBO });
		_addColorsHQRShader->use();
		_addColorsHQRShader->setUniform("cameraMatrix", projectionMatrix);
		_addColorsHQRShader->setUniform("distanceThreshold", PointCloudParameters::_distanceThreshold);
		_addColorsHQRShader->setUniform("numPoints", numPoints);
		_addColorsHQRShader->setUniform("windowSize", _windowSize);

		{
			if (_renderingParameters->_visualizationMode == RenderingParameters::RGB)
			{
				if (_renderingParameters->_normalizedColor)
				{
					_addColorsHQRShader->setUniform("minMaxColor", vec2(_pointCloud->getMinColor(), _pointCloud->getMaxColor()));
					colorUniform = "rgbNormalizedColor";
				}
			}
			else if (_renderingParameters->_visualizationMode == RenderingParameters::HEIGHT)
			{
				_addColorsHQRShader->setUniform("minMaxHeight", minMaxHeight);
				_inferno->applyTexture(_addColorsHQRShader, 0, "paletteTexture");
				colorUniform = "heightColor";
			}
			else if (_renderingParameters->_visualizationMode == RenderingParameters::NORMAL)
			{
				_inferno->applyTexture(_addColorsHQRShader, 0, "paletteTexture");
				colorUniform = "normalColor";
			}
			else if (_renderingParameters->_visualizationMode == RenderingParameters::CLASS)
			{
				_addColorsHQRShader->setUniform("maxClassId", _pointCloud->getMaxClassId());
				_inferno->applyTexture(_addColorsHQRShader, 0, "paletteTexture");
				colorUniform = "classColor";
			}
		}

		_addColorsHQRShader->setSubroutineUniform(GL_COMPUTE_SHADER, "colorUniform", colorUniform);
		_addColorsHQRShader->applyActiveSubroutines();
		_addColorsHQRShader->execute(numGroupsPoints, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

		accumSize += _pointCloudChunkSize[chunk++];
	}
}

void PointCloudAggregator::reducePointChunk(GLuint& pointsSSBO, const GLuint indexSSBO, unsigned& numPoints)
{
	ComputeShader* reduceShader			= ShaderList::getInstance()->getComputeShader(RendEnum::REDUCE_POINT_BUFFER_SHADER);
	ComputeShader* iotaShader			= ShaderList::getInstance()->getComputeShader(RendEnum::IOTA_SHADER);
	ComputeShader* transferPointsShader = ShaderList::getInstance()->getComputeShader(RendEnum::TRANSFER_POINTS_SHADER);
	const int numGroups					= ComputeShader::getNumGroups(numPoints);
	const GLuint nullCount				= 0;

	GLuint countPointSSBO = ComputeShader::setReadBuffer(&numPoints, 1, GL_DYNAMIC_DRAW);
	GLuint countPointAuxSSBO = ComputeShader::setReadBuffer(&nullCount, 1, GL_DYNAMIC_DRAW);

	iotaShader->bindBuffers(std::vector<GLuint> { indexSSBO });
	iotaShader->use();
	iotaShader->setUniform("arraySize", numPoints);
	iotaShader->execute(numGroups, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

	reduceShader->use();
	reduceShader->setUniform("sceneMaxBoundary", _pointCloud->getAABB().max());
	reduceShader->setUniform("sceneMinBoundary", _pointCloud->getAABB().min());
	
	for (int iteration = 0; iteration < PointCloudParameters::_reduceIterations; ++iteration)
	{
		reduceShader->bindBuffers(std::vector<GLuint> { pointsSSBO, indexSSBO, countPointSSBO, countPointAuxSSBO });
		reduceShader->execute(numGroups, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

		std::swap(countPointSSBO, countPointAuxSSBO);
		ComputeShader::updateReadBuffer(countPointAuxSSBO, &nullCount, 1, GL_DYNAMIC_DRAW);
	}

	numPoints = *ComputeShader::readData(countPointSSBO, GLint());
	GLuint pointAuxSSBO = ComputeShader::setWriteBuffer(PointCloud::PointModel(), numPoints, GL_DYNAMIC_DRAW);

	transferPointsShader->bindBuffers(std::vector<GLuint> { pointsSSBO, pointAuxSSBO, indexSSBO });
	transferPointsShader->use();
	transferPointsShader->setUniform("arraySize", numPoints);
	transferPointsShader->execute(numGroups, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);

	glDeleteBuffers(1, &pointsSSBO);
	glDeleteBuffers(1, &countPointSSBO);
	glDeleteBuffers(1, &countPointAuxSSBO);
	pointsSSBO = pointAuxSSBO;
}

void PointCloudAggregator::sortPoints(const GLuint pointsSSBO, unsigned numPoints)
{
	const GLuint pointCodeSSBO	= this->calculateMortonCodes(pointsSSBO, numPoints);

	const GLuint indicesBufferSSBO = this->sortFacesByMortonCode(pointCodeSSBO, numPoints);
	GLuint* indices = ComputeShader::readData(indicesBufferSSBO, GLuint());
	std::vector<GLuint> bufferIndices = std::vector<GLuint>(indices, indices + numPoints);

	PointCloud::PointModel* previousPoints;
	if (PointCloudParameters::_reducePointCloud)
	{
		previousPoints = ComputeShader::readData(pointsSSBO, PointCloud::PointModel());
	}
	else
	{
		previousPoints = _pointCloud->getPoints()->data();
	}

	for (int pointIdx = 0; pointIdx < numPoints; ++pointIdx)
	{
		_supportBuffer.at(pointIdx) = previousPoints[indices[pointIdx]];
	}

	ComputeShader::updateReadBuffer(pointsSSBO, _supportBuffer.data(), numPoints, GL_STATIC_DRAW);

	glDeleteBuffers(1, &indicesBufferSSBO);
}

GLuint PointCloudAggregator::sortFacesByMortonCode(const GLuint mortonCodes, unsigned numPoints)
{
	ComputeShader* bitMaskShader = ShaderList::getInstance()->getComputeShader(RendEnum::BIT_MASK_RADIX_SORT);
	ComputeShader* reduceShader = ShaderList::getInstance()->getComputeShader(RendEnum::REDUCE_PREFIX_SCAN);
	ComputeShader* downSweepShader = ShaderList::getInstance()->getComputeShader(RendEnum::DOWN_SWEEP_PREFIX_SCAN);
	ComputeShader* resetPositionShader = ShaderList::getInstance()->getComputeShader(RendEnum::RESET_LAST_POSITION_PREFIX_SCAN);
	ComputeShader* reallocatePositionShader = ShaderList::getInstance()->getComputeShader(RendEnum::REALLOCATE_RADIX_SORT);

	const unsigned numBits	= 30;			// 10 bits per coordinate (3D)
	unsigned arraySize		= numPoints;
	unsigned currentBits	= 0;
	const int numGroups		= ComputeShader::getNumGroups(arraySize);
	const int maxGroupSize	= ComputeShader::getMaxGroupSize();
	GLuint* indices			= new GLuint[arraySize];

	// Binary tree parameters
	const unsigned startThreads = unsigned(std::ceil(arraySize / 2.0f));
	const unsigned numExec = unsigned(std::ceil(std::log2(arraySize)));
	const unsigned numGroups2Log = unsigned(ComputeShader::getNumGroups(startThreads));
	unsigned numThreads = 0, iteration;

	// Fill indices array from zero to arraySize - 1
	for (int i = 0; i < arraySize; ++i) { indices[i] = i; }

	GLuint indicesBufferID_1, indicesBufferID_2, pBitsBufferID, nBitsBufferID, positionBufferID;
	indicesBufferID_1 = ComputeShader::setWriteBuffer(GLuint(), arraySize);
	indicesBufferID_2 = ComputeShader::setReadBuffer(indices, arraySize);					// Substitutes indicesBufferID_1 for the next iteration
	pBitsBufferID = ComputeShader::setWriteBuffer(GLuint(), arraySize);
	nBitsBufferID = ComputeShader::setWriteBuffer(GLuint(), arraySize);
	positionBufferID = ComputeShader::setWriteBuffer(GLuint(), arraySize);

	while (currentBits < numBits)
	{
		std::vector<GLuint> threadCount{ startThreads };
		threadCount.reserve(numExec);

		std::swap(indicesBufferID_1, indicesBufferID_2);							// indicesBufferID_2 is initialized with indices cause it's swapped here

		// FIRST STEP: BIT MASK, check if a morton code gives zero or one for a certain mask (iteration)
		unsigned bitMask = 1 << currentBits++;

		bitMaskShader->bindBuffers(std::vector<GLuint> { mortonCodes, indicesBufferID_1, pBitsBufferID, nBitsBufferID });
		bitMaskShader->use();
		bitMaskShader->setUniform("arraySize", arraySize);
		bitMaskShader->setUniform("bitMask", bitMask);
		bitMaskShader->execute(numGroups, 1, 1, maxGroupSize, 1, 1);

		// SECOND STEP: build a binary tree with a summatory of the array
		reduceShader->bindBuffers(std::vector<GLuint> { nBitsBufferID });
		reduceShader->use();
		reduceShader->setUniform("arraySize", arraySize);

		iteration = 0;
		while (iteration < numExec)
		{
			numThreads = threadCount[threadCount.size() - 1];

			reduceShader->setUniform("iteration", iteration++);
			reduceShader->setUniform("numThreads", numThreads);
			reduceShader->execute(numGroups2Log, 1, 1, maxGroupSize, 1, 1);

			threadCount.push_back(std::ceil(numThreads / 2.0f));
		}

		// THIRD STEP: set last position to zero, its faster to do it in GPU than retrieve the array in CPU, modify and write it again to GPU
		resetPositionShader->bindBuffers(std::vector<GLuint> { nBitsBufferID });
		resetPositionShader->use();
		resetPositionShader->setUniform("arraySize", arraySize);
		resetPositionShader->execute(1, 1, 1, 1, 1, 1);

		// FOURTH STEP: build tree back to first level and compute position of each bit
		downSweepShader->bindBuffers(std::vector<GLuint> { nBitsBufferID });
		downSweepShader->use();
		downSweepShader->setUniform("arraySize", arraySize);

		iteration = unsigned(threadCount.size()) - 2;
		while (iteration >= 0 && iteration < numExec)
		{
			downSweepShader->setUniform("iteration", iteration);
			downSweepShader->setUniform("numThreads", threadCount[iteration--]);
			downSweepShader->execute(numGroups2Log, 1, 1, maxGroupSize, 1, 1);
		}

		reallocatePositionShader->bindBuffers(std::vector<GLuint> { pBitsBufferID, nBitsBufferID, indicesBufferID_1, indicesBufferID_2 });
		reallocatePositionShader->use();
		reallocatePositionShader->setUniform("arraySize", arraySize);
		reallocatePositionShader->execute(numGroups, 1, 1, maxGroupSize, 1, 1);
	}

	glDeleteBuffers(1, &indicesBufferID_1);
	glDeleteBuffers(1, &pBitsBufferID);
	glDeleteBuffers(1, &nBitsBufferID);
	glDeleteBuffers(1, &positionBufferID);

	delete[] indices;

	return indicesBufferID_2;
}

void PointCloudAggregator::updateWindowBuffers()
{
	ComputeShader::updateWriteBuffer(_depthBufferSSBO, uint64_t(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);
	ComputeShader::updateWriteBuffer(_rawDepthBufferSSBO, GLuint(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);
	ComputeShader::updateWriteBuffer(_color01SSBO, uint64_t(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);
	ComputeShader::updateWriteBuffer(_color02SSBO, uint64_t(), _windowSize.x * _windowSize.y, GL_DYNAMIC_DRAW);

	// Update size of texture
	glBindTexture(GL_TEXTURE_2D, _textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _windowSize.x, _windowSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glGenerateMipmap(GL_TEXTURE_2D);
}

void PointCloudAggregator::writeColorsTexture()
{
	const int numGroupsImage = ComputeShader::getNumGroups(_windowSize.x * _windowSize.y);
	
	_storeTexture->bindBuffers(std::vector<GLuint> { _depthBufferSSBO });
	_storeTexture->use();
	this->bindTexture();
	_storeTexture->setUniform("backgroundColor", _renderingParameters->_backgroundColor);
	_storeTexture->setUniform("texImage", GLint(0));
	_storeTexture->setUniform("windowSize", _windowSize);
	_storeTexture->execute(numGroupsImage, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);
}

void PointCloudAggregator::writeColorsTextureHQR()
{
	const int numGroupsImage = ComputeShader::getNumGroups(_windowSize.x * _windowSize.y);

	_storeHQRTexture->bindBuffers(std::vector<GLuint> { _color01SSBO, _color02SSBO });
	_storeHQRTexture->use();
	this->bindTexture();
	_storeHQRTexture->setUniform("backgroundColor", _renderingParameters->_backgroundColor);
	_storeHQRTexture->setUniform("texImage", GLint(0));
	_storeHQRTexture->setUniform("windowSize", _windowSize);
	_storeHQRTexture->execute(numGroupsImage, 1, 1, ComputeShader::getMaxGroupSize(), 1, 1);
}

void PointCloudAggregator::writePointCloudGPU()
{
	unsigned currentNumPoints, leftPoints = _pointCloud->getNumberOfPoints(), currentNumPointAux;
	unsigned numPoints = std::min(this->getAllowedNumberOfPoints(), _pointCloud->getNumberOfPoints());
	std::vector<PointCloud::PointModel>* points = _pointCloud->getPoints();
	GLuint indexSSBO = ComputeShader::setWriteBuffer(GLuint(), numPoints, GL_DYNAMIC_DRAW);

	while (leftPoints > 0)
	{
		currentNumPoints = std::min(numPoints, leftPoints), currentNumPointAux = currentNumPoints;

		GLuint pointBufferSSBO = ComputeShader::setReadBuffer(&(points->at(points->size() - leftPoints)), currentNumPoints, GL_DYNAMIC_DRAW);

		if (PointCloudParameters::_reducePointCloud)
		{
			this->reducePointChunk(pointBufferSSBO, indexSSBO, currentNumPointAux);
		}

		if (PointCloudParameters::_sortPointCloud)
		{
			this->sortPoints(pointBufferSSBO, currentNumPointAux);
		}

		_pointCloudSSBO.push_back(pointBufferSSBO);
		_pointCloudChunkSize.push_back(currentNumPointAux);
		leftPoints -= currentNumPoints;
	}

	glDeleteBuffers(1, &indexSSBO);
}
