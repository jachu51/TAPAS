/*
 * PositionEstimation.cpp
 *
 *  Created on: Mar 31, 2013
 *      Author: smi
 */

#include <opencv2/opencv.hpp>
#include <string>
#include "PositionEstimation.h"
#include <thread>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>

using namespace cv;
using namespace std;

PositionEstimation::PositionEstimation(Robot* irobot) :
		robot(irobot), imu(irobot), gps(irobot), encoders(irobot) {
	std::cout << "PositionEstimation::PositionEstimation" << std::endl;
	ENCODER_TICK_PER_REV = 300;
	WHEEL_DIAMETER = 178.0 / 1000;
	WHEEL_BASE = 432.0 / 1000;
	/* KALMAN:
	 * - we track 2 values -> global position
	 *
	 * - to predict we can use values from encoders
	 * - to correct we can use information from the GPS
	 *
	 */
	kalmanSetup();




	lastUpdateTimestamp = std::chrono::high_resolution_clock::now();
	lastEncoderTimestamp = std::chrono::high_resolution_clock::now();
	lastGpsTimestamp = std::chrono::high_resolution_clock::now();
	lastImuTimestamp = std::chrono::high_resolution_clock::now();
	runThread = false;
	estimationThread = std::thread(&PositionEstimation::run, this);

	std::cout << "End PositionEstimation::PositionEstimation" << std::endl;
}

PositionEstimation::~PositionEstimation() {
	cout << "~PositionEstimation()" << endl;
	stopThread();
	closeGps();
	closeImu();
	delete EKF;
	cout << "End ~PositionEstimation()" << endl;
}

void PositionEstimation::run() {
	while(!gps.isOpen()) usleep(200);
	while (gps.getFixStatus() == 1 || (fabs(gps.getLat()) < 0.00001
			|| fabs(gps.getLon()) < 0.000001)) {
		usleep(200);
	};


	gps.setZeroXY(gps.getLat(), gps.getLon());

	struct timeval start, end;
	while (runThread) {
		gettimeofday(&start, NULL);

		KalmanLoop();

		// Thread sleep, so that the position is not updated too often
		// Right now 1 ms as Robot Drive has it's own sleep
		std::chrono::milliseconds duration(200);
		std::this_thread::sleep_for(duration);

		gettimeofday(&end, NULL);

		long seconds = end.tv_sec - start.tv_sec;
		long useconds = end.tv_usec - start.tv_usec;
		long mtime = ((seconds) * 1000 + useconds / 1000.0) + 0.5;

		if (mtime == 0)
			mtime = 1;
		printf("PE: X:%5.5f \tY:%5.5f \tS:%5.5f \tA:%5.5f\n",
				state.at<double>(0), state.at<double>(1), state.at<double>(2),
				state.at<double>(3));

		//cout << "PE:: framerate: " << 1000.0 / mtime << endl;
	}
}

void PositionEstimation::stopThread() {
	runThread = false;
	if (estimationThread.joinable()) {
		estimationThread.join();
	}
}

void PositionEstimation::kalmanSetup() {
	EKF = new ExtendedKalmanFilter();

	state = cv::Mat(4, 1, CV_64F);
	setZeroPosition();
}

