/*
 * HierClassifier.cpp
 *
 *  Created on: 22-10-2013
 *      Author: jachu
 */

//OpenCV
#include <opencv2/opencv.hpp>
//STL
#include <cmath>
#include <algorithm>
#include <chrono>
//RobotsIntellect
#include "HierClassifier.h"
#include "UnionFind.h"
#include "ClassifierSVM.h"

#define PI 3.14159265359
/*#define HIST_SIZE_H 4
#define HIST_SIZE_S 4
#define HIST_SIZE_V 4
#define COVAR_HSV_SIZE 9
#define MEAN_HSV_SIZE 3
#define COVAR_LASER_SIZE 4
#define MEAN_LASER_SIZE 2*/

using namespace std;
using namespace cv;
using namespace boost;

Scalar colors[] = {
		Scalar(0xFF, 0x00, 0x00), //Red
		Scalar(0xFF, 0xFF, 0xFF), //White
		Scalar(0x00, 0xFF, 0xFF), //Cyan
		Scalar(0xC0, 0xC0, 0xC0), //Silver
		Scalar(0x00, 0x00, 0xFF), //Blue
		Scalar(0x80, 0x80, 0x80), //Gray
		Scalar(0x00, 0x00, 0xA0), //DarkBlue
		Scalar(0x00, 0x00, 0x00), //Black
		Scalar(0xAD, 0xD8, 0xE6), //LightBlue
		Scalar(0xFF, 0xA5, 0x00), //Orange
		Scalar(0x80, 0x00, 0x80), //Purple
		Scalar(0xA5, 0x2A, 0x2A), //Brown
		Scalar(0xFF, 0xFF, 0x00), //Yellow
		Scalar(0x80, 0x00, 0x00), //Maroon
		Scalar(0x00, 0xFF, 0x00), //Lime
		Scalar(0x00, 0x80, 0x00), //Green
		Scalar(0xFF, 0x00, 0xFF), //Magenta
		Scalar(0x80, 0x80, 0x00) //Olive
};

/*cv::Mat HierClassifier::projectPointsTo3D(cv::Mat disparity){
	//TODO correct
	Mat ret;
	Mat Q = Mat::eye(4, 4, CV_32FC1);
	cameraMatrix.copyTo(Mat(Q, Rect(0, 0, 3, 3)));
	reprojectImageTo3D(disparity, ret, Q);
	return ret;
}

cv::Mat HierClassifier::projectPointsTo2D(cv::Mat _3dImage){
	Mat ret;
	projectPoints(_3dImage, (0,0,0), (0,0,0), cameraMatrix, Mat(), ret);
	return ret;
}*/

void HierClassifier::prepareData(const std::vector<Entry>& data){
	cout << "Prepare data" << endl;
	//constructing dataset for each classifier without copying data
	dataClassifiers.resize(numWeakClassifiers);
	for(int c = 0; c < numWeakClassifiers; c++){
		dataClassifiers[c].resize(data.size());
		for(int e = 0; e < data.size(); e++){
			Entry tmp;
			//cout << "Col range: " << weakClassInfo[c].descBeg << ", " << weakClassInfo[c].descEnd << endl;
			tmp.descriptor = data[e].descriptor.colRange(
													weakClassInfo[c].descBeg,
													weakClassInfo[c].descEnd);
			tmp.label = data[e].label;
			tmp.weight = data[e].weight;
			dataClassifiers[c][e] = tmp;
		}
	}
	cout << "Prepared " << data.size() << " entries" << endl;
}

void HierClassifier::clearData(){
	dataClassifiers.clear();
	for(int c = 0; c < classifiers.size(); c++){
		delete classifiers[c];
	}
	classifiers.clear();
	classifiersInfo.clear();
	weights.clear();
	numLabels = 0;
}

//---------------MISCELLANEOUS----------------

HierClassifier::HierClassifier(cv::Mat icameraMatrix) :
	cacheEnabled(false),
	cameraMatrix(icameraMatrix)
{

}

HierClassifier::HierClassifier(cv::Mat icameraMatrix, TiXmlElement* settings) :
	cacheEnabled(false),
	cameraMatrix(icameraMatrix)
{
	loadSettings(settings);
}

HierClassifier::~HierClassifier(){
	for(int c = 0; c < weakClassifiersSet.size(); c++){
		delete weakClassifiersSet[c];
	}
	for(int c = 0; c < classifiers.size(); c++){
		delete classifiers[c];
	}
}


void HierClassifier::loadSettings(TiXmlElement* settings){
	TiXmlElement* pPtr = settings->FirstChildElement("cache");
	if(!pPtr){
		throw "Bad settings file - no cache setting for HierClassifier";
	}
	pPtr->QueryBoolAttribute("enabled", &cacheEnabled);

	pPtr = settings->FirstChildElement("segmentation");
	if(!pPtr){
		throw "Bad settings file - no segmentation setting for HierClassifier";
	}
	pPtr->QueryFloatAttribute("k", &kSegment);
	pPtr->QueryIntAttribute("min_size", &minSizeSegment);

	pPtr = settings->FirstChildElement("descriptor");
	if(!pPtr){
		throw "Bad settings file - no descriptor setting for HierClassifier";
	}
	std::vector<int> descLen;
	TiXmlElement* dPtr = pPtr->FirstChildElement();
	while(dPtr){
		if(dPtr->Value() == string("hist_HS")){
			dPtr->QueryIntAttribute("len_H", &histHLen);
			dPtr->QueryIntAttribute("len_S", &histSLen);
			if(descLen.size() < 1){
				descLen.resize(1);
			}
			descLen[0] = histHLen*histSLen;
		}
		else if(dPtr->Value() == string("hist_V")){
			dPtr->QueryIntAttribute("len", &histVLen);
			if(descLen.size() < 2){
				descLen.resize(2);
			}
			descLen[1] = histVLen;
		}
		else if(dPtr->Value() == string("covar_HSV")){
			dPtr->QueryIntAttribute("len", &covarHSVLen);
			if(descLen.size() < 3){
				descLen.resize(3);
			}
			descLen[2] = covarHSVLen;
		}
		else if(dPtr->Value() == string("mean_HSV")){
			dPtr->QueryIntAttribute("len", &meanHSVLen);
			if(descLen.size() < 4){
				descLen.resize(4);
			}
			descLen[3] = meanHSVLen;
		}
		else if(dPtr->Value() == string("covar_laser")){
			dPtr->QueryIntAttribute("len", &covarLaserLen);
			if(descLen.size() < 5){
				descLen.resize(5);
			}
			descLen[4] = covarLaserLen;
		}
		else if(dPtr->Value() == string("mean_laser")){
			dPtr->QueryIntAttribute("len", &meanLaserLen);
			if(descLen.size() < 6){
				descLen.resize(6);
			}
			descLen[5] = meanLaserLen;
		}
		else if(dPtr->Value() == string("kurt_laser")){
			dPtr->QueryIntAttribute("len", &kurtLaserLen);
			if(descLen.size() < 7){
				descLen.resize(7);
			}
			descLen[6] = kurtLaserLen;
		}
		else if(dPtr->Value() == string("hist_DI")){
			dPtr->QueryIntAttribute("len_D", &histDLen);
			dPtr->QueryIntAttribute("len_I", &histILen);
			if(descLen.size() < 8){
				descLen.resize(8);
			}
			descLen[7] = histDLen*histILen;
		}
		else if(dPtr->Value() == string("shape")){
			shapeLen = 14;
			if(descLen.size() < 9){
				descLen.resize(9);
			}
			descLen[8] = shapeLen;
		}
		else{
			throw "Bad settings file - no such descriptor";
		}
		dPtr = dPtr->NextSiblingElement();
	}

	descBeg.assign(descLen.size(), 0);
	for(int d = 1; d < descLen.size(); d++){
		descBeg[d] = descBeg[d - 1] + descLen[d - 1];
	}
	descBeg.push_back(descBeg.back() + descLen.back());

	for(int c = 0; c < weakClassifiersSet.size(); c++){
		delete weakClassifiersSet[c];
	}
	weakClassifiersSet.clear();
	weakClassInfo.clear();

	numWeakClassifiers = 0;
	TiXmlElement* cPtr = settings->FirstChildElement("Classifier");
	while(cPtr){
		string type;
		cPtr->QueryStringAttribute("type", &type);
		if(type == "SVM"){
			weakClassifiersSet.push_back(new ClassifierSVM(cPtr));
			TiXmlElement* iPtr = cPtr->FirstChildElement("info");
			if(!iPtr){
				throw "Bad settings file - no info setting for Classifier type = SVM";
			}
			int dBeg, dEnd;
			iPtr->QueryIntAttribute("desc_beg", &dBeg);
			iPtr->QueryIntAttribute("desc_end", &dEnd);
			weakClassInfo.push_back(WeakClassifierInfo(descBeg[dBeg], descBeg[dEnd]));
		}
		numWeakClassifiers++;
		cPtr = cPtr->NextSiblingElement("Classifier");
	}

	numIterations = 18;
}

