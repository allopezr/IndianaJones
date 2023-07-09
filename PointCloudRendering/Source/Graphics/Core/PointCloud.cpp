#include "stdafx.h"
#include "PointCloud.h"

#include <filesystem>
#include "Graphics/Application/PointCloudParameters.h"
#include "Graphics/Application/TextureList.h"
#include "Graphics/Core/ShaderList.h"
#include "Graphics/Core/VAO.h"
#include "LASlib/lasreader.hpp"
#include <pcl/features/normal_3d.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/point_types.h>
#include "tinyply/tinyply.h"

// Initialization of static attributes
const std::string	PointCloud::WRITE_POINT_CLOUD_FOLDER = "PointClouds/";

/// Public methods

PointCloud::PointCloud(const std::string& filename, const bool useBinary, const mat4& modelMatrix) : 
	Model3D(modelMatrix, 1), _filename(filename), _useBinary(useBinary), _calculatedNormals(false), _minColor(FLT_MAX), _maxColor(FLT_MIN), _maxClassId(0), _maxReturns(.0f)
{
}

PointCloud::~PointCloud()
{
}

void PointCloud::filterGround(CSF* csf, std::vector<GLint>& groundIndices)
{
	std::vector<csf::Point> csfPoints;
	std::vector<GLint> offGroundIndices;

	for (PointModel& point : _points)
	{
		csf::Point csfPoint;
		csfPoint.x = point._point.x;
		csfPoint.y = point._point.y;
		csfPoint.z = point._point.z;

		csfPoints.push_back(csfPoint);
	}

	csf->setPointCloud(csfPoints);
	csf->do_filtering(groundIndices, offGroundIndices, true);

	std::vector<GLint> indices (1e6);
	std::iota(indices.begin(), indices.end(), 0);
	csf->savePoints(indices, "Hola.xyz");
}

bool PointCloud::load(const mat4& modelMatrix)
{
	if (!_loaded)
	{
		bool success = false, binaryExists = false;

		if (_useBinary && (binaryExists = std::filesystem::exists(_filename + BINARY_EXTENSION)))
		{
			success = this->loadModelFromBinaryFile();

			if (PointCloudParameters::_computeNormal && !_calculatedNormals)
			{
				this->computeNormals();
				this->writeToBinary(_filename + BINARY_EXTENSION);
			}
		}

		if (!success)
		{
			if (std::filesystem::exists(_filename + BINARY_EXTENSION))
				success = this->loadModelFromPLY(modelMatrix);

			else if (std::filesystem::exists(_filename + LAS_EXTENSION))
				success = this->loadModelFromLAS(modelMatrix);

			if (success && PointCloudParameters::_computeNormal)
				this->computeNormals();
		}

		std::cout << "Number of Points: " << _points.size() << std::endl;

		if (success && !binaryExists)
		{
			this->writeToBinary(_filename + BINARY_EXTENSION);
		}

		_loaded = true;
		
		return true;
	}

	return false;
}

bool PointCloud::writePointCloud(const std::string& filename, const bool ascii)
{
	std::thread writePointCloudThread(&PointCloud::threadedWritePointCloud, this, filename, ascii);
	writePointCloudThread.detach();

	return true;
}

/// [Protected methods]

void PointCloud::computeCloudData()
{
	ModelComponent* modelComp = _modelComp[0];

	// Fill point cloud indices with iota
	modelComp->_pointCloud.resize(_points.size());
	std::iota(modelComp->_pointCloud.begin(), modelComp->_pointCloud.end(), 0);
}

void PointCloud::computeNormals()
{
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);

	for (PointModel& point : _points)
	{
		pcl::PointXYZ pclPoint(point._point.x, point._point.y, point._point.z);
		cloud->push_back(pclPoint);
	}

	// Create the normal estimation class, and pass the input dataset to it
	pcl::NormalEstimationOMP<pcl::PointXYZ, pcl::Normal> ne;
	ne.setInputCloud(cloud);

	// Create an empty kdtree representation
	pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>());
	ne.setSearchMethod(tree);

	// Output datasets
	pcl::PointCloud<pcl::Normal>::Ptr cloudNormals(new pcl::PointCloud<pcl::Normal>);
	ne.setKSearch(PointCloudParameters::_knn);
	ne.compute(*cloudNormals);

	for (int normalIdx = 0; normalIdx < cloudNormals->size(); ++normalIdx)
	{
		_points[normalIdx]._normal = vec4(glm::normalize(vec3(cloudNormals->at(normalIdx).normal_x, cloudNormals->at(normalIdx).normal_y, cloudNormals->at(normalIdx).normal_z)), .0f);
	}

	_calculatedNormals = true;
}

bool PointCloud::loadModelFromBinaryFile()
{
	return this->readBinary(_filename + BINARY_EXTENSION, _modelComp);
}

