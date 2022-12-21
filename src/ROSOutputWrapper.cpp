#include "ROSOutputWrapper.h"
#include <FullSystem/HessianBlocks.h>
#include <util/FrameShell.h>

using namespace dso;

dso::ROSOutputWrapper::ROSOutputWrapper()
    : nh("stereo_dso"),
      global_cloud(new PointCloudXYZ),
      //      reference_cloud(new PointCloudXYZ),
      lastTimestamp(0.0)
{
    dsoOdomHighFreqPublisher = nh.advertise<nav_msgs::Odometry>("pose_hf", 10);
    dsoOdomLowFreqPublisher = nh.advertise<nav_msgs::Odometry>("pose_lf", 10);
    dsoLocalPointCloudPublisher = nh.advertise<sensor_msgs::PointCloud2>("local_point_cloud", 1);
    //    dsoGlobalPointCloudPublisher = nh.advertise<sensor_msgs::PointCloud2>("global_point_cloud", 1);
    //    dsoReferencePointCloudPublisher = nh.advertise<sensor_msgs::PointCloud2>("reference_point_cloud", 1);
    //    dsoLocReferencePointCloudPublisher = nh.advertise<sensor_msgs::PointCloud2>("loc_reference_point_cloud", 1, true);

    //    dsoImagePublisher = nh.advertise<sensor_msgs::Image>("/dso/image_undistort", 10);

    //    dsoGlobalCloudSaver = nh.advertiseService<std_srvs::Empty::Request,
    //                                              std_srvs::Empty::Response>("/dso/save_global_cloud", srv_cbk);

    ros::param::get("distanceThreshold", distanceThreshold);
    ros::param::get("probability", probability);
    ros::param::get("maxIterations", maxIterations);
    std::cout << "distanceThreshold: " << distanceThreshold << std::endl;
    std::cout << "probability: " << probability << std::endl;
    std::cout << "maxIterations: " << maxIterations << std::endl;

    ros::param::get("activeRadiusSearch", activeRadiusSearch);
    ros::param::get("activeMinNeighborsInRadius", activeMinNeighborsInRadius);
    std::cout << "activeRadiusSearch: " << activeRadiusSearch << std::endl;
    std::cout << "activeMinNeighborsInRadius: " << activeMinNeighborsInRadius << std::endl;

    ros::param::get("marginRadiusSearch", marginRadiusSearch);
    ros::param::get("marginMinNeighborsInRadius", marginMinNeighborsInRadius);
    std::cout << "marginRadiusSearch: " << marginRadiusSearch << std::endl;
    std::cout << "marginMinNeighborsInRadius: " << marginMinNeighborsInRadius << std::endl;

    ros::param::get("meanK", meanK);
    ros::param::get("stddevMulThresh", stddevMulThresh);
    std::cout << "meanK: " << meanK << std::endl;
    std::cout << "stddevMulThresh: " << stddevMulThresh << std::endl;

    ros::param::get("minNumPointsToSend", minNumPointsToSend);
    ros::param::get("useRANSAC", useRANSAC);

    std::cout << "minNumPointsToSend: " << minNumPointsToSend << std::endl;
    std::cout << "useRANSAC: " << useRANSAC << std::endl;

    //    ros::param::get("useReferenceCloud", useReferenceCloud);
    //    std::cout << "useReferenceCloud: " << useReferenceCloud << std::endl;
    //    ros::param::get("referenceCloudPath", referenceCloudPath);
    //    std::cout << "referenceCloudPath: " << referenceCloudPath << std::endl;

    //    pcl::io::loadPCDFile(referenceCloudPath, loc_reference_cloud);
    //    if (useReferenceCloud)
    //        publish_reference_cloud();

    poseBuf.clear();
    localPointsBuf.clear();
    globalPointsBuf.clear();

    margin_cloud_window.clear();

    referencePointsBuf.clear();

    e_Toc.block<3, 3>(0, 0) = Eigen::Quaterniond(e_Toc.block<3, 3>(0, 0))
                                      .normalized()
                                      .toRotationMatrix();
    Toc = Sophus::SE3d(e_Toc);

    Tmir = Sophus::SE3d(e_mir_Y * e_mir_Z);
    Tmir.setRotationMatrix((e_mir_Y * e_mir_Z).block<3, 3>(0, 0));

    Sophus::SO3d R1 = Sophus::SO3d((e_mir_Y * e_mir_Z).block<3, 3>(0, 0));
}

