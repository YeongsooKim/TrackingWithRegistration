#include "extractMeasurement.h"

ExtractMeasurement::ExtractMeasurement(unsigned int size, bool bDoVisualizePCD) : m_measurementN (size), m_bDoVisualize(bDoVisualizePCD)
{
	// define publisher
	m_pub_result = nh.advertise<pcl::PointCloud<pcl::PointXYZRGB>> ("OnlyBoundingBox", 100);
	m_pub_resultICP = nh.advertise<pcl::PointCloud<pcl::PointXYZRGB>> ("ICP", 100);
	m_pub_shape = nh.advertise<visualization_msgs::MarkerArray>("Shape", 100);
	m_pub_shapeICP = nh.advertise<visualization_msgs::MarkerArray>("ShapeICP", 100);
	m_pub_shapeReference = nh.advertise<visualization_msgs::MarkerArray>("ShapeReference", 100);
	//m_pub_Origin = nh.advertise<visualization_msgs::Marker> ("Origin", 1);

	m_maxIndexNumber = 0;	

	vecOf_measurementCSV.resize (m_measurementN);
	vecOf_accumMeasurementCSV.resize (m_measurementN);

	for (unsigned int measurementIndex = 0; measurementIndex < m_measurementN; measurementIndex++)
	{
		string num = std::to_string(measurementIndex);

		vecOf_measurementCSV[measurementIndex].open ("measurement_" + num + ".csv");
		if (vecOf_measurementCSV[measurementIndex].is_open()){
			vecOf_measurementCSV[measurementIndex] << "timestamp, pose_x, pose_y" << std::endl;
		}

		vecOf_accumMeasurementCSV[measurementIndex].open ("accumulation_measurement_" + num + ".csv");
		if (vecOf_accumMeasurementCSV[measurementIndex].is_open()){
			vecOf_accumMeasurementCSV[measurementIndex] << "timestamp, pose_x, pose_y" << std::endl;
		}
	}

	m_maxIndexNumber = 0;
	m_bDoICP = true;

	m_vecVehicleTrackingClouds.resize(m_measurementN);
	for (unsigned int measurementIndex = 0; measurementIndex < m_measurementN; measurementIndex++)
	{
		clusterPtr pCluster (new Cluster());
		m_vecVehicleAccumulatedCloud.push_back(pCluster);
	}
}

void ExtractMeasurement::setParam()
{
	// set parameter
	nh.param ("extractMeasurement/Marker_duration", m_fMarkerDuration, 0.1);
	nh.param ("extractMeasurement/cluster_err_range", m_dClusterErrRadius, 0.5);
	nh.param ("extractMeasurement/cluster_min_size", m_dClusterMinSize, 15.0);
	nh.param ("extractMeasurement/cluster_max_size", m_dClusterMaxSize, 50.0);
}

void ExtractMeasurement::setData (const std::vector<VectorXd>& vecVecXdRef, long long timestamp)
{
	m_vecVecXdRef = vecVecXdRef;
	m_llTimestamp_s = timestamp;

	VectorXd vecXdRef = m_vecVecXdRef.back();
	geometry_msgs::Pose geomsgRefPose;
	geomsgRefPose.position.x = vecXdRef[0];
	geomsgRefPose.position.y = vecXdRef[1];
	m_geomsgReferences.poses.push_back (geomsgRefPose);
}

void ExtractMeasurement::process()
{
	// get pcd 
	pcl::PointCloud<pcl::PointXYZ>::Ptr pCloudTraffic (new pcl::PointCloud<pcl::PointXYZ>);
	getPCD(pCloudTraffic);

	// downsample
	pcl::PointCloud<pcl::PointXYZ>::Ptr pDownsampledCloud (new pcl::PointCloud<pcl::PointXYZ>);
	downsample(pCloudTraffic, pDownsampledCloud, 0.1);

	// dbscan
	std::vector<pcl::PointIndices> vecClusterIndices;
	dbscan (pDownsampledCloud, vecClusterIndices);

	// Set cluster pointcloud from clusterIndices and coloring
	setCluster (vecClusterIndices, pDownsampledCloud);

	// Associate 
	association ();

	// calculate RMSE
	calculateRMSE ();

	// display shape
	displayShape ();

	// publish	
	publish ();
}


