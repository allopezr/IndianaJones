#pragma once

#include "Geometry/3D/AABB.h"
#include "Graphics/Application/RenderingParameters.h"
#include "Graphics/Core/Model3D.h"

/**
*	@file Pix4DPointCloud.h
*	@authors Alfonso López Ruiz (alr00048@red.ujaen.es)
*	@date 09/24/2020
*/

#define LAS_EXTENSION ".las"
#define LAZ_EXTENSION ".laz"
#define PLY_EXTENSION ".ply"

/**
*	@brief 
*/
class PointCloud : public Model3D
{
public:
	struct PointModel
	{
		vec3		_point;
		unsigned	_rgb;

		vec3		_normal;
		unsigned	_returnClass;

		/**
		*	@brief Encodes return id and number of returns.
		*/
		static unsigned encodeReturnsClass(float returnNumber, float numReturns, float classId) { return glm::packUnorm4x8(vec4(returnNumber, numReturns, classId, .0f)); }

		/**
		*	@return RGB color packed as a single unsigned value. 
		*/
		static unsigned getRGBColor(const vec3& rgb) { return glm::packUnorm4x8(vec4(rgb, .0f) / 255.0f); }

		/**
		*	@return RGB color packed as a single unsigned value.
		*/
		vec4 getRGBVec3() { return glm::unpackUnorm4x8(_rgb) * 255.0f; }

		/**
		*	@brief Packs an RGB color into an unsigned value. 
		*/
		void saveRGB(const vec3& rgb) { _rgb = this->getRGBColor(rgb); }
	};

protected:
	const static std::string	WRITE_POINT_CLOUD_FOLDER;					//!<

protected:
	std::string					_filename;									//!<

	bool						_calculatedNormals;							//!<
	bool						_useBinary;									//!<

	// Spatial information
	AABB						_aabb;										//!<
	float						_lidarBeamWidth;							//!<
	std::vector<PointModel>		_points;									//!<

	// Radiometric information
	float						_minColor, _maxColor, _maxReturns;			//!<
	unsigned					_maxClassId;								//!<

protected:
	/**
	*	@brief Computes a triangle mesh buffer composed only by indices.
	*/
	void computeCloudData();

	/**
	*	@brief Computes normal vectors with PCL.
	*/
	void computeNormals();

	/**
	*	@brief Fills the content of model component with binary file data.
	*/
	bool loadModelFromBinaryFile();

	/**
	*	@brief 
	*/
	bool loadModelFromLAS(const mat4& modelMatrix);

	/**
	*	@brief 
	*/
	bool loadModelFromPLY(const mat4& modelMatrix);

	/**
	*	@brief Loads the PLY point cloud from a binary file, if possible.
	*/
	virtual bool readBinary(const std::string& filename, const std::vector<Model3D::ModelComponent*>& modelComp);

	/**
	*	@brief Communicates the model structure to GPU for rendering purposes.
	*/
	virtual void setVAOData();

	/**
	*	@brief
	*/
	void threadedWritePointCloud(const std::string& filename, const bool ascii);

	/**
	*	@brief Writes the model to a binary file in order to fasten the following executions.
	*	@return Success of writing process.
	*/
	virtual bool writeToBinary(const std::string& filename);

public:
	/**
	*	@brief 
	*/
	PointCloud(const std::string& filename, const bool useBinary, const mat4& modelMatrix = mat4(1.0f));

	/**
	*	@brief
	*/
	virtual ~PointCloud();

	/**
	*	@brief
	*/
	void filterGround(CSF* csf, std::vector<GLint>& groundIndices);

	/**
	*	@brief Loads the point cloud, either from a binary or a PLY file.
	*	@param modelMatrix Model transformation matrix.
	*	@return True if the point cloud could be properly loaded.
	*/
	virtual bool load(const mat4& modelMatrix = mat4(1.0f));

	/**
	*	@brief Updates the current Axis-Aligned Bounding-Box.
	*/
	void updateBoundaries(const vec3& xyz) { _aabb.update(xyz); }

	/**
	*	@brief Writes point cloud as a PLY file.
	*/
	bool writePointCloud(const std::string& filename, const bool ascii);

	// Getters

	/**
	*	@return
	*/
	AABB getAABB() { return _aabb; }

	/**
	*	@return Path where the point cloud is saved.
	*/
	std::string getFilename() { return _filename; }

	/**
	*	@return Minimum intensity.
	*/
	unsigned getMaxClassId() { return _maxClassId; }

	/**
	*	@return Minimum intensity.
	*/
	float getMaxColor() { return _maxColor; }

	/**
	*	@return Maximum number of returns.
	*/
	float getMaxReturns() { return _maxReturns; }

	/**
	*	@return Minimum intensity.
	*/
	float getMinColor() { return _minColor; }

	/**
	*	@brief
	*/
	unsigned getNumberOfPoints() { return unsigned(_points.size()); }

	/**
	*	@return
	*/
	std::vector<PointModel>* getPoints() { return &_points; }
};