void HierClassifier::saveCache(boost::filesystem::path file){
	TiXmlDocument doc;
	TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "UTF-8", "");
	doc.LinkEndChild(decl);
	TiXmlElement* pWeakClassifiers = new TiXmlElement("weak_classifiers");
	doc.LinkEndChild(pWeakClassifiers);
	pWeakClassifiers->SetAttribute("num_labels", numLabels);
	for(int c = 0; c < classifiers.size(); c++){
		TiXmlElement* pClassifier = new TiXmlElement("classifier");
		pWeakClassifiers->LinkEndChild(pClassifier);
		char buffer[10];
		sprintf(buffer, ".%02d.cache", c);
		pClassifier->SetAttribute("cache_file", file.string() + string(buffer));
		pClassifier->SetDoubleAttribute("weight", weights[c]);
		pClassifier->SetAttribute("desc_beg", classifiersInfo[c].descBeg);
		pClassifier->SetAttribute("desc_end", classifiersInfo[c].descEnd);

		classifiers[c]->saveCache(pClassifier, file.string() + string(buffer));
	}
	doc.SaveFile(file.c_str());
}

void HierClassifier::loadCache(boost::filesystem::path file){
	cout << "Loading cache from file " << file.string() << endl;
	TiXmlDocument doc(file.c_str());
	if(!doc.LoadFile()){
		throw "Could not load cache file";
	}
	TiXmlElement* pWeakClassifiers = doc.FirstChildElement("weak_classifiers");
	if(!pWeakClassifiers){
		throw "Bad cache file - no weak_classifiers";
	}
	int numClassifiers;
	pWeakClassifiers->QueryIntAttribute("num", &numClassifiers);
	pWeakClassifiers->QueryIntAttribute("num_labels", &numLabels);
	TiXmlElement* pClassifier = pWeakClassifiers->FirstChildElement("classifier");
	while(pClassifier){
		string cacheFile;
		double weight;
		WeakClassifierInfo info;
		pClassifier->QueryStringAttribute("cache_file", &cacheFile);
		pClassifier->QueryDoubleAttribute("weight", &weight);
		pClassifier->QueryIntAttribute("desc_beg", &(info.descBeg));
		pClassifier->QueryIntAttribute("desc_end", &(info.descEnd));
		weights.push_back(weight);
		classifiersInfo.push_back(info);
		classifiers.push_back(new ClassifierSVM());
		classifiers.back()->loadCache(pClassifier, cacheFile);

		pClassifier = pClassifier->NextSiblingElement("classifier");
	}
}

//---------------COMPUTING----------------

void HierClassifier::train(const std::vector<Entry>& data,
		int inumLabels)
{

	for(int i = 0; i < 20; i++){
		int e = rand() % data.size();
		cout << "label = " << data[e].label << ", weight = " << data[e].weight << endl;
		for(int d = 0; d < descBeg.size() - 1; d++){
			cout << "descriptor <" << descBeg[d] << ".." << descBeg[d + 1] << ") = " << data[e].descriptor.colRange(descBeg[d], descBeg[d + 1]) << endl;
		}
	}

	cout << "HierClassifier::train, numIterations = " << numIterations << ", numLabels = " << inumLabels << endl;

	clearData();
	prepareData(data);

	static const float epsMin = 10e-20;
	static const float epsMax = 10e20;

	numLabels = inumLabels;
	//initializing weights
	vector<double> dataWeights(data.size(), (double)1);

	float sumClassifierWeights = 0;
	//main loop
	for(int t = 0; t < numIterations; t++){
		cout << "Iteration " << t << endl;
		int maxIndex = 0;
		double maxVal = -1;
		for(int c = t % numWeakClassifiers; c < (t % numWeakClassifiers) + 1; c++){
		//for(int c = 0; c < numWeakClassifiers; c++){
			cout << "Training classifier " << c << endl;
			//training
			//dataWeights*entryWeights
			weakClassifiersSet[c]->train(dataClassifiers[c], dataWeights);

			//evaluating
			double score = 0;
			for(int e = 0; e < dataClassifiers[c].size(); e++){
				//cout << "Entry " << e << ", weight " << dataWeights[e]*dataClassifiers[c][e].weight << ": ";
				Mat probEst = weakClassifiersSet[c]->classify(dataClassifiers[c][e].descriptor);
				probEst *= 2;
				probEst -= 1;
				for(int l = 0; l < numLabels; l++){
					int ind = (l == dataClassifiers[c][e].label ? 1 : -1);
					//cout << ind << " (" << probEst.at<float>(l) << "), ";
					//dataWeights*entrysWeights
					score += dataClassifiers[c][e].weight*dataWeights[e]*probEst.at<float>(l)*ind/numLabels;
				}
				//cout << endl;
			}
			cout << "Classifier " << c << ", score " << score << endl;
			if(score > maxVal){
				maxVal = score;
				maxIndex = c;
			}
		}

		//computing weight for best classifer
		//compute accurate value of alpha
		double alpha = 0.5*log((1 + maxVal)/(1 - maxVal));
		if(alpha > epsMax || std::isnan(alpha)){
			cout << "Warning - alpha greater than epsMax" << endl;
			alpha = epsMax;
		}
		weights.push_back(abs(alpha));
		sumClassifierWeights += alpha;

		cout << "Adding classifier " << maxIndex << ", alpha = " << alpha << endl;
		//adding classifier
		classifiers.push_back(weakClassifiersSet[maxIndex]->copy());
		classifiersInfo.push_back(weakClassInfo[maxIndex]);

		cout << "recomputing dataWeights" << endl;
		//recomputing dataWeights
		double sum = 0;
		double sumVar = 0;
		double maxWeight = 0;
		int indMaxWeight = 0;
		for(int e = 0; e < dataClassifiers[maxIndex].size(); e++){
			Mat probEst = classifiers.back()->classify(dataClassifiers[maxIndex][e].descriptor);
			probEst *= 2;
			probEst -= 1;
			double score = 0;
			//if(e == 602){
			//	cout << "Entry " << e << " ";
			//}
			for(int l = 0; l < numLabels; l++){
				int ind = (l == dataClassifiers[maxIndex][e].label ? 1 : -1);
				//entrysWeights
				score += probEst.at<float>(l)*ind/numLabels;
				//if(e == 602){
				//	cout << (probEst.at<float>(l)+1)/2 << " (" << ind << "), ";
				//}
			}
			//score *= dataWeights[e]*dataClassifiers[maxIndex][e].weight*dataClassifiers[maxIndex].size();
			//if(e == 602){
			//	cout << ", score = " << score << endl;
			//}
			dataWeights[e] *= exp(-alpha*score);
			if(dataWeights[e] < epsMin){
				cout << "Warning - dataWeights[" << e << "] lesser than epsMin" << endl;
				dataWeights[e] = epsMin;
			}
			if(dataWeights[e] > epsMax){
				cout << "Warning - dataWeights[" << e << "] greater than epsMax" << endl;
				dataWeights[e] = epsMax;
			}
			if(dataWeights[e] > maxWeight){
				maxWeight = dataWeights[e];
				indMaxWeight = e;
			}
			//if(e == 602){
			//	cout << "dataWeights[" << e << "] = " << dataWeights[e] << endl;
			//}
			//dataWeights*entrysWeights
			sum += dataWeights[e]*dataClassifiers[maxIndex][e].weight;
			sumVar += dataWeights[e];
			//cout << "sum = " << sum << endl;
		}
		cout << "max weight = " << maxWeight << ", ind = " << indMaxWeight << endl;
		cout << "sum = " << sum << ", sumVar = " << sumVar << endl;
		double var = 0;
		double mean = sumVar / dataWeights.size();
		for(int e = 0; e < dataWeights.size(); e++){
			var += pow(dataWeights[e] - mean, 2);
			dataWeights[e] /= sum;
		}
		cout << "Variance = " << var / dataWeights.size() << endl;
	}
	for(int c = 0; c < weights.size(); c++){
		weights[c] /= sumClassifierWeights;
	}
	dataClassifiers.clear();
}


std::vector<cv::Mat> HierClassifier::classify(cv::Mat image,
							  	  	  	  	  cv::Mat terrain,
							  	  	  	  	  cv::Mat segmentation)
{


	Mat regionsOnImage;
	if(segmentation.empty()){
		regionsOnImage = segmentImage(image);
	}
	else{
		regionsOnImage = segmentation;
	}
	vector<Entry> entries = extractEntries(image, terrain, regionsOnImage, false);

	using namespace std::chrono;
	high_resolution_clock::time_point start = high_resolution_clock::now();
	high_resolution_clock::time_point end;

	//ofstream log("descriptors");
	//for(int e = 0; e < entries.size(); e++){
	//	log << entries[e].descriptor << endl;
	//}

	map<int, int> imageIdToEntry;
	for(int e = 0; e < entries.size(); e++){
		imageIdToEntry[entries[e].imageId] = e;
	}
	Mat result(entries.size(), numLabels, CV_32FC1, Scalar(0));
	for(int c = 0; c < classifiers.size(); c++){
		//cout << "Classifing using classifier " << c << endl;
		for(int e = 0; e < entries.size(); e++){
			Mat desc = entries[e].descriptor.colRange(
													classifiersInfo[c].descBeg,
													classifiersInfo[c].descEnd);
			//cout << "result, entry " << e << ", " << weights[c]*classifiers[c]->classify(desc) << endl;
			result.row(e) = result.row(e) + weights[c]*classifiers[c]->classify(desc);
		}
	}
	vector<Mat> ret;
	ret.resize(numLabels);
	for(int l = 0; l < numLabels; l++){
		ret[l] = Mat(image.rows, image.cols, CV_32FC1);
	}
	for(int l = 0; l < numLabels; l++){
		for(int r = 0; r < image.rows; r++){
			for(int c = 0; c < image.cols; c++){
				ret[l].at<float>(r, c) = result.at<float>(imageIdToEntry[regionsOnImage.at<int>(r, c)], l);
			}
		}
	}

	end = high_resolution_clock::now();
	static duration<double> compTime = duration<double>::zero();
	static int times = 0;

	compTime += duration_cast<duration<double> >(end - start);
	times++;
	cout << "Classify Times: " << times << endl;
	cout << "Classify Average computing time: " << compTime.count()/times << endl;

	return ret;
}

struct Pixel{
	int r, c;
	int imageId;
	Pixel(){}
	Pixel(int ir, int ic, int iimageId) :
		r(ir), c(ic), imageId(iimageId)
	{}
};

bool operator<(const Pixel& left, const Pixel& right){
	return (left.imageId < right.imageId);
}


int ransac2DLine(	const vector<Point2f>& points,
					vector<Point2f>& ptsStart,
					vector<Point2f>& ptsEnd,
					vector<bool>& bestPoints,
					int iterations,
					double thresholdCons,
					double thresholdModel,
					double thresholdGap,
					double thresholdMinLen)
{
	unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
	default_random_engine generator(seed);
	uniform_int_distribution<int> uniIntDist(0, points.size() - 1);
	uniform_int_distribution<int> uniIntDistNext(0, points.size() - 2);

	//double bestA, bestB, bestC;
	//bestA = bestB = bestC = 0;
	Point2f bestPtStart, bestPtEnd;
	int bestScore = -1;
	bestPoints.assign(points.size(), false);
	//Mat borderImage(480, 640, CV_8UC3, Scalar(0, 0, 0));
	//for(int p = 0; p < points.size(); p++){
	//	borderImage.at<Vec3b>((int)points[p].y, (int)points[p].x) = Vec3b(255, 255, 255);
	//}
	//Mat showImage;
	for(int i = 0; i < iterations; i++){
		//cout << "iteration " << i << endl;
		//borderImage.copyTo(showImage);
		//losowanie punktów
		int point1 = uniIntDist(generator);
		int point2 = uniIntDistNext(generator);
		if(point2 >= point1){
			point2++;
		}
		//cout << "point1 = " << point1 << ", point2 = " << point2 << endl;
		//circle(showImage, points[point1], 2, Scalar(0, 0, 255));
		//circle(showImage, points[point2], 2, Scalar(0, 0, 255));
		//imshow("imageRegion", showImage);
		//waitKey();
		//estymacja modelu
		double tmpA = points[point1].y - points[point2].y;
		double tmpB = points[point2].x - points[point1].x;
		double tmpC = points[point1].x * points[point2].y - points[point2].x * points[point1].y;
		//sprawdzenie modelu
		int conSize = 0;
		vector<bool> tmpPoints(points.size(), false);
		for(int p = 0; p < points.size(); p++){
			if(abs(tmpA * points[p].x + tmpB * points[p].y + tmpC)/sqrt(tmpA * tmpA + tmpB * tmpB) < thresholdCons){
				tmpPoints[p] = true;
				//circle(showImage, points[p], 2, Scalar(255, 0, 0));
				conSize++;
			}
		}

		//podział na odcinki - http://pl.wikipedia.org/wiki/Prosta#R.C3.B3wnanie_normalne
		double ni = (tmpC < 0 ? 1 : -1)/sqrt(tmpA*tmpA + tmpB*tmpB);
		tmpA *= ni;
		tmpB *= ni;
		tmpC *= ni;
		//cout << "bestA = " << bestA << ", bestB = " << bestB << ", bestC = " << bestC << endl;
		//równanie parametryczne
		double dirX = -tmpB;
		double dirY = tmpA;
		//punkt na prostej najbliższy punktowi (0, 0)
		double startX = -tmpC * tmpA;
		double startY = -tmpC * tmpB;
		vector<pair<double, int> > sortedPoints;
		for(int i = 0; i < bestPoints.size(); i++){
			if(tmpPoints[i] == true){
				//rzut na wektor równoległy do prostej
				double t = dirX * points[i].x + dirY * points[i].y;
				//cout << "Point = " << points[bestPoints[i]] << ", t = " << t << endl;
				sortedPoints.push_back(pair<double, int>(t, i));
			}
		}
		sort(sortedPoints.begin(), sortedPoints.end());
		//double bestLen = 0;
		//Point2f bestStartP, bestEndP;
		int begInd = 0;
		int endInd = 0;
		while(endInd < sortedPoints.size()){
			begInd = endInd;
			Point2f startP = Point2f(	startX + sortedPoints[begInd].first * dirX,
										startY + sortedPoints[begInd].first * dirY);
			endInd++;
			if(endInd < sortedPoints.size()){
				while(sortedPoints[endInd].first - sortedPoints[endInd - 1].first < thresholdGap){
					endInd++;
					if(endInd >= sortedPoints.size()){
						break;
					}
				}
			}
			Point2f endP = Point2f(	startX + sortedPoints[endInd - 1].first * dirX,
									startY + sortedPoints[endInd - 1].first * dirY);
			//cout << "sortedPoints.size() = " << sortedPoints.size() << ", begInd = " << begInd << ", endInd = " << endInd << endl;
			if(sortedPoints[endInd - 1].first - sortedPoints[begInd].first > thresholdMinLen){
				if(bestScore < endInd - begInd){
					bestPtStart = startP;
					bestPtEnd = endP;
					bestScore = endInd - begInd;
					bestPoints.assign(points.size(), false);
					for(int i = begInd; i < endInd; i++){
						bestPoints[sortedPoints[i].second] = true;
					}
				}
			}
		}
		//line(showImage, bestPtStart, bestPtEnd, Scalar(0, 0, 255));
		//imshow("imageRegion", showImage);
		//waitKey();
		//cout << "score = " << (double)conSize/points.size() << endl;
		//imshow("imageRegion", showImage);
		//waitKey();
		//if(bestScore < conSize){
		//	bestScore = conSize;
		//	bestPoints = tmpPoints;
		//}
		if((double)bestScore/points.size() > thresholdModel){
			break;
		}
	}
	if(bestScore > 0){
		ptsStart.push_back(bestPtStart);
		ptsEnd.push_back(bestPtEnd);
		return 1;
	}
	return 0;
	/*int segmentsCount = 0;
	if(bestScore > 0){
		//borderImage.copyTo(showImage);
		//for(int p = 0; p < bestPoints.size(); p++){
		//	if(bestPoints[p] == true){
		//		circle(showImage, points[p], 2, Scalar(255, 0, 0));
		//	}
		//}
		//imshow("imageRegion", showImage);
		//waitKey();
		//poprawienie modelu - http://pl.wikipedia.org/wiki/Metoda_najmniejszych_kwadrat%C3%B3w
		double S, Sx, Sy, Sxx, Sxy, Syy;
		S = bestScore;
		Sx = Sy = Sxx = Sxy = Syy = 0;
		for(int i = 0; i < bestPoints.size(); i++){
			if(bestPoints[i] == true){
				Sx += points[i].x;
				Sy += points[i].y;
				Sxx += points[i].x * points[i].x;
				Sxy += points[i].x * points[i].y;
				Syy += points[i].y * points[i].y;
			}
		}
		double delta = S*Sxx - Sx*Sx;
		bestA = Sx*Sy - S*Sxy;
		bestB = delta;
		bestC = Sx*Sxy - Sxx*Sy;
		//podział na odcinki - http://pl.wikipedia.org/wiki/Prosta#R.C3.B3wnanie_normalne
		double ni = (bestC < 0 ? 1 : -1)/sqrt(bestA*bestA + bestB*bestB);
		bestA *= ni;
		bestB *= ni;
		bestC *= ni;
		//cout << "bestA = " << bestA << ", bestB = " << bestB << ", bestC = " << bestC << endl;
		//równanie parametryczne
		double dirX = -bestB;
		double dirY = bestA;
		//punkt na prostej najbliższy punktowi (0, 0)
		double startX = -bestC * bestA;
		double startY = -bestC * bestB;
		vector<pair<double, int> > sortedPoints;
		for(int i = 0; i < bestPoints.size(); i++){
			if(bestPoints[i] == true){
				//rzut na wektor równoległy do prostej
				double t = dirX * points[i].x + dirY * points[i].y;
				//cout << "Point = " << points[bestPoints[i]] << ", t = " << t << endl;
				sortedPoints.push_back(pair<double, int>(t, i));
			}
		}
		sort(sortedPoints.begin(), sortedPoints.end());
		//double bestLen = 0;
		//Point2f bestStartP, bestEndP;
		int begInd = 0;
		int endInd = 0;
		while(endInd < sortedPoints.size()){
			begInd = endInd;
			Point2f startP = Point2f(	startX + sortedPoints[begInd].first * dirX,
										startY + sortedPoints[begInd].first * dirY);
			endInd++;
			if(endInd < sortedPoints.size()){
				while(sortedPoints[endInd].first - sortedPoints[endInd - 1].first < thresholdGap){
					endInd++;
					if(endInd >= sortedPoints.size()){
						break;
					}
				}
			}
			Point2f endP = Point2f(	startX + sortedPoints[endInd - 1].first * dirX,
									startY + sortedPoints[endInd - 1].first * dirY);
			//cout << "sortedPoints.size() = " << sortedPoints.size() << ", begInd = " << begInd << ", endInd = " << endInd << endl;
			if(sortedPoints[endInd - 1].first - sortedPoints[begInd].first > thresholdMinLen){
				ptsStart.push_back(startP);
				ptsEnd.push_back(endP);
				segmentsCount++;
				//line(showImage, startP, endP, Scalar(0, 0, 255));
				//imshow("imageRegion", showImage);
				//waitKey();
			}
			else{
				for(int i = begInd; i < endInd; i++){
					//cout << "Removing " << sortedPoints[i].second << " from bestPoints" << endl;
					bestPoints[sortedPoints[i].second] = false;
				}
			}
		}
	}*/
}

