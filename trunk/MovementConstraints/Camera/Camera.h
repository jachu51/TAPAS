/*
 * Camera.h
 *
 *  Created on: 01-07-2013
 *      Author: jachu
 */

#ifndef CAMERA_H_
#define CAMERA_H_

//STL
#include <string>
#include <vector>
//OpenCV
#include <opencv2/opencv.hpp>
//Boost
#include <boost/filesystem.hpp>
//TinyXML
#include <tinyxml.h>
//Robots Intellect

class MovementConstraints;
class Debug;

struct Entry{
	int label;
	cv::Mat descriptor;
	Entry() {}
	Entry(int ilabel, cv::Mat idescriptor) : label(ilabel), descriptor(idescriptor){}
};

class Camera {
	friend class Debug;

	//Parent MovementConstraints class
	MovementConstraints* movementConstraints;

	int numCameras, numRows, numCols, angleX, angleY, cameraZ;

	bool cacheEnabled;

	int classifyGrid;

	boost::filesystem::path learningDir;

	//CV_32FC1 MAP_SIZExMAP_SIZE: 0-1 chance of being occupied, robot's position (MAP_SIZE/2, 0)
	cv::Mat constraints;

	//CV_32FC1 4x4: camera origin position and orientation
	cv::Mat cameraOrig[2];

	//CV_32FC1 4x1: ground plane equation [A, B, C, D]'
	cv::Mat groundPlane;

	//Grid of image classification
	int cameraGrid;

	int currentTimeStamp;

	boost::filesystem::path settingsFile;

	CvSVM svm;

	CvSVMParams svmParams;

	int bins;

	std::vector<Entry> entries;

	std::vector<std::string> labels;

	//array containing polygon vertices for all image regions
	std::vector<std::vector<std::vector<cv::Point*> > > groundPolygons;

	void computeConstraints(std::vector<cv::Mat> image);

	void computeGroundPolygons();

	cv::Point3f computePointProjection(cv::Point2f imPoint, int cameraInd);

	void addToLearnDatabase(cv::Mat samples, int label);

	void clearLearnDatabase();

	void learn();

	cv::Mat selectPolygonPixels(std::vector<cv::Point2i> polygon, const cv::Mat& image);

	void learnFromDir(boost::filesystem::path dir);

	cv::Mat classifySlidingWindow(cv::Mat image);

	void GenerateColorHistHSVGpu(
			const cv::gpu::GpuMat& ImageH,
			const cv::gpu::GpuMat& ImageS,
			cv::gpu::GpuMat& result,
			cv::gpu::GpuMat& buf);

	//Run as separate thread
	void cameraThread();

	void readSettings(TiXmlElement* settings);

	void readCache(boost::filesystem::path cacheFile);

	void saveCache(boost::filesystem::path cacheFile);
public:
	Camera(MovementConstraints* imovementConstraints, TiXmlElement* settings);
	virtual ~Camera();

	cv::Mat segment(cv::Mat image);

	//Returns constraints map and inserts time of data from cameras fetch
	const cv::Mat getConstraints(int* timestamp);

	//CV_8UC3 2x640x480: left, right image
	const std::vector<cv::Mat> getData();

	void open();

	void close();

	bool isOpen();
};

#include "../MovementConstraints.h"

#endif /* CAMERA_H_ */