void ExtractMeasurement::downsample (const pcl::PointCloud<pcl::PointXYZ>::Ptr& pInputCloud, pcl::PointCloud<pcl::PointXYZ>::Ptr& pDownsampledCloud, float f_paramLeafSize_m)
{
	// Voxel length of the corner : fLeafSize
	pcl::VoxelGrid<pcl::PointXYZ> voxelFilter;
	voxelFilter.setInputCloud (pInputCloud);
	voxelFilter.setLeafSize(f_paramLeafSize_m, f_paramLeafSize_m, f_paramLeafSize_m);
	voxelFilter.filter (*pDownsampledCloud);
}

void ExtractMeasurement::downsample (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& pInputCloud, pcl::PointCloud<pcl::PointXYZRGB>::Ptr& pDownsampledCloud, float f_paramLeafSize_m)
{
	// Voxel length of the corner : fLeafSize
	pcl::VoxelGrid<pcl::PointXYZRGB> voxelFilter;
	voxelFilter.setInputCloud (pInputCloud);
	voxelFilter.setLeafSize(f_paramLeafSize_m, f_paramLeafSize_m, f_paramLeafSize_m);
	voxelFilter.filter (*pDownsampledCloud);
}


void ExtractMeasurement::getPCD (pcl::PointCloud<pcl::PointXYZ>::Ptr& pCloudTraffic)
{
	if(m_bDoVisualize)
	{
		pCloudTraffic = loadPCD("/workspace/TrackingWithRegistration/src/tracking_with_registration/src/sensors/data/pcd/highway_"+std::to_string(m_llTimestamp_s)+".pcd");
	}
}


void ExtractMeasurement::dbscan(const pcl::PointCloud<pcl::PointXYZ>::Ptr& pInputCloud, std::vector<pcl::PointIndices>& vecClusterIndices)
{
	//////////////////////////////////////////////////////////////////
	// DBSCAN

	// Creating the KdTree object for the search method of the extraction
	pcl::search::KdTree<pcl::PointXYZ>::Ptr pKdtreeDownsampledCloud (new pcl::search::KdTree<pcl::PointXYZ>);
	pKdtreeDownsampledCloud->setInputCloud (pInputCloud);

	pcl::EuclideanClusterExtraction<pcl::PointXYZ> euclideanCluster;
	euclideanCluster.setClusterTolerance (1.5);
	euclideanCluster.setMinClusterSize (50);
	euclideanCluster.setMaxClusterSize (23000);
	//	euclideanCluster.setClusterTolerance (m_dClusterErrRadius);
	//	euclideanCluster.setMinClusterSize (m_dClusterMinSize);
	//	euclideanCluster.setMaxClusterSize (m_dClusterMaxSize);
	euclideanCluster.setSearchMethod (pKdtreeDownsampledCloud);
	euclideanCluster.setInputCloud (pInputCloud);
	euclideanCluster.extract (vecClusterIndices);
}

void ExtractMeasurement::setCluster (const std::vector<pcl::PointIndices> vecClusterIndices, const pcl::PointCloud<pcl::PointXYZ>::Ptr pInputCloud)
{
	m_OriginalClusters.clear();

	int objectNumber = 0;
	for (const auto& clusterIndice : vecClusterIndices)
	{
		std::string label_ = "vehicle";
		std::string label = label_;
		label.append (std::to_string(objectNumber));

		// generate random color to globalRGB variable
		generateColor(vecClusterIndices.size());

		// pCluster is a local cluster
		clusterPtr pCluster (new Cluster());

		std_msgs::Header dummy;
		dummy.frame_id = "map";
		dummy.stamp = ros::Time(m_llTimestamp_s/1e6);

		// Cloring and calculate the cluster center point and quaternion
		pCluster->SetCloud(pInputCloud, clusterIndice.indices, dummy, objectNumber, m_globalRGB[objectNumber].m_r, m_globalRGB[objectNumber].m_g, m_globalRGB[objectNumber].m_b, label, true);

		m_OriginalClusters.push_back(pCluster);

		objectNumber++;
	}
}


// generate random color
void ExtractMeasurement::generateColor(size_t indexNumber)
{
	if (indexNumber > m_maxIndexNumber)
	{
		int addRGB = indexNumber - m_maxIndexNumber;
		m_maxIndexNumber = indexNumber;

		for (size_t i = 0; i < addRGB; i++)
		{
			uint8_t r = 1024 * rand () % 255;
			uint8_t g = 1024 * rand () % 255;
			uint8_t b = 1024 * rand () % 255;
			m_globalRGB.push_back(RGB(r, g, b));
		}
	}
}