std::vector<Entry> HierClassifier::extractEntries(	cv::Mat imageBGR,
													cv::Mat terrain,
													cv::Mat regionsOnImage,
													bool debug)
{
	using namespace std::chrono;
	high_resolution_clock::time_point start = high_resolution_clock::now();

	if(debug){
		namedWindow("imageRegion");
	}
	Mat imageHSV;
	cvtColor(imageBGR, imageHSV, CV_BGR2HSV);
	vector<Pixel> pixels;
	pixels.resize(imageHSV.rows * imageHSV.cols);
	for(int r = 0; r < imageHSV.rows; r++){
		for(int c = 0; c < imageHSV.cols; c++){
			pixels[r * imageHSV.cols + c] = Pixel(r, c, regionsOnImage.at<int>(r, c));
		}
	}
	sort(pixels.begin(), pixels.end());

	bool runToNext = false;
	vector<pair<int, int> > terrainRegion;
	if(!terrain.empty()){
		Mat terrainPointsImage(terrain.cols, 2, CV_32FC1);
		Mat tmpTerrain = terrain.rowRange(0, 3).t();
		Mat tvec(1, 3, CV_32FC1, Scalar(0));
		Mat rvec(1, 3, CV_32FC1, Scalar(0));
		Mat distCoeffs(1, 5, CV_32FC1, Scalar(0));
		projectPoints(tmpTerrain, tvec, rvec, cameraMatrix, Mat(), terrainPointsImage);
		//terrainPointsImage = terrainPointsImage.t();
		terrainPointsImage = terrainPointsImage.reshape(1).t();
		//cout << tmpTerrain.rowRange(1, 10) << endl;
		//cout << terrainPointsImage.rowRange(1, 10) << endl;
		//cout << cameraMatrix << endl;
		for(int p = 0; p < terrain.cols; p++){
			int imageRow = round(terrainPointsImage.at<float>(1, p));
			int imageCol = round(terrainPointsImage.at<float>(0, p));
			if(imageRow >= 0 && imageRow < imageBGR.rows &&
				imageCol >= 0 && imageCol < imageBGR.cols)
			{
				int region = regionsOnImage.at<int>(imageRow, imageCol);
				terrainRegion.push_back(pair<int, int>(region, p));
			}
		}
		sort(terrainRegion.begin(), terrainRegion.end());
	}

	high_resolution_clock::time_point endSorting = high_resolution_clock::now();
	//cout << "End sorting terrain" << endl;

	vector<Entry> ret;
	int endIm = 0;
	int endTer = 0;
	while(endIm < pixels.size()){
		Mat values, valuesTer, histogramHS, histogramV, statisticsHSV, stisticsLaser;
		int begIm = endIm;
		while(pixels[begIm].imageId == pixels[endIm].imageId){
			endIm++;
			if(endIm == pixels.size()){
				break;
			}
		}
		//cout << "segment id = " << pixels[beg].imageId << ", beg = " << beg << ", end = " << end << endl;
		values = Mat(1, endIm - begIm, CV_8UC3);
		for(int p = begIm; p < endIm; p++){
			values.at<Vec3b>(p - begIm) = imageHSV.at<Vec3b>(pixels[p].r, pixels[p].c);
		}

		int begTer = endTer;
		if(begTer < terrainRegion.size()){
			while(terrainRegion[begTer].first != pixels[begIm].imageId){
				begTer++;
				if(begTer >= terrainRegion.size()){
					break;
				}
			}
		}
		endTer = begTer;
		if(endTer < terrainRegion.size()){
			while(terrainRegion[begTer].first == terrainRegion[endTer].first){
				endTer++;
				if(endTer >= terrainRegion.size()){
					break;
				}
			}
		}
		if(endTer - begTer > 0){
			valuesTer = Mat(terrain.rows, endTer - begTer, CV_32FC1);
			Mat tmpImageBGR(imageBGR);
			for(int p = begTer; p < endTer; p++){
				//cout << terrainRegion[p].second << endl;
				terrain.colRange(terrainRegion[p].second, terrainRegion[p].second + 1).copyTo(valuesTer.colRange(p - begTer, p - begTer + 1));
				//cout << "terrainRegion[p].second = " << terrainRegion[p].second << endl;
				//int imageRow = round(terrainPointsImage.at<float>(1 ,terrainRegion[p].second));
				//int imageCol = round(terrainPointsImage.at<float>(0, terrainRegion[p].second));
				//cout << "Point: " << imageRow << ", " << imageCol << endl;
				//tmpImageBGR.at<Vec3b>(imageRow, imageCol) = Vec3b(0x00, 0x00, 0xff);
			}
			//cout << "ImageId = " << pixels[begIm].imageId << endl;
			//imshow("imageBGR", imageBGR);
			//waitKey();
		}
		else{
			//cout << "Warning - no terrain values for imageId " << pixels[begIm].imageId << endl;
			valuesTer = Mat(6, 1, CV_32FC1, Scalar(0));
		}

		int channelsHS[] = {0, 1};
		float rangeH[] = {0, 40};
		float rangeS[] = {0, 146};
		const float* rangesHS[] = {rangeH, rangeS};
		int sizeHS[] = {histHLen, histSLen};
		int channelsV[] = {2};
		float rangeV[] = {48, 228};
		const float* rangesV[] = {rangeV};
		int sizeV[] = {histVLen};
		calcHist(&values, 1, channelsHS, Mat(), histogramHS, 2, sizeHS, rangesHS);
		calcHist(&values, 1, channelsV, Mat(), histogramV, 1, sizeV, rangesV);
		histogramHS = histogramHS.reshape(0, 1);
		histogramV = histogramV.reshape(0, 1);
		normalize(histogramHS, histogramHS);
		normalize(histogramV, histogramV);
		//cout << "V size = " << histogramV.size() << ", HS size = " << histogramHS.size() << endl;


		values = values.reshape(1, values.cols);
		//cout << "values size = " << values.size() << endl;
		Mat covarHSV;
		Mat meanHSV;
		calcCovarMatrix(values,
						covarHSV,
						meanHSV,
						CV_COVAR_NORMAL | CV_COVAR_SCALE | CV_COVAR_ROWS,
						CV_32F);
		//cout << "Calculated covar matrix" << endl;
		//cout << "meanHSV.size() = " << meanHSV.size() << endl;
		//cout << "covarHSV.size() = " << covarHSV.size() << endl;

		covarHSV = covarHSV.reshape(0, 1);
		meanHSV = meanHSV.reshape(0, 1);
		//normalize(covarHSV, covarHSV);
		//normalize(meanHSV, meanHSV);

		Mat covarLaser, meanLaser;
		calcCovarMatrix(valuesTer.rowRange(4, 6),
						covarLaser,
						meanLaser,
						CV_COVAR_NORMAL | CV_COVAR_SCALE | CV_COVAR_COLS,
						CV_32F);
		//cout << "meanLaser = " << meanLaser << endl;
		//cout << "covarLaser = " << covarLaser << endl;
		covarLaser = covarLaser.reshape(0, 1);
		meanLaser = meanLaser.reshape(0, 1);
		//normalize(covarLaser, covarLaser);
		//normalize(meanLaser, meanLaser);
		//cout << covarLaser << endl << meanLaser << endl;

		//cout << "Entry " << ret.size() << endl;
		//cout << "histHS = " << histogramHS << endl;
		//cout << "histV = " << histogramV << endl;
		//cout << "covarHSV = " << covarHSV << endl;
		//cout << "meanHSV = " << meanHSV << endl;

		Mat kurtLaser(1, 2, CV_32FC1);
		Mat tmpVal;
		valuesTer.rowRange(4, 6).copyTo(tmpVal);
		//cout << "tmpVal = " << tmpVal << endl;
		//cout << "mean(0) = " << meanLaser.at<float>(0) << ", mean(1) = " << meanLaser.at<float>(1) << endl;
		//cout << "stdDev^4(0) = " << pow(covarLaser.at<float>(0), 2) << ", stdDev^4(3) = " << pow(covarLaser.at<float>(3), 2) << endl;
		tmpVal.rowRange(0, 1) -= meanLaser.at<float>(0);
		tmpVal.rowRange(1, 2) -= meanLaser.at<float>(1);

		pow(tmpVal, 4, tmpVal);
		kurtLaser.at<float>(0) = sum(tmpVal.rowRange(0, 1))(0);
		if(tmpVal.cols * pow(covarLaser.at<float>(0), 2) != 0){
			kurtLaser.at<float>(0) = kurtLaser.at<float>(0) / (tmpVal.cols * pow(covarLaser.at<float>(0), 2)) - 3;
		}
		kurtLaser.at<float>(1) = sum(tmpVal.rowRange(1, 2))(0);
		if(tmpVal.cols * pow(covarLaser.at<float>(3), 2) != 0){
			kurtLaser.at<float>(1) = kurtLaser.at<float>(1) / (tmpVal.cols * pow(covarLaser.at<float>(3), 2)) - 3;
		}

		Mat histogramDI;
		int channelsDI[] = {0, 1};
		float rangeD[] = {1500, 2500};
		float rangeI[] = {1868, 4000};
		const float* rangesDI[] = {rangeD, rangeI};
		int sizeDI[] = {histDLen, histILen};
		Mat valHistD = valuesTer.rowRange(4, 5);
		Mat valHistI = valuesTer.rowRange(5, 6);
		Mat valuesHistDI[] = {valHistD, valHistI};
		calcHist(valuesHistDI, 2, channelsDI, Mat(), histogramDI, 2, sizeDI, rangesDI);
		histogramDI = histogramDI.reshape(0, 1);
		normalize(histogramDI, histogramDI);

		/*Mat huMoments;
		int curImageId = pixels[begIm].imageId;
		Mat tmpRegions = (regionsOnImage == curImageId);
		Moments mom = moments(tmpRegions, true);
		HuMoments(mom, huMoments);
		huMoments = huMoments.reshape(0, 1);*/
		//imshow("imageRegion", tmpRegions);
		//waitKey();

		//Generalised Fourier Descriptor
		/*static const int maxRadFreq = 5;
		static const int maxAngFreq = 5;
		Mat fr(maxRadFreq, maxAngFreq, CV_64FC1, Scalar(0));
		Mat fi(maxRadFreq, maxAngFreq, CV_64FC1, Scalar(0));
		//centroid
		double rc = 0;
		double cc = 0;
		for(int p = begIm; p < endIm; p++){
			rc += pixels[p].r;
			cc += pixels[p].c;
		}
		rc /= endIm - begIm;
		cc /= endIm - begIm;
		//maximum radius
		double maxRad = 0;
		for(int p = begIm; p < endIm; p++){
			double curRad = (rc - pixels[p].r)*(rc - pixels[p].r) + (cc - pixels[p].c)*(cc - pixels[p].c);
			if(curRad > maxRad){
				maxRad = curRad;
			}
		}
		maxRad = sqrt(maxRad);
		//polar fourier transform
		for(int rad = 0; rad < maxRadFreq; rad++){
			for(int ang = 0; ang < maxAngFreq; ang++){
				for(int p = begIm; p < endIm; p++){
					double radius = sqrt((pixels[p].r - rc)*(pixels[p].r - rc) +
										(pixels[p].c - cc)*(pixels[p].c - cc));
					double theta = atan2(pixels[p].r - rc, pixels[p].c - cc);
					if(theta < 0){
						theta += 2*PI;
					}
					Vec3b color = imageBGR.at<Vec3b>(pixels[p].r, pixels[p].c);
					//double intensity = (color[0]*0.114 + color[1]*0.587 + color[2]*0.299)/255;
					double intensity = 1.0;
					fr.at<double>(rad, ang) += (double)intensity*cos(2*PI*rad*(radius/maxRad) + ang*theta);	//real
					fi.at<double>(rad, ang) -= (double)intensity*sin(2*PI*rad*(radius/maxRad) + ang*theta); //imaginary
				}
			}
		}
		//FD
		Mat fd(1, maxRadFreq*maxAngFreq, CV_32FC1);
		for(int rad = 0; rad < maxRadFreq; rad++){
			for(int ang = 0; ang < maxAngFreq; ang++){
				if(rad == 0 && ang == 0){
					fd.at<float>(0) = (float)sqrt(fr.at<double>(0, 0)*fr.at<double>(0, 0) +
													fi.at<double>(0, 0)*fi.at<double>(0, 0))/(PI*maxRad*maxRad);
				}
				else{
					fd.at<float>(rad*maxAngFreq + ang) = sqrt(fr.at<double>(ang, rad)*fr.at<double>(ang, rad) +
																fi.at<double>(ang, rad)*fi.at<double>(ang, rad))/fd.at<float>(0);
				}
			}
		}*/
		/*Mat tmpRegions(regionsOnImage.rows + 2, regionsOnImage.cols + 2, CV_32SC1, Scalar(-1));
		regionsOnImage.copyTo(Mat(tmpRegions, Rect(1, 1, regionsOnImage.cols, regionsOnImage.rows)));
		Mat turns(1, 8, CV_32FC1, Scalar(0));
		{
			int nhood[][2] = {{1, 0},
							{1, 1},
							{0, 1},
							{-1, 1},
							{-1, 0},
							{-1, -1},
							{0, -1},
							{1, -1}};
			//vector<bool> visited(endIm - begIm, 0);
			int regId = pixels[begIm].imageId;
			//imshow("imageRegion", tmpRegions == regId);
			int count = 0;
			for(int p = begIm; p < endIm; p++){
				int r = pixels[p].r;
				int c = pixels[p].c;
				if(r != 0 && c != 0 && r != imageBGR.rows && c != imageBGR.cols){
					for(int dir = 0; dir < sizeof(nhood)/sizeof(nhood[0]); dir++){
						int nextDir = (dir + 1) % (sizeof(nhood)/sizeof(nhood[0]));
						if(tmpRegions.at<int>(r + 1 + nhood[dir][0], c + 1 + nhood[dir][1]) != regId &&
							tmpRegions.at<int>(r + 1 + nhood[nextDir][0], c + 1 + nhood[nextDir][1]) == regId)
						{
							count++;
							turns.at<float>(nextDir) += 1.0;
							continue;
						}
					}
				}
			}
			//cout << "count == " << count << endl;
			if(count > 0){
				turns = turns / count;
			}
		}*/
		cout << "Computing lines" << endl;
		Mat linesDesc(1, 14, CV_32FC1, Scalar(0));
		//vector<Vec4i> lines;
		Mat borderImage(imageBGR.rows, imageBGR.cols, CV_8UC1, Scalar(0));
		vector<Point2f> borderPoints;
		int nhood[][2] = {{1, 0},
						{1, 1},
						{0, 1},
						{-1, 1},
						{-1, 0},
						{-1, -1},
						{0, -1},
						{1, -1}};
		int regId = pixels[begIm].imageId;
		int countPix = 0;
		for(int p = begIm; p < endIm; p++){
			int r = pixels[p].r;
			int c = pixels[p].c;
			if(r != 0 && c != 0 && r != imageBGR.rows - 1 && c != imageBGR.cols - 1){
				for(int dir = 0; dir < sizeof(nhood)/sizeof(nhood[0]); dir++){
					int nextDir = (dir + 1) % (sizeof(nhood)/sizeof(nhood[0]));
					int nr = r + nhood[dir][0];
					int nc = c + nhood[dir][1];
					if(nr >= 0 && nc >= 0 && nr < imageBGR.rows && nc < imageBGR.cols){
						if(regionsOnImage.at<int>(nr, nc) != regId)
						{
							borderImage.at<unsigned char>(r, c) = 255;
							borderPoints.push_back(Point2f(c, r));
							countPix++;
							break;
						}
					}
				}
			}
		}
		cout << "countPix = " << countPix << endl;
		/*//dilate(tmpImage, tmpImage, Mat(), Point(-1, -1), 1);
		rectangle(tmpImage, Point(20, 20), Point(200, 200), Scalar(255));
		HoughLinesP(tmpImage, lines, 1, 4*CV_PI/180, 10, 10);*/

		vector<Point2f> ptsStart, ptsEnd;
		while(borderPoints.size() > 10){
			vector<bool> bestPoints;
			if(ransac2DLine(borderPoints,
							ptsStart,
							ptsEnd,
							bestPoints,
							50,
							3,
							0.8,
							5,
							15) == 0)
			{
				break;
			}
			vector<Point2f> newBorderPoints;
			for(int i = 0; i < borderPoints.size(); i++){
				if(bestPoints[i] == false){
					newBorderPoints.push_back(borderPoints[i]);
				}
			}
			borderPoints = newBorderPoints;
		}

		double meanLen = 0;
		double sumLen = 0;
		Mat rhos(1, ptsStart.size(), CV_32FC1);
		cout << "ptsStart.size() = " << ptsStart.size() << endl;
		if(ptsStart.size() > 0){
			for(int l = 0; l < ptsStart.size(); l++){
				double rho = atan2((double)(ptsStart[l].y - ptsEnd[l].y), (double)(ptsStart[l].x - ptsEnd[l].x));
				if(rho < 0){
					rhos.at<float>(l) = (float)(CV_PI + rho);
				}
				else{
					rhos.at<float>(l) = (float)rho;
				}
				double len = sqrt((double)(ptsStart[l].y - ptsEnd[l].y)*(ptsStart[l].y - ptsEnd[l].y) +
						(ptsStart[l].x - ptsEnd[l].x)*(ptsStart[l].x - ptsEnd[l].x));
				meanLen += len;
				sumLen += len;
			}

			meanLen /= ptsStart.size();
			double varLen = 0;
			for(int l = 0; l < ptsStart.size(); l++){
				//double rho = atan2((double)(ptsStart[l].y - ptsEnd[l].y), (double)(ptsStart[l].x - ptsEnd[l].x));
				double len = sqrt((ptsStart[l].y - ptsEnd[l].y)*(ptsStart[l].y - ptsEnd[l].y) +
						(ptsStart[l].x - ptsEnd[l].x)*(ptsStart[l].x - ptsEnd[l].x));
				//varRho += (rho - meanRho)*(rho - meanRho);
				varLen += (len - meanLen)*(len - meanLen);
			}
			varLen /= ptsStart.size();

			//cout << "Calculating hist" << endl;
			int rhoBins = 10;
			int histSize[] = {rhoBins};
			int channelsHist[] = {0};
			float rhoRanges[] = {0, CV_PI};
			const float* ranges[] = {rhoRanges};
			Mat rhoHist;
			calcHist(&rhos, 1, channelsHist, Mat(), rhoHist, 1, histSize, ranges);
			rhoHist = rhoHist.reshape(0, 1);
			//cout << "Shifting hist, rhoHist.type = " << rhoHist.type() << ", CV_32FC1 = " << CV_32FC1 << endl;
			float maxVal = 0;
			int maxInd = 0;
			for(int i = 0; i < rhoHist.cols; i++){
				//cout << "rhoHist.at<float>(" << i << ") = " << rhoHist.at<float>(i) << endl;
				if(rhoHist.at<float>(i) > maxVal){
					maxVal = rhoHist.at<float>(i);
					maxInd = i;
				}
			}
			//cout << "Copying shifted hist, maxInd = " << maxInd << endl;
			Mat tmpRhoHist(rhoHist.rows, rhoHist.cols, CV_32FC1);

			if(maxInd > 0){
				//cout << "First part" << endl;
				rhoHist.colRange(maxInd, rhoHist.cols).copyTo(tmpRhoHist.colRange(0, rhoHist.cols - maxInd));
				//cout << "Second part" << endl;
				rhoHist.colRange(0, maxInd).copyTo(tmpRhoHist.colRange(rhoHist.cols - maxInd, rhoHist.cols));
				rhoHist = tmpRhoHist;
			}
			//cout << "Normalization" << endl;
			rhoHist = rhoHist/ptsStart.size();

			linesDesc.at<float>(0) = (float)sumLen/countPix;
			linesDesc.at<float>(1) = (float)meanLen;
			linesDesc.at<float>(2) = (float)varLen;
			linesDesc.at<float>(3) = (float)ptsStart.size();
			//cout << "Copying hist, rhoHist.cols = " << rhoHist.cols << endl;
			rhoHist.copyTo(linesDesc.colRange(4, linesDesc.cols));
			//cout << "End copying hist" << endl;
		}

		if(debug && !runToNext){
			cout << "linesDesc = " << linesDesc << endl;
			Mat showImage;
			cvtColor(borderImage, showImage, CV_GRAY2BGR);
			for(int l = 0; l < ptsStart.size(); l++){
				line(showImage, Point(ptsStart[l].x, ptsStart[l].y), Point(ptsEnd[l].x, ptsEnd[l].y), Scalar(0, 0, 255));
			}
			imshow("imageRegion", showImage);
			char a = waitKey();
			if(a == 's'){
				runToNext = true;
			}
		}

		Entry tmp;
		tmp.imageId = pixels[begIm].imageId;
		tmp.weight = (endIm - begIm) + (endTer - begTer);
		tmp.descriptor = Mat(1, histHLen*histSLen +
							histVLen +
							covarHSVLen +
							meanHSVLen +
							covarLaserLen +
							meanLaserLen +
							kurtLaserLen +
							histDLen*histILen +
							shapeLen,
							CV_32FC1);

		int begCol = 0;
		histogramHS.copyTo(tmp.descriptor.colRange(begCol, begCol + histHLen*histSLen));
		begCol += histHLen*histSLen;
		histogramV.copyTo(tmp.descriptor.colRange(begCol, begCol + histVLen));
		begCol += histVLen;
		covarHSV.copyTo(tmp.descriptor.colRange(begCol, begCol + covarHSVLen));
		begCol += covarHSVLen;
		meanHSV.copyTo(tmp.descriptor.colRange(begCol, begCol + meanHSVLen));
		begCol += meanHSVLen;
		covarLaser.copyTo(tmp.descriptor.colRange(begCol, begCol + covarLaserLen));
		begCol += covarLaserLen;
		meanLaser.copyTo(tmp.descriptor.colRange(begCol, begCol + meanLaserLen));
		begCol += meanLaserLen;
		kurtLaser.copyTo(tmp.descriptor.colRange(begCol, begCol + kurtLaserLen));
		begCol += kurtLaserLen;
		histogramDI.copyTo(tmp.descriptor.colRange(begCol, begCol + histDLen*histILen));
		begCol += histDLen*histILen;
		linesDesc.copyTo(tmp.descriptor.colRange(begCol, begCol + shapeLen));
		begCol += shapeLen;

		ret.push_back(tmp);
	}

	static duration<double> sortingTime = duration<double>::zero();
	static duration<double> compTime = duration<double>::zero();
	static duration<double> wholeTime = duration<double>::zero();
	static int times = 0;

	high_resolution_clock::time_point endComp = high_resolution_clock::now();
	sortingTime += duration_cast<duration<double> >(endSorting - start);
	compTime += duration_cast<duration<double> >(endComp - endSorting);
	wholeTime += duration_cast<duration<double> >(endComp - start);
	times++;
	cout << "Times: " << times << endl;
	cout << "Extract Average sorting time: " << sortingTime.count()/times << endl;
	cout << "Extract Average computing time: " << compTime.count()/times << endl;
	cout << "Extract Average whole time: " << wholeTime.count()/times << endl;

	return ret;
}