bool PointCloud::loadModelFromLAS(const mat4& modelMatrix)
{
	std::string filename = _filename + std::string(LAS_EXTENSION);
	LASreadOpener lasReadOpener;
	lasReadOpener.set_file_name(filename.c_str());

	LASreader* lasReader = lasReadOpener.open();
	if (lasReader == 0)
	{
		fprintf(stderr, "ERROR: could not open lasreader\n");
		return false;
	}

	float x, y, z, intensity, xoffset = lasReader->header.x_offset, yoffset = lasReader->header.y_offset, zoffset = lasReader->header.z_offset;
	unsigned returnNumber, numReturns, classId;

	for (int i = 0; i < 5 && lasReader->header.number_of_points_by_return[i] != 0; ++i) ++_maxReturns;
	_maxReturns = glm::clamp(_maxReturns - 1.0f, 1.0f, 255.0f);

	for (int idx = 0; idx < lasReader->npoints; idx++)
	{
		lasReader->read_point();
		LASpoint& pointReader = lasReader->point;
		
		x = pointReader.get_x(); y = pointReader.get_y(), z = pointReader.get_z(), intensity = pointReader.get_intensity();
		returnNumber = pointReader.get_return_number(); numReturns = pointReader.get_number_of_returns();
		classId = pointReader.get_classification();

		_points.push_back(PointModel{ vec3(x - xoffset, y - yoffset, z - zoffset), unsigned(intensity), vec3(.0f), PointModel::encodeReturnsClass(returnNumber / _maxReturns, numReturns / _maxReturns, classId / 256.0f) });
		_minColor = (std::min)(_minColor, intensity);
		_maxColor = (std::max)(_maxColor, intensity);
		_maxClassId = (std::max)(_maxClassId, classId);
	}

	_aabb = AABB(
		vec3(lasReader->get_min_x() - xoffset, lasReader->get_min_y() - yoffset, lasReader->get_min_z() - zoffset),
		vec3(lasReader->get_max_x() - xoffset, lasReader->get_max_y() - yoffset, lasReader->get_max_z() - zoffset));
}

bool PointCloud::loadModelFromPLY(const mat4& modelMatrix)
{
	std::unique_ptr<std::istream> fileStream;
	std::vector<uint8_t> byteBuffer;
	std::shared_ptr<tinyply::PlyData> plyPoints, plyColors;
	unsigned baseIndex;
	float* pointsRawFloat = nullptr;
	double* pointsRawDouble = nullptr;
	uint8_t* colorsRaw;

	try
	{
		const std::string filename = _filename + PLY_EXTENSION;
		fileStream.reset(new std::ifstream(filename, std::ios::binary));

		if (!fileStream || fileStream->fail()) return false;

		fileStream->seekg(0, std::ios::end);
		const float size_mb = fileStream->tellg() * float(1e-6);
		fileStream->seekg(0, std::ios::beg);

		tinyply::PlyFile file;
		file.parse_header(*fileStream);

		try { plyPoints = file.request_properties_from_element("vertex", { "x", "y", "z" }); }
		catch (const std::exception & e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }
		
		try { plyColors = file.request_properties_from_element("vertex", { "red", "green", "blue" }); }
		catch (const std::exception & e) { std::cerr << "tinyply exception: " << e.what() << std::endl; }

		file.read(*fileStream);

		{
			const bool isDouble = plyPoints->t == tinyply::Type::FLOAT64;
			const size_t numPoints = plyPoints->count;
			const size_t numPointsBytes = numPoints * (!isDouble ? sizeof(float) : sizeof(double)) * 3;

			const size_t numColors = plyColors->count;
			const size_t numColorsBytes = numColors * 1 * 3;

			// Allocate space
			_points.resize(numPoints);
			if (!isDouble)
			{
				pointsRawFloat = new float[numPoints * 3];
				std::memcpy(pointsRawFloat, plyPoints->buffer.get(), numPointsBytes);
			}
			else
			{
				pointsRawDouble = new double[numPoints * 3];
				std::memcpy(pointsRawDouble, plyPoints->buffer.get(), numPointsBytes);
			}
			colorsRaw = new uint8_t[numColors * 3];
			
			std::memcpy(colorsRaw, plyColors->buffer.get(), numColorsBytes);

			if (!isDouble)
			{
				for (unsigned index = 0; index < numPoints; ++index)
				{
					baseIndex = index * 3;

					_points[index] = PointModel{ vec3(pointsRawFloat[baseIndex], pointsRawFloat[baseIndex + 1], pointsRawFloat[baseIndex + 2]),
												 PointModel::getRGBColor(vec3(colorsRaw[baseIndex], colorsRaw[baseIndex + 1], colorsRaw[baseIndex + 2])) };
					_minColor = (std::min)(_minColor, float((std::min)((std::min)(colorsRaw[baseIndex], colorsRaw[baseIndex + 1]), colorsRaw[baseIndex + 2])));
					_maxColor = (std::max)(_maxColor, float((std::max)((std::max)(colorsRaw[baseIndex], colorsRaw[baseIndex + 1]), colorsRaw[baseIndex + 2])));
					_aabb.update(_points[index]._point);
				}
			}
			else
			{
				for (unsigned index = 0; index < numPoints; ++index)
				{
					baseIndex = index * 3;

					_points[index] = PointModel{ vec3(pointsRawDouble[baseIndex], pointsRawDouble[baseIndex + 1], pointsRawDouble[baseIndex + 2]),
												 PointModel::getRGBColor(vec3(colorsRaw[baseIndex], colorsRaw[baseIndex + 1], colorsRaw[baseIndex + 2])) };
					_minColor = (std::min)(_minColor, float((std::min)((std::min)(colorsRaw[baseIndex], colorsRaw[baseIndex + 1]), colorsRaw[baseIndex + 2])));
					_maxColor = (std::max)(_maxColor, float((std::max)((std::max)(colorsRaw[baseIndex], colorsRaw[baseIndex + 1]), colorsRaw[baseIndex + 2])));
					_aabb.update(_points[index]._point);
				}
			}
		}
	}
	catch (const std::exception & e)
	{
		std::cerr << "Caught tinyply exception: " << e.what() << std::endl;

		return false;
	}

	return true;
}