void ROSOutputWrapper::publish_reference_cloud()
{
    sensor_msgs::PointCloud2 cloud_msg;
    pcl::toROSMsg(loc_reference_cloud, cloud_msg);
    cloud_msg.header.frame_id = "map";
    cloud_msg.header.stamp = ros::Time::now();

    dsoLocReferencePointCloudPublisher.publish(cloud_msg);
    ROS_INFO("Publish reference cloud as latched");
}

void setMsgFromSE3(geometry_msgs::Pose &poseMsg, const Sophus::SE3d &pose)
{
    poseMsg.position.x = pose.translation()[0];
    poseMsg.position.y = pose.translation()[1];
    poseMsg.position.z = pose.translation()[2];
    poseMsg.orientation.x = pose.so3().unit_quaternion().x();
    poseMsg.orientation.y = pose.so3().unit_quaternion().y();
    poseMsg.orientation.z = pose.so3().unit_quaternion().z();
    poseMsg.orientation.w = pose.so3().unit_quaternion().w();
}

void setTfFromSE3(geometry_msgs::Transform &tfMsg, const Sophus::SE3d &pose)
{
    tfMsg.translation.x = pose.translation()[0];
    tfMsg.translation.y = pose.translation()[1];
    tfMsg.translation.z = pose.translation()[2];
    tfMsg.rotation.x = pose.so3().unit_quaternion().x();
    tfMsg.rotation.y = pose.so3().unit_quaternion().y();
    tfMsg.rotation.z = pose.so3().unit_quaternion().z();
    tfMsg.rotation.w = pose.so3().unit_quaternion().w();
}

Sophus::SE3d transformPointFixedScale(const Sophus::SE3d &pose,
                                      Eigen::Vector3d &point_cam)
{
    // TODO: we used sim(3) here
    Sophus::SE3d T_w_cam = pose.inverse();
    Sophus::Vector3d t_cam_point(point_cam);
    Sophus::SO3d R_cam_point;
    Sophus::SE3d T_W_point;

    T_W_point = T_w_cam * Sophus::SE3d(R_cam_point, t_cam_point);

    return T_W_point;
}

void ROSOutputWrapper::publishOutput()
{
    if (poseBuf.empty() || localPointsBuf.empty())
    {
        return;
    }

    // TODO: how the mutex affects FILLING the buffers?
    //    ROS_WARN("Good buffer");
    std::unique_ptr<nav_msgs::Odometry> dso_pose = nullptr;
    poseMutex.lock();
    while (poseBuf.size() > 1)
        poseBuf.pop_front();
    dso_pose = std::make_unique<nav_msgs::Odometry>(poseBuf.front());
    dsoOdomLowFreqPublisher.publish(*dso_pose);
    poseBuf.clear();
    poseMutex.unlock();

    if (dso_pose == nullptr)
    {
        ROS_WARN("Locked the pose. Nothing to publish");
        return;
    }

    sensor_msgs::PointCloud2 dso_local_cloud;
    pclMutex.lock();

    while (localPointsBuf.size() > 1)
        localPointsBuf.pop_front();
    dso_local_cloud = localPointsBuf.front();
    dso_local_cloud.header.stamp = dso_pose->header.stamp;
    dsoLocalPointCloudPublisher.publish(dso_local_cloud);
    localPointsBuf.clear();

    pclMutex.unlock();
}

void ROSOutputWrapper::publishInitSignal()
{
    if (!isInitialized)
    {
        ROS_INFO("System is initialized. Start publishing poses");
        isInitialized = true;
    }
}

void ROSOutputWrapper::publishCamPose(dso::FrameShell *frame, dso::CalibHessian *HCalib)
{

    ros::Time ros_ts;
    ros_ts.fromSec(frame->timestamp);
    lastTimestamp = frame->timestamp;

    auto &camToWorld = frame->camToWorld;

    //    std::cout << "camToWorld: " << camToWorld.matrix() << std::endl;
    // camToWorld to eigen  matrxi
    // mult e_mir
    // nback to sophus
    //    Eigen::Matrix4d e_camToWorld = camToWorld.matrix();
    //    Eigen::Matrix4d e_camToWorld_mir = e_camToWorld * e_mir;
    //    camToWorld = Sophus::SE3d(e_camToWorld_mir);

    //    std::cout << "e_camToWorld: " << e_camToWorld << std::endl;
    //    std::cout << "e_camToWorld_mir: " << e_camToWorld_mir << std::endl;
    //    std::cout << "camToWorld: " << camToWorld.matrix() << std::endl;

    {
        std::unique_lock<std::mutex> lk(mutex);
        if (!isInitialized)
            return;

        nav_msgs::Odometry metricOdomMsg;
        metricOdomMsg.header.stamp = ros_ts;
        metricOdomMsg.header.frame_id = "odom";
        metricOdomMsg.child_frame_id = "zed2_camera_frame";

        mCamToWorld = camToWorld;
        setMsgFromSE3(metricOdomMsg.pose.pose, Tmir * Toc * camToWorld * Toc.inverse() * Tmir.inverse());

        geometry_msgs::TransformStamped tf_cam;
        tf_cam.header = metricOdomMsg.header;
        tf_cam.header.frame_id = "odom";
        tf_cam.child_frame_id = "zed2_camera_frame";
        setTfFromSE3(tf_cam.transform, Tmir * Toc * camToWorld * Toc.inverse() * Tmir.inverse());

        {
            std::unique_lock<std::mutex> mtx(poseMutex);
            poseBuf.push_back(metricOdomMsg);
        }
        dsoOdomHighFreqPublisher.publish(metricOdomMsg);
        dsoWcamBr.sendTransform(tf_cam);
    }
}

void ROSOutputWrapper::pushLiveFrame(dso::FrameHessian *image)
{
    //    cv_bridge::CvImage cvImage;
    //    cvImage.encoding = "mono8";
    //
    //    int cols = 848;
    //    int rows = 800;
    //    unsigned char *img_data = new unsigned char[cols * rows];
    //    for (int i = 0; i < cols * rows; i++)
    //        img_data[i] = image->dI[i][0] * 0.8 > 255.0f ? 255.0 : image->dI[i][0] * 0.8;
    //    cv::Mat cv_mat_image(rows, cols, CV_8UC1, &img_data[0]);
    //    cvImage.image = cv_mat_image;
    //
    //    sensor_msgs::Image imageMsg;
    //    cvImage.toImageMsg(imageMsg);
    //    delete[] img_data;
    //
    //    ros::Time ros_ts;
    //    ros_ts.fromSec(image->shell->timestamp);
    //    imageMsg.header.stamp = ros_ts;
    //    dsoImagePublisher.publish(imageMsg);
}

void ROSOutputWrapper::publishKeyframes(std::vector<dso::FrameHessian *> &frames,
                                        bool final,
                                        dso::CalibHessian *HCalib)
{
    if (!isInitialized)
        return;

    float fx = HCalib->fxl();
    float fy = HCalib->fyl();
    float cx = HCalib->cxl();
    float cy = HCalib->cyl();

    float fxi = 1 / fx;
    float fyi = 1 / fy;
    float cxi = -cx / fx;
    float cyi = -cy / fy;

    sensor_msgs::PointCloud2 msg_local_cloud, msg_global_cloud, msg_reference_cloud;
    //    PointCloudXYZ::Ptr local_cloud_cam(new PointCloudXYZ);
    //    PointCloudXYZ::Ptr local_cloud_world(new PointCloudXYZ);
    //    PointCloudXYZ::Ptr active_local_cloud_cam(new PointCloudXYZ);
    PointCloudXYZ::Ptr active_local_cloud_world(new PointCloudXYZ);
    PointCloudXYZ::Ptr margin_local_cloud(new PointCloudXYZ);

    pcl::PCLPointCloud2::Ptr active_local_cloud_world_2(new pcl::PCLPointCloud2);
    pcl::PCLPointCloud2::Ptr margin_local_cloud_2(new pcl::PCLPointCloud2);

    long int npointsHessians = 0;
    long int npointsHessiansMarginalized = 0;
    double timestamp = 0.0;

    ROS_WARN("Start accumulating points");
    {
        std::unique_lock<std::mutex> lk(mutex);
        for (dso::FrameHessian *fh: frames)
        {
            npointsHessians += fh->pointHessians.size();
            npointsHessiansMarginalized += fh->pointHessiansMarginalized.size();

            for (dso::PointHessian *ph: fh->pointHessians)
            {
                Eigen::Vector3d pos_cam, pos_world, pos_metric_cam;

                // [sx, sy, s]
                float idpeth = ph->idepth_scaled;
                float idepth_hessian = ph->idepth_hessian;
                float relObsBaseline = ph->maxRelBaseline;

                if (idpeth < 0) continue;

                float depth = (1.0f / idpeth);
                float depth4 = depth * depth;
                depth4 *= depth4;
                float var = (1.0f / (idepth_hessian + 0.01));

                if (var * depth4 > scaledTH)
                    continue;

                if (var > absTH)
                    continue;

                if (relObsBaseline < minRelBS)
                    continue;

                // TODO: we used fixed scaling here
                pos_cam[0] = (ph->u * fxi + cxi) * depth;
                pos_cam[1] = (ph->v * fyi + cyi) * depth;
                pos_cam[2] = depth * (1 + 2 * fxi * (rand() / (float) RAND_MAX - 0.5f));

                auto &camToWorld = fh->shell->camToWorld;
                Sophus::SE3d fixedScalePointToWorld(transformPointFixedScale(camToWorld.inverse(),
                                                                             pos_cam));

                Eigen::Matrix<double, 4, 4> e_fixedScalePointToWorld = fixedScalePointToWorld.matrix();

                for (int i = 0; i < 3; i++)
                    pos_world[i] = e_fixedScalePointToWorld(i, 3);

                // TODO: make for cam points

                //                Sophus::SE3d fixedScalePointToCam(invTransformPointFixedScale(mCamToWorld.inverse().matrix(),
                //                                                                              pos_world));

                // If you don't want to use the optical transform, then remove Toc from here
                //                Eigen::Matrix<double, 4, 4> e_fixedScalePointToCam = Toc.matrix() * fixedScalePointToCam.matrix();
                //                for (int i = 0; i < 3; i++)
                //                {
                //                    pos_metric_cam[i] = e_fixedScalePointToCam(i, 3);
                //                }

                pcl::PointXYZ point_world;
                point_world.x = pos_world(0);
                point_world.y = pos_world(1);
                point_world.z = pos_world(2);

                //                pcl::PointXYZ point_metric_cam;
                //                point_metric_cam.x = pos_metric_cam(0);
                //                point_metric_cam.y = pos_metric_cam(1);
                //                point_metric_cam.z = pos_metric_cam(2);

                active_local_cloud_world->push_back(point_world);
                ROS_WARN("Accumulated active points");
            }

            for (dso::PointHessian *phm: fh->pointHessiansMarginalized)
            {
                Eigen::Vector3d pos_cam, pos_world, pos_metric_cam;

                // [sx, sy, s]
                float idpeth = phm->idepth_scaled;
                float idepth_hessian = phm->idepth_hessian;
                float relObsBaseline = phm->maxRelBaseline;

                if (idpeth < 0) continue;

                float depth = (1.0f / idpeth);
                float depth4 = depth * depth;
                depth4 *= depth4;
                float var = (1.0f / (idepth_hessian + 0.01));

                if (var * depth4 > scaledTH)
                    continue;

                if (var > absTH)
                    continue;

                if (relObsBaseline < minRelBS)
                    continue;

                // TODO: we used fixed scaling here
                pos_cam[0] = (phm->u * fxi + cxi) * depth;
                pos_cam[1] = (phm->v * fyi + cyi) * depth;
                pos_cam[2] = depth * (1 + 2 * fxi * (rand() / (float) RAND_MAX - 0.5f));

                auto &camToWorld = fh->shell->camToWorld;
                Sophus::SE3d fixedScalePointToWorld(transformPointFixedScale(camToWorld.inverse(),
                                                                             pos_cam));

                Eigen::Matrix<double, 4, 4> e_fixedScalePointToWorld = fixedScalePointToWorld.matrix();

                for (int i = 0; i < 3; i++)
                    pos_world[i] = e_fixedScalePointToWorld(i, 3);

                // TODO: make for cam points

                //                Sophus::SE3d fixedScalePointToCam(invTransformPointFixedScale(mCamToWorld.inverse().matrix(),
                //                                                                              pos_world));
                //
                //                // If you don't want to use the optical transform, then remove Toc from here
                //                Eigen::Matrix<double, 4, 4> e_fixedScalePointToCam = Toc.matrix() * fixedScalePointToCam.matrix();
                //                for (int i = 0; i < 3; i++)
                //                {
                //                    pos_metric_cam[i] = e_fixedScalePointToCam(i, 3);
                //                }

                pcl::PointXYZ point_world;
                point_world.x = pos_world(0);
                point_world.y = pos_world(1);
                point_world.z = pos_world(2);

                //                pcl::PointXYZ point_metric_cam;
                //                point_metric_cam.x = pos_metric_cam(0);
                //                point_metric_cam.y = pos_metric_cam(1);
                //                point_metric_cam.z = pos_metric_cam(2);
                //                point_metric_cam.curvature = fh->shell->timestamp;

                margin_local_cloud->push_back(point_world);
                ROS_WARN("Accumulated margin points");
            }

            timestamp = fh->shell->timestamp;
        }
    }

    ROS_WARN("Check zero size");
    if (active_local_cloud_world->size() < 1)
        return;

    pcl::toPCLPointCloud2(*active_local_cloud_world, *active_local_cloud_world_2);
    pcl::toPCLPointCloud2(*margin_local_cloud, *margin_local_cloud_2);

    //    PointCloudXYZ::Ptr filtered_active_local_cloud_cam(new PointCloudXYZ);
    //    PointCloudXYZ::Ptr filtered_active_local_cloud_world(new PointCloudXYZ);
    pcl::PCLPointCloud2::Ptr filtered_active_local_cloud_world_2(new pcl::PCLPointCloud2);
    //        PointCloudXYZ::Ptr filtered_margin_local_cloud(new PointCloudXYZ);
    pcl::PCLPointCloud2::Ptr filtered_margin_local_cloud_2(new pcl::PCLPointCloud2);

    ROS_WARN("Filtering act. points");
    sor.setInputCloud(active_local_cloud_world_2);
    sor.setMeanK(meanK);
    sor.setStddevMulThresh(stddevMulThresh);
    sor.filter(*filtered_active_local_cloud_world_2);

    //    outrem.setInputCloud(active_local_cloud_world);
    //    outrem.setRadiusSearch(activeRadiusSearch);
    //    outrem.setMinNeighborsInRadius(activeMinNeighborsInRadius);
    //    outrem.setKeepOrganized(true);
    //    outrem.filter(*filtered_active_local_cloud_world);
    //
    //    ROS_WARN("Filtering m. points ");
    //    outrem.setInputCloud(margin_local_cloud);
    //    outrem.setRadiusSearch(marginRadiusSearch);
    //    outrem.setMinNeighborsInRadius(marginMinNeighborsInRadius);
    //    outrem.setKeepOrganized(true);
    //    outrem.filter(*filtered_margin_local_cloud);

    //    ROS_WARN("Check size points");
    //    if (filtered_active_local_cloud_world->size() < minNumPointsToSend)
    //    {
    //        ROS_WARN("Not enough points");
    //        return;
    //    }
    //
    ros::Time ros_ts;
    ros_ts.fromSec(timestamp);

    // Get local_cloud

    //    *local_cloud_world = *filtered_active_local_cloud_world + *filtered_margin_local_cloud;
    //
    pcl_conversions::moveFromPCL(*active_local_cloud_world_2, msg_local_cloud);
    //    pcl::toROSMsg(*active_local_cloud_world, msg_local_cloud);
    msg_local_cloud.header.stamp = ros_ts;
    msg_local_cloud.header.frame_id = "zed2_camera_frame";

    //    *global_cloud += *local_cloud_world;

    ROS_WARN("Pub. points");
    dsoLocalPointCloudPublisher.publish(msg_local_cloud);
    //    {
    //        std::unique_lock<std::mutex> mtx(pclMutex);
    //        localPointsBuf.push_back(msg_local_cloud);
    //    }
    ROS_WARN("NULL");
}