void ExtractMeasurement::association()
{
	static bool bIsFirst = true;
	m_ObstacleTracking.association(m_OriginalClusters);

	for (auto pCluster : m_ObstacleTracking.m_TrackingObjects)
	{
		vecOf_measurementCSV[(pCluster->m_id)-1] << pCluster->m_timestamp << "," 
			<< pCluster->m_center.position.x << "," << pCluster->m_center.position.y << std::endl;
	}

	VectorXd meas(2);
	meas << m_ObstacleTracking.m_TrackingObjects[0]->m_center.position.x, m_ObstacleTracking.m_TrackingObjects[0]->m_center.position.y;
	m_vecVecXdOnlyBoundingbox.push_back (meas);

	// Store data into vector of vehicles tracking cloud
	for (unsigned int vehicleIndex = 0; vehicleIndex < m_measurementN; vehicleIndex++)
	{
		for (unsigned int objectIndex = 0; objectIndex < m_measurementN; objectIndex++)
		{
			if ((m_ObstacleTracking.m_TrackingObjects[objectIndex]->m_id-1) == vehicleIndex)
			{
				pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTrackingCloud (m_ObstacleTracking.m_TrackingObjects[objectIndex]->GetCloud());
				m_vecVehicleTrackingClouds[vehicleIndex].push_back (pTrackingCloud);

				break;
			}
		}
	}

	// ICP
	if (m_bDoICP)
	{
		// Init target
		if (bIsFirst)
		{
			for (unsigned int vehicleIndex = 0; vehicleIndex < m_measurementN; vehicleIndex++)
			{
				*(m_vecVehicleAccumulatedCloud[vehicleIndex]->GetCloud()) += *m_vecVehicleTrackingClouds[vehicleIndex].back();

				unsigned int r; unsigned int g; unsigned int b;
				if (vehicleIndex == 0) {
					r = 255; g = 0; b = 0;
				}
				else if (vehicleIndex == 1) {
					r = 0; g = 255; b = 0;
				}
				else if (vehicleIndex == 2) {
					r = 0; g = 0; b = 255;
				}
				m_vecVehicleAccumulatedCloud[vehicleIndex]->SetCluster (m_llTimestamp_s, vehicleIndex, r, g, b);
			}

			bIsFirst = false;
		}
		// ICP 
		else
		{
			point2pointICP();
		}

		for (auto pCluster : m_vecVehicleAccumulatedCloud)
		{
			vecOf_accumMeasurementCSV[pCluster->m_id] << pCluster->m_timestamp << "," 
				<< pCluster->m_center.position.x << "," << pCluster->m_center.position.y << std::endl;
		}

		VectorXd meas2 (2);
		meas2 << m_vecVehicleAccumulatedCloud[0]->m_center.position.x,  m_vecVehicleAccumulatedCloud[0]->m_center.position.y;
		m_vecVecXdRegistrationAccum.push_back (meas2);
	}
}

void ExtractMeasurement::point2pointICP ()
{
	unsigned int vehicleN = 0;
	for (const auto& vecVehicleTrackingCloud : m_vecVehicleTrackingClouds)
	{
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr pSourceCloud (m_vecVehicleAccumulatedCloud[vehicleN]->GetCloud());
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTargetCloud (vecVehicleTrackingCloud.back());

		pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
		icp.setInputSource (pSourceCloud);
		icp.setInputTarget (pTargetCloud);
		pcl::PointCloud<pcl::PointXYZRGB> Final;
		icp.align (Final);

//		if (icp.getFitnessScore() < 0.02)
//		{
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTmpPointCloud (new pcl::PointCloud<pcl::PointXYZRGB>);
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTmpPointCloud2 (new pcl::PointCloud<pcl::PointXYZRGB>);
			*pTmpPointCloud += Final;
			*pTmpPointCloud += *pTargetCloud;

			downsample (pTmpPointCloud, pTmpPointCloud2, 0.09);
			m_vecVehicleAccumulatedCloud[vehicleN]->clear ();
			m_vecVehicleAccumulatedCloud[vehicleN]->setPointCloud (pTmpPointCloud2);
//		}
//
//		else {
//			*(m_vecVehicleAccumulatedCloud[vehicleN]->GetCloud()) = *pTargetCloud;
//		}


		//		pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTmpPointCloud (new pcl::PointCloud<pcl::PointXYZRGB>);
		//		pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTmpPointCloud2 (new pcl::PointCloud<pcl::PointXYZRGB>);
		//		*pTmpPointCloud += Final;
		//		*pTmpPointCloud += *pTargetCloud;
		//
		//		downsample (pTmpPointCloud, pTmpPointCloud2, 0.09);
		//		m_vecVehicleAccumulatedCloud[vehicleN]->clear ();
		//		m_vecVehicleAccumulatedCloud[vehicleN]->setPointCloud (pTmpPointCloud2);


		unsigned int r; unsigned int g; unsigned int b;
		if (vehicleN == 0) {
			r = 255; g = 0; b = 0;
		}
		else if (vehicleN == 1) {
			r = 0; g = 255; b = 0;
		}
		else if (vehicleN == 2) {
			r = 0; g = 0; b = 255;
		}
		m_vecVehicleAccumulatedCloud[vehicleN]->SetCluster (m_llTimestamp_s, vehicleN, r, g, b);
		vehicleN++;
	}
}

void ExtractMeasurement::displayShape ()
{
	// Tracking objects
	m_arrShapes.markers.clear();
	m_arrShapesICP.markers.clear();
	m_arrShapesReference.markers.clear();

	// For OnlyBoundingBox
	for (auto pCluster : m_ObstacleTracking.m_TrackingObjects)
	{
		visualization_msgs::Marker shape;

		shape.lifetime = ros::Duration();
		shape.header.frame_id = "map";
		shape.header.stamp = ros::Time(pCluster->m_timestamp);
		shape.id = pCluster->m_id;

		// bounding box
		shape.type = visualization_msgs::Marker::CUBE;
		shape.action = visualization_msgs::Marker::ADD;
		shape.ns = "/OnlyBoundingBox";

		shape.pose.position = pCluster->m_center.position;
		shape.pose.orientation = pCluster->m_center.orientation;

		shape.scale.x = pCluster->m_dimensions.x;
		shape.scale.y = pCluster->m_dimensions.y;
		shape.scale.z = pCluster->m_dimensions.z;

		shape.color.r = pCluster->m_r/255.0f;
		shape.color.g = pCluster->m_g/255.0f;
		shape.color.b = pCluster->m_b/255.0f;
		shape.color.a = 0.5;

		m_arrShapes.markers.push_back(shape);

		// text
		shape.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
		shape.ns = "/vehicle number";

		shape.points.clear();
		shape.pose.position = pCluster->m_center.position;
		shape.pose.orientation = pCluster->m_center.orientation;

		shape.scale.x = 1.0;
		shape.scale.y = 1.0;
		shape.scale.z = 1.0;

		shape.color.r = shape.color.g = shape.color.b = 1.0;
		shape.color.a = 1.0;

		shape.text = std::to_string(pCluster->m_id);

		m_arrShapes.markers.push_back (shape);

		// text
		string s_x_RMSE = std::to_string(m_vecVecXdResultRMSE[0][0]);
		string s_y_RMSE = std::to_string(m_vecVecXdResultRMSE[0][1]);
		string sWholeText = "OnlyBoundingBox RMSE\r\nx: " + s_x_RMSE + "\r\ny: " + s_y_RMSE;

		shape.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
		shape.ns = "/RMSE";

		shape.points.clear();
		shape.pose.position.x = 0;
		shape.pose.position.y = 30;
		shape.pose.position.z = 0;
		shape.pose.orientation.x = 0.0;
		shape.pose.orientation.y = 0.0;
		shape.pose.orientation.z = 0.0;
		shape.pose.orientation.w = 1.0;

		shape.scale.x = 2.0;
		shape.scale.y = 2.0;
		shape.scale.z = 2.0;

		shape.color.r = shape.color.g = shape.color.b = 1.0;
		shape.color.a = 1.0;

		shape.text = sWholeText;

		m_arrShapes.markers.push_back (shape);

		// center point using sphere
		shape.type = visualization_msgs::Marker::SPHERE;
		shape.action = visualization_msgs::Marker::ADD;
		shape.ns = "/center point";

		shape.points.clear();
		shape.pose.position = pCluster->m_center.position;
		shape.pose.orientation = pCluster->m_center.orientation;

		shape.scale.x = 0.5;
		shape.scale.y = 0.5;
		shape.scale.z = 0.5;

		shape.color.r = 1.0;
		shape.color.g = 1.0;
		shape.color.b = 0.0;
		shape.color.a = 0.5;

		m_arrShapes.markers.push_back (shape);
	}

	// For registration and accumulation
	for (auto pCluster : m_vecVehicleAccumulatedCloud)
	{
		visualization_msgs::Marker shape;

		shape.lifetime = ros::Duration();
		shape.header.frame_id = "map";
		shape.header.stamp = ros::Time(pCluster->m_timestamp);
		shape.id = (pCluster->m_id)+1;

		// bounding box
		shape.type = visualization_msgs::Marker::CUBE;
		shape.action = visualization_msgs::Marker::ADD;
		shape.ns = "/Registration&accumulation";

		shape.pose.position = pCluster->m_center.position;
		shape.pose.orientation = pCluster->m_center.orientation;

		shape.scale.x = pCluster->m_dimensions.x;
		shape.scale.y = pCluster->m_dimensions.y;
		shape.scale.z = pCluster->m_dimensions.z;

		shape.color.r = pCluster->m_r/254.0f;
		shape.color.g = pCluster->m_g/254.0f;
		shape.color.b = pCluster->m_b/254.0f;
		shape.color.a = 0.5;

		m_arrShapesICP.markers.push_back(shape);

		// text
		shape.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
		shape.ns = "/vehicle number";

		shape.points.clear();
		shape.pose.position = pCluster->m_center.position;
		shape.pose.orientation = pCluster->m_center.orientation;

		shape.scale.x = 1.0;
		shape.scale.y = 1.0;
		shape.scale.z = 1.0;

		shape.color.r = shape.color.g = shape.color.b = 1.0;
		shape.color.a = 1.0;

		shape.text = std::to_string(shape.id);

		m_arrShapesICP.markers.push_back (shape);

		// text
		string s_x_RMSE = std::to_string(m_vecVecXdResultRMSE[1][0]);
		string s_y_RMSE = std::to_string(m_vecVecXdResultRMSE[1][1]);
		string sWholeText = "Registraton and Accumulation RMSE\r\nx: " + s_x_RMSE + "\r\ny: " + s_y_RMSE;

		shape.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
		shape.ns = "/RMSE";

		shape.points.clear();
		shape.pose.position.x = 0;
		shape.pose.position.y = 20;
		shape.pose.position.z = 0;
		shape.pose.orientation.x = 0.0;
		shape.pose.orientation.y = 0.0;
		shape.pose.orientation.z = 0.0;
		shape.pose.orientation.w = 1.0;

		shape.scale.x = 2.0;
		shape.scale.y = 2.0;
		shape.scale.z = 2.0;

		shape.color.r = shape.color.g = shape.color.b = 1.0;
		shape.color.a = 1.0;

		shape.text = sWholeText;

		m_arrShapesICP.markers.push_back (shape);

		// center point using sphere
		shape.type = visualization_msgs::Marker::SPHERE;
		shape.action = visualization_msgs::Marker::ADD;
		shape.ns = "/center point";

		shape.points.clear();
		shape.pose.position = pCluster->m_center.position;
		shape.pose.orientation = pCluster->m_center.orientation;

		shape.scale.x = 0.5;
		shape.scale.y = 0.5;
		shape.scale.z = 0.5;

		shape.color.r = 0.0;
		shape.color.g = 1.0;
		shape.color.b = 1.0;
		shape.color.a = 0.5;

		m_arrShapesICP.markers.push_back (shape);
	}

	// For reference
	{
		visualization_msgs::Marker shape;

		// Line stript
		shape.lifetime = ros::Duration();
		shape.header.frame_id = "map";
		shape.header.stamp = ros::Time::now();
		shape.id = 0;

		shape.type = visualization_msgs::Marker::LINE_STRIP;
		shape.action = visualization_msgs::Marker::ADD;
		shape.ns = "/Trajectory";

		shape.pose.orientation.x = 0.0;
		shape.pose.orientation.y = 0.0;
		shape.pose.orientation.z = 0.0;
		shape.pose.orientation.w = 1.0;

		shape.scale.x = 0.09; 
		shape.scale.y = 0.09; 
		shape.scale.z = 0.09;

		shape.color.r = 1.0;
		shape.color.g = 0.0;
		shape.color.b = 0.0;
		shape.color.a = 1;

		for (const auto& pose : m_geomsgReferences.poses)
		{
			geometry_msgs::Point tmp;
			tmp.x = pose.position.x;
			tmp.y = pose.position.y;
			tmp.z = pose.position.z;
			shape.points.push_back(tmp);
		}

		m_arrShapesReference.markers.push_back (shape);

		// End point
		geometry_msgs::Pose geomsgEndPoint(m_geomsgReferences.poses.back());

		shape.type = visualization_msgs::Marker::SPHERE;
		shape.action = visualization_msgs::Marker::ADD;
		shape.ns = "/End point";

		shape.pose.position = geomsgEndPoint.position;
		shape.pose.orientation.x = 0.0;
		shape.pose.orientation.y = 0.0;
		shape.pose.orientation.z = 0.0;
		shape.pose.orientation.w = 1.0;

		shape.scale.x = 0.5; 
		shape.scale.y = 0.5; 
		shape.scale.z = 0.5;

		shape.color.r = 1.0;
		shape.color.g = 0.0;
		shape.color.b = 0.0;
		shape.color.a = 1;

		m_arrShapesReference.markers.push_back(shape);
	}
}

void ExtractMeasurement::publish ()
{
	// Accumulate all cluster to pAccumulationCloud
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr pAccumulationCloud (new pcl::PointCloud<pcl::PointXYZRGB>);
	pAccumulationCloud->header.frame_id = "map";

	// accumulation for publish
	for (const auto& pCluster : m_ObstacleTracking.m_TrackingObjects)
		*pAccumulationCloud += *(pCluster->GetCloud());

	// Accumulate all cluster to pAccumCloudForICP
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr pAccumCloudForICP (new pcl::PointCloud<pcl::PointXYZRGB>);
	pAccumCloudForICP->header.frame_id = "map";
	//
	// accumulation for publish
	for (const auto& pVehicleTrackingCloud : m_vecVehicleAccumulatedCloud)
		*pAccumCloudForICP += *(pVehicleTrackingCloud->GetCloud());


	// publish
	m_pub_resultICP.publish (*pAccumCloudForICP);
	m_pub_result.publish (*pAccumulationCloud);
	m_pub_shapeReference.publish (m_arrShapesReference);
	m_pub_shapeICP.publish (m_arrShapesICP);
	m_pub_shape.publish (m_arrShapes);
}

void ExtractMeasurement::savePCD (const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& pInputCloud)
{
	pcl::io::savePCDFile ("ICP_test.pcd", *pInputCloud);
}