bool PointCloud::readBinary(const std::string& filename, const std::vector<Model3D::ModelComponent*>& modelComp)
{
	std::ifstream fin(filename, std::ios::in | std::ios::binary);
	if (!fin.is_open())
	{
		return false;
	}

	size_t numPoints;

	fin.read((char*)&numPoints, sizeof(size_t));
	_points.resize(numPoints);
	fin.read((char*)&_points[0], numPoints * sizeof(PointModel));
	fin.read((char*)&_aabb, sizeof(AABB));
	fin.read((char*)&_calculatedNormals, sizeof(bool));
	fin.read((char*)&_maxColor, sizeof(float));
	fin.read((char*)&_minColor, sizeof(float));
	fin.read((char*)&_maxClassId, sizeof(uint8_t));
	fin.read((char*)&_maxReturns, sizeof(float));

	fin.close();

	return true;
}

void PointCloud::setVAOData()
{
	VAO* vao = new VAO(false);
	ModelComponent* modelComp = _modelComp[0];
	unsigned startIndex = 0, size = modelComp->_pointCloud.size(), currentSize;

	vao->setVBOData(RendEnum::VBO_POSITION, _points, GL_STATIC_DRAW);
	vao->setIBOData(RendEnum::IBO_POINT_CLOUD, modelComp->_pointCloud);
	modelComp->_topologyIndicesLength[RendEnum::IBO_POINT_CLOUD] = unsigned(modelComp->_pointCloud.size());
}

void PointCloud::threadedWritePointCloud(const std::string& filename, const bool ascii)
{
	std::filebuf fileBuffer;					
	fileBuffer.open(WRITE_POINT_CLOUD_FOLDER + filename, ascii ? std::ios::out : std::ios::out | std::ios::binary);

	std::ostream outstream(&fileBuffer);
	if (outstream.fail()) throw std::runtime_error("Failed to open " + filename + ".");

	tinyply::PlyFile pointCloud;

	std::vector<vec3> position;
	std::vector<vec3> rgb;

	for (int pointIdx = 0; pointIdx < _points.size(); ++pointIdx)
	{
		position.push_back(_points[pointIdx]._point);
		rgb.push_back(_points[pointIdx].getRGBVec3());
	}

	const std::string componentName = "pointCloud";
	pointCloud.add_properties_to_element(componentName, { "x", "y", "z" }, tinyply::Type::FLOAT32, position.size(), reinterpret_cast<uint8_t*>(position.data()), tinyply::Type::INVALID, 0);
	pointCloud.add_properties_to_element(componentName, { "r", "g", "b" }, tinyply::Type::FLOAT32, rgb.size(), reinterpret_cast<uint8_t*>(rgb.data()), tinyply::Type::INVALID, 0);
	pointCloud.write(outstream, !ascii);
}

bool PointCloud::writeToBinary(const std::string& filename)
{
	std::ofstream fout(filename, std::ios::out | std::ios::binary);
	if (!fout.is_open())
	{
		return false;
	}

	const size_t numPoints = _points.size();
	fout.write((char*)&numPoints, sizeof(size_t));
	fout.write((char*)&_points[0], numPoints * sizeof(PointModel));
	fout.write((char*)&_aabb, sizeof(AABB));
	fout.write((char*)&_calculatedNormals, sizeof(bool));
	fout.write((char*)&_maxColor, sizeof(float));
	fout.write((char*)&_minColor, sizeof(float));
	fout.write((char*)&_maxClassId, sizeof(uint8_t));
	fout.write((char*)&_maxReturns, sizeof(float));

	fout.close();

	return true;
}