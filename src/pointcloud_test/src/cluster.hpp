#ifndef __CLUSTER_H__
#define __CLUSTER_H__

#include <ros/ros.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <geometry_msgs/PolygonStamped.h>

#include "kalman_filter.h"
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include <tf/tf.h>

class Cluster
{
	private:
	pcl::PointCloud <pcl::PointXYZRGB>::Ptr m_pPointCloud;
	bool m_bIsFirstRegistration;

	public:
	pcl::PointXYZ m_min_point;
	pcl::PointXYZ m_max_point;
	float m_length, m_width, m_height;

	geometry_msgs::PolygonStamped m_polygon;
	geometry_msgs::Pose m_center;
	geometry_msgs::Vector3 m_dimensions;
	double m_dDistanceToCenter;

	std::string m_label;
	int m_id;
	int m_originalID;
	int m_r, m_g, m_b;
	double m_timestamp;

	Eigen::Matrix3f m_eigen_vectors;
	Eigen::Vector3f m_eigen_values;

	KalmanFilter KF;

	bool m_valid_cluster;
	void SetCloud(const pcl::PointCloud<pcl::PointXYZ>::Ptr pInputCloud,
			const std::vector<int>& vecClusterIndices, std_msgs::Header _header, int iClusterNumber, int r,
			int g, int b, std::string label, bool doEstimatePose);


	Cluster();
	virtual ~Cluster();
	/* \brief Sets whether the Cluster is valid or not*/
	void SetValidity(bool in_valid);
	void setPointCloud (pcl::PointCloud<pcl::PointXYZRGB>::Ptr pInputCloud);
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr GetCloud();
	void clear();
	size_t size();
	void SetCluster (long long timestamp, unsigned int iClusterNumber, unsigned int r, unsigned int g, unsigned int b);
	bool& getIsFristRegistration();
	void setIsFirstRegistration(bool isRegistration);
};

typedef boost::shared_ptr<Cluster> clusterPtr;

#endif //__CLUSTER_H__