struct Edge{
	int i, j;
	float weight;
	Edge() {}
	Edge(int ii, int ij, float iweight) : i(ii), j(ij), weight(iweight) {}
};

bool operator<(const Edge& left, const Edge& right){
	return left.weight < right.weight;
}


cv::Mat HierClassifier::segmentImage(cv::Mat image, int kCurSegment){

	using namespace std::chrono;
	high_resolution_clock::time_point start = high_resolution_clock::now();
	high_resolution_clock::time_point endSorting;
	high_resolution_clock::time_point endComp;
	high_resolution_clock::time_point endMerging;

	Mat imageR(image.rows, image.cols, CV_32FC1);
	Mat imageG(image.rows, image.cols, CV_32FC1);
	Mat imageB(image.rows, image.cols, CV_32FC1);
	Mat imageChannels[] = {imageR, imageG, imageB};
	Mat imageFloat(image.rows, image.cols, CV_32FC3);
	int nchannels = 3;
	/*int nhood[][2] = {{-1, 1},
					{1, 0},
					{1, 1},
					{0, 1}};*/
	int nhood[][2] = {{1, 0},
					{0, 1}};
	//cout << "Size of nhood " << sizeof(nhood)/sizeof(nhood[0]) << endl;
	//cout << "rows: " << image.rows << ", cols: " << image.cols << endl;
	if(kCurSegment == -1){
		kCurSegment = kSegment;
	}
	image.convertTo(imageFloat, CV_32F);
	//resize(imageFloat, imageFloat, Size(320, 240));
	GaussianBlur(imageFloat, imageFloat, Size(7, 7), 0.8);
	split(imageFloat, imageChannels);

	int nrows = imageFloat.rows;
	int ncols = imageFloat.cols;

	vector<Mat> segments;

	vector<Edge> edges;
	for(int r = 0; r < nrows; r++){
		for(int c = 0; c < ncols; c++){
			for(int nh = 0; nh < sizeof(nhood)/sizeof(nhood[0]); nh++){
				if((r + nhood[nh][0] < nrows) && (r + nhood[nh][0] >= 0) &&
						(c + nhood[nh][1] < ncols) && (c + nhood[nh][1] >= 0))
				{
					float diffAll = 0;
					for(int ch = 0; ch < nchannels; ch++){
						float diff = abs(imageChannels[ch].at<float>(r, c) - imageChannels[ch].at<float>(r + nhood[nh][0], c + nhood[nh][1]));
						diffAll += diff*diff;
					}
					diffAll = sqrt(diffAll);
					edges.push_back(Edge(c + ncols*r, c + nhood[nh][1] + ncols*(r + nhood[nh][0]), diffAll));
					//if(edges.back().i == 567768 || edges.back().j == 567768){
					//	cout << "diff = abs(" << (int)imageChannels[ch].at<unsigned char>(r, c) << " - " << (int)imageChannels[ch].at<unsigned char>(r + nhood[nh][0], c + nhood[nh][1]) << ") = " << diff << endl;
					//}
				}
			}
		}
	}
	sort(edges.begin(), edges.end()); //possible improvement by bin sorting

	endSorting = high_resolution_clock::now();

	{
		//cout << "Channel " << ch << endl;
		segments.push_back(Mat(nrows, ncols, CV_32SC1));

		//cout << "Largest differece = " << edges[edges.size() - 1].weight <<
		//		", between (" << edges[edges.size() - 1].i << ", " << edges[edges.size() - 1].j <<
		//		")" << endl;

		UnionFind sets(nrows * ncols);
		vector<float> intDiff;
		intDiff.assign(nrows * ncols, 0);
		for(vector<Edge>::iterator it = edges.begin(); it != edges.end(); it++){
			int iRoot = sets.findSet(it->i);
			int jRoot = sets.findSet(it->j);
			//cout << "i = " << it->i << ", j = " << it->j << ", weight = " << it->weight << endl;
			if(iRoot != jRoot){
				//cout << "intDiff[iRoot] + (float)k/sizes[iRoot] = " << intDiff[iRoot] << " + " << (float)k/sizes[iRoot] << " = " << intDiff[iRoot] + (float)k/sizes[iRoot] << endl;
				//cout << "intDiff[jRoot] + (float)k/sizes[jRoot] = " << intDiff[jRoot] << " + " << (float)k/sizes[jRoot] << " = " << intDiff[jRoot] + (float)k/sizes[jRoot] << endl;
				if(min(intDiff[iRoot] + (float)kCurSegment/sets.size(iRoot), intDiff[jRoot] + (float)kCurSegment/sets.size(jRoot))
						>=
						it->weight)
				{
					//cout << "union " << min(intDiff[iRoot] + (float)k/sizes[iRoot], intDiff[jRoot] + (float)k/sizes[jRoot]) << " >= " << it->weight << endl;
					int newRoot = sets.unionSets(iRoot, jRoot);
					intDiff[newRoot] = it->weight;
				}
			}
		}
		for(vector<Edge>::iterator it = edges.begin(); it != edges.end(); it++){
			int iRoot = sets.findSet(it->i);
			int jRoot = sets.findSet(it->j);
			if((iRoot != jRoot) && ((sets.size(iRoot) < minSizeSegment) || (sets.size(jRoot) < minSizeSegment))){
				sets.unionSets(iRoot, jRoot);
			}
		}
		set<int> numElements;
		for(int r = 0; r < nrows; r++){
			for(int c = 0; c < ncols; c++){
				segments.back().at<int>(r, c) = sets.findSet(c + ncols*r);
				numElements.insert(sets.findSet(c + ncols*r));
			}
		}
		//cout << "number of elements = " << numElements.size() << endl;
	}

	endComp = high_resolution_clock::now();

	Mat finSegments(nrows, ncols, CV_32SC1);
	UnionFind sets(nrows * ncols);

	for(vector<Edge>::iterator it = edges.begin(); it != edges.end(); it++){
		bool areOneSegment = true;
		for(int ch = 0; ch < segments.size(); ch++){
			if(segments[ch].at<int>(it->i / ncols, it->i % ncols) != segments[ch].at<int>(it->j / ncols, it->j % ncols)){
				areOneSegment = false;
				break;
			}
		}
		if(areOneSegment){
			sets.unionSets(it->i, it->j);
		}
	}
	for(int r = 0; r < nrows; r++){
		for(int c = 0; c < ncols; c++){
			finSegments.at<int>(r, c) = sets.findSet(c + ncols*r);
		}
	}

	endMerging = high_resolution_clock::now();

	static duration<double> sortingTime = duration<double>::zero();
	static duration<double> compTime = duration<double>::zero();
	static duration<double> mergingTime = duration<double>::zero();
	static duration<double> wholeTime = duration<double>::zero();
	static int times = 0;

	sortingTime += duration_cast<duration<double> >(endSorting - start);
	compTime += duration_cast<duration<double> >(endComp - endSorting);
	mergingTime += duration_cast<duration<double> >(endMerging - endComp);
	wholeTime += duration_cast<duration<double> >(endMerging - start);
	times++;
	cout << "Segment Times: " << times << endl;
	cout << "Segment Average sorting time: " << sortingTime.count()/times << endl;
	cout << "Segment Average computing time: " << compTime.count()/times << endl;
	cout << "Segment Average merging time: " << mergingTime.count()/times << endl;
	cout << "Segment Average whole time: " << wholeTime.count()/times << endl;


	return finSegments;
}

