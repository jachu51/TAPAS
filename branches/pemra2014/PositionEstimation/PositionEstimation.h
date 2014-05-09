/*
 * PositionEstimation.h
 *
 * In this class, we integrate data from different sensors using Kalman Filter
 *
 * TO THINK: - find Kalman implementation and maybe put it in another class
 */

#ifndef POSITIONESTIMATION_H_
#define POSITIONESTIMATION_H_

#include <opencv2/opencv.hpp>
#include "Encoders/Encoders.h"
#include "GPS/GPS.h"
#include "IMU/IMU.h"
#include <thread>


class Robot;
class Debug;

class PositionEstimation {
	friend class Debug;

private:
	// unique pointer to the PositionEstimation thread
	std::thread estimationThread;
	bool runThread;

	// Kalman filter to gather position information
	cv::Mat state;
	cv::KalmanFilter *KF;

	// GPS
	GPS gps;

	// IMU
	IMU imu;

	// Encoders
	Encoders encoders;

	//Parent class Robot
	Robot* robot;

	int ENCODER_TICK_PER_REV ;
	double WHEEL_DIAMETER ;
	double WHEEL_BASE ;


public:
	PositionEstimation(Robot* irobot);
	virtual ~PositionEstimation();

	// The cycle of the position estimation thread
	void run();

	// Stopping the position estimation thread
	void stopThread();

	// Initializing the filter
	void kalmanSetup();

	// Update Kalman - updates on GPS
	void KalmanLoop();

	// Encoders - predict
	void KalmanPredict();

	// Zeroes the current position estimate
	void setZeroPosition();

	//----------------------EXTERNAL ACCESS TO MEASUREMENTS
	//CV_32SC1 2x1: left, right encoder
	cv::Mat getEncoderData();

	//----------------------ACCESS TO COMPUTED DATA
	//CV_32SC1 3x1: x, y, fi
	const cv::Mat getEstimatedPosition();

	//----------------------MENAGMENT OF PositionEstimation DEVICES
	//Gps
	void openGps(std::string port);

	void closeGps();

	bool isGpsOpen();

	//Imu
	void openImu(std::string port);

	void closeImu();

	bool isImuOpen();

	//Encoders
	void openEncoders(std::string port);

	void closeEncoders();

	bool isEncodersOpen();
};

#include "../Robot/Robot.h"

#endif /* POSITIONESTIMATION_H_ */