void PositionEstimation::KalmanLoop() {


	// Local variables
	std::chrono::high_resolution_clock::time_point encoderTimestamp,
			gpsTimestamp, imuTimestamp;
	bool predictPerformed = false;

	// Time difference between updates
	float predictTime =
			std::chrono::duration_cast < std::chrono::milliseconds
					> (std::chrono::high_resolution_clock::now()
							- lastUpdateTimestamp).count();
	lastUpdateTimestamp = std::chrono::high_resolution_clock::now();

	printf("Kalman Loop --> predictTime: %f\n", predictTime);

	// Check if GPS is online
//	printf("Checking GPS\n");
	if (isGpsOpen() && gps.getFixStatus() > 1) {
		printf("GPS is online and will be used in update!\n");

		// Get the GPS data if GPS is available
		gpsTimestamp = this->gps.getTimestamp();
		float gps_dt = std::chrono::duration_cast < std::chrono::milliseconds
				> (gpsTimestamp - lastGpsTimestamp).count();

		// Current measurement is newer than last update, so predict
		if (gps_dt > 0) {
			EKF->predict(predictTime / 1000);
			predictPerformed = true;

			// Correct with GPS
			lastGpsTimestamp = gpsTimestamp;
			Mat gps_data = Mat(2, 1, CV_64FC1);
			gps_data.at<double>(0, 0) = this->gps.getPosX()/1000;
			gps_data.at<double>(1, 0) = this->gps.getPosY()/1000;
			printf("\n\n GPS : %f %f\n", gps.getLat(), gps.getLon());
			printf("GPS pos : %f %f\n", gps.getPosX()/1000, gps.getPosY()/1000);
			printf("Gps update --> values %f %f\n", gps_data.at<double>(0, 0),
					gps_data.at<double>(1, 0));
			state = EKF->correctGPS(gps_data);

		}

	}

	// Check if encoder is online
//	printf("Checking encoder\n");
	if (isEncodersOpen()) {
		printf("Encoders are open and will be used in update!\n");
		cv::Mat enc_data = this->getEncoderData(encoderTimestamp);
		printf("Encoder data: %d %d\n", enc_data.at<int>(0) - lastLeft,
							enc_data.at<int>(1) - lastRight);

		float encoder_dt = std::chrono::duration_cast
				< std::chrono::milliseconds
				> (encoderTimestamp - lastEncoderTimestamp).count();
		if (encoder_dt > 0) {

			if (!predictPerformed) {
				EKF->predict(predictTime / 1000);
				predictPerformed = true;
			} else {
				EKF->predict(0);
			}

			printf("Encoder data: %d %d %f\n", enc_data.at<int>(0) - lastLeft,
					enc_data.at<int>(1) - lastRight, encoder_dt);
			lastEncoderTimestamp = encoderTimestamp;

			if (encoderStart) {
				lastLeft = enc_data.at<int>(0);
				lastRight = enc_data.at<int>(1);
				encoderStart = 0;
			}

			printf("Bug test: %f %f\n", ENCODER_TICK_PER_REV, WHEEL_DIAMETER);

			float left_encoder = ((float) (enc_data.at<int>(0) - lastLeft))
					/ ENCODER_TICK_PER_REV * M_PI * WHEEL_DIAMETER;
			float right_encoder = ((float) (enc_data.at<int>(1) - lastRight))
					/ ENCODER_TICK_PER_REV * M_PI * WHEEL_DIAMETER;

			printf("Encoder left/right: %f %f\n", left_encoder, right_encoder);

			float distance = (left_encoder + right_encoder) / 2.0;

			Mat speed(1, 1, CV_64FC1);
			speed.at<double>(0) = (double) (distance / encoder_dt * 1000); // Is in seconds or ms ?
			printf("Encoder distance: %.10f\n", distance);
//				printf("Encoder encoder_dt: %.10f\n", encoder_dt );
			printf("Encoder speed update: %f\n", distance / encoder_dt);
			state = EKF->correctEncoder(speed);

			lastLeft = enc_data.at<int>(0);
			lastRight = enc_data.at<int>(1);

		}
	}

	// Checking if info from IMU is available
//	printf("Checking IMU\n");
	if (isImuOpen()) {
		printf("IMU is online and will be used\n");
		cv::Mat imuData = this->imu.getData(imuTimestamp);
		printf("IMU data : %f\n", imuData.at<float>(11));

		float imu_dt = std::chrono::duration_cast < std::chrono::milliseconds
				> (imuTimestamp - lastImuTimestamp).count();

		// Starting imu angle
		if (imuStart) {
			imuZeroAngle = 0;
			imuStart = false;
		}

		// Imu timestamps
		if (imu_dt > 0) {
			lastImuTimestamp = imuTimestamp;

			if (!predictPerformed) {
				EKF->predict(predictTime / 1000);
				predictPerformed = true;
			} else {
				EKF->predict(0);
			}

			// 3x4 - acc(x, y, z), gyro(x, y, z), magnet(x, y, z), euler(yaw, pitch, roll)
			Mat orientation(1, 1, CV_64FC1);
			orientation.at<double>(0) = (double) (imuData.at<float>(11)
					- imuZeroAngle) * M_PI / 180.0;
//			printf("IMU new update: %f\n", imuData.at<float>(11));
			state = EKF->correctIMU(orientation);
		}
	}
}

void PositionEstimation::setZeroPosition() {
	state.at<float>(0) = 0.0;
	state.at<float>(1) = 0.0;
	state.at<float>(2) = 0.0;

	lastLeft = 0;
	lastRight = 0;
	encoderStart = 1;
	imuStart = 1;

	if (isGpsOpen()) {
		gps.setZeroXY(gps.getLat(), gps.getLon());
	}

//	KF->statePost = KF->statePre = Mat::zeros(2,1, CV_32F);
}

//----------------------EXTERNAL ACCESS TO MEASUREMENTS
//CV_32SC1 2x1: left, right encoder
cv::Mat PositionEstimation::getEncoderData(
		std::chrono::high_resolution_clock::time_point &timestamp) {
	return encoders.getEncoders(timestamp);
}

//CV_32FC1 3x4: acc(x, y, z), gyro(x, y, z), magnet(x, y, z), euler(roll, pitch, yaw)
cv::Mat PositionEstimation::getImuData(
		std::chrono::high_resolution_clock::time_point &timestamp) {
	return imu.getData(timestamp);
}

//----------------------ACCESS TO COMPUTED DATA
//CV_32SC1 3x1: x, y, fi
const cv::Mat PositionEstimation::getEstimatedPosition() {
	return state;
}

//----------------------MENAGMENT OF PositionEstimation DEVICES
//Gps
void PositionEstimation::openGps(std::string port) {
	gps.initController(port.c_str(), 9600);
}

void PositionEstimation::closeGps() {
	gps.deinitController();
}

bool PositionEstimation::isGpsOpen() {
	return gps.isOpen();
}

double PositionEstimation::getPosX(double longitude) {
	return gps.getPosX(longitude);
}

double PositionEstimation::getPosLongitude(double X) {
	return gps.getPosLongitude(X);
}

double PositionEstimation::getPosY(double latitude) {
	return gps.getPosY(latitude);
}

double PositionEstimation::getPosLatitude(double X) {
	return gps.getPosLatitude(X);
}

//Imu
void PositionEstimation::openImu(std::string port) {
	imu.openPort(port);
}

void PositionEstimation::closeImu() {
	imu.closePort();
}

bool PositionEstimation::isImuOpen() {
	return imu.isPortOpen();
}

//Encoders
void PositionEstimation::openEncoders(std::string port) {
	encoders.openPort(port, 115200);
}

void PositionEstimation::closeEncoders() {
	encoders.closePort();
}

bool PositionEstimation::isEncodersOpen() {
	return encoders.isPortOpen();
}