pcl::PointCloud<pcl::PointXYZ>::Ptr ExtractMeasurement::loadPCD (std::string file)
{
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);

	if (pcl::io::loadPCDFile<pcl::PointXYZ> (file, *cloud) == -1) //* load the file
	{
		PCL_ERROR ("Couldn't read file \n");
	}
	//std::cerr << "Loaded " << cloud->points.size () << " data points from "+file << std::endl;

	return cloud;
}


void ExtractMeasurement::calculateRMSE ()
{
	m_vecVecXdResultRMSE.clear();

	VectorXd vOnlyBoundingBoxRMSE(2);
	vOnlyBoundingBoxRMSE << 0,0;

	for (unsigned int measIndex = 0; measIndex < m_vecVecXdRef.size(); measIndex++)
	{
		VectorXd residual = m_vecVecXdRef[measIndex] - m_vecVecXdOnlyBoundingbox[measIndex];
		residual = residual.array() * residual.array();
		vOnlyBoundingBoxRMSE += residual;
	}
	vOnlyBoundingBoxRMSE = vOnlyBoundingBoxRMSE/m_vecVecXdRef.size();
	vOnlyBoundingBoxRMSE = vOnlyBoundingBoxRMSE.array().sqrt();

	m_vecVecXdResultRMSE.push_back (vOnlyBoundingBoxRMSE);


	VectorXd vRegistrationAccumRMSE(2);
	vRegistrationAccumRMSE << 0,0;

	for (unsigned int measIndex = 0; measIndex < m_vecVecXdRef.size(); measIndex++)
	{
		VectorXd residual = m_vecVecXdRef[measIndex] - m_vecVecXdRegistrationAccum[measIndex];
		residual = residual.array() * residual.array();
		vRegistrationAccumRMSE += residual;
	}
	vRegistrationAccumRMSE = vRegistrationAccumRMSE/m_vecVecXdRef.size();
	vRegistrationAccumRMSE = vRegistrationAccumRMSE.array().sqrt();

	m_vecVecXdResultRMSE.push_back (vRegistrationAccumRMSE);
}