Mat HierClassifier::colorSegments(const Mat segments){
	int nrows = segments.rows;
	int ncols = segments.cols;
	Mat ret(nrows, ncols, CV_8UC3);

	map<int, int> colorMap;
	int ind = 0;
	for(int r = 0; r < nrows; r++){
		for(int c = 0; c < ncols; c++){
			if(colorMap.count(segments.at<int>(r, c)) == 0){
				colorMap.insert(pair<int, int>(segments.at<int>(r, c), (ind++) % (sizeof(colors)/sizeof(colors[0]))));
			}
		}
	}

	for(map<int, int>::iterator it = colorMap.begin(); it != colorMap.end(); it++){
		Mat mask = (segments == it->first);

		ret.setTo(colors[it->second], mask);
	}

	return ret;
}

std::map<int, int> HierClassifier::assignManualId(cv::Mat autoSegments, cv::Mat manualSegments){
	std::map<int, int> ret;
	map<int, int> autoImageIdToIdx, manualImageIdToIdx;
	int numAutoSegments = 0;
	int numManualSegments = 0;
	for(int r = 0; r < autoSegments.rows; r++){
		for(int c = 0; c < autoSegments.cols; c++){
			if(autoImageIdToIdx.count(autoSegments.at<int>(r, c)) == 0){
				autoImageIdToIdx[autoSegments.at<int>(r, c)] = numAutoSegments++;
			}
			if(manualImageIdToIdx.count(manualSegments.at<int>(r, c)) == 0){
				manualImageIdToIdx[manualSegments.at<int>(r, c)] = numManualSegments++;
			}
		}
	}
	vector<vector<int> > score(numAutoSegments, vector<int>(numManualSegments, 0));
	for(int r = 0; r < autoSegments.rows; r++){
		for(int c = 0; c < autoSegments.cols; c++){
			int autoIdx = autoImageIdToIdx[autoSegments.at<int>(r, c)];
			int manualIdx = manualImageIdToIdx[manualSegments.at<int>(r, c)];
			score[autoIdx][manualIdx]++;
		}
	}
	vector<int> maxScore(numAutoSegments, -1);
	vector<int> maxScoreIdx(numAutoSegments, 0);
	for(int a = 0; a < numAutoSegments; a++){
		for(map<int, int>::iterator itM = manualImageIdToIdx.begin(); itM != manualImageIdToIdx.end(); ++itM){
			if(maxScore[a] < score[a][itM->second]){
				maxScore[a] = score[a][itM->second];
				maxScoreIdx[a] = itM->first;
			}
		}
	}
	for(map<int, int>::iterator itA = autoImageIdToIdx.begin(); itA != autoImageIdToIdx.end(); ++itA){
		ret[itA->first] = maxScoreIdx[itA->second];
	}
	return ret;
}

void HierClassifier::crossValidateSVMs(const std::vector<Entry>& entries)
{
	clearData();
	prepareData(entries);
	for(int c = 1; c < weakClassifiersSet.size(); c++){
		if(weakClassifiersSet[c]->type() == Classifier::SVM){
			cout << "Cross validating classifier " << c << endl;
			weakClassifiersSet[c]->crossValidate(dataClassifiers[c]);
		}
	}
	dataClassifiers.clear();
}
