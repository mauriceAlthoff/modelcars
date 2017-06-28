#include <stdlib.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/PoseStamped.h>
#include <image_transport/image_transport.h>
#include <nav_msgs/Odometry.h>
#include <opencv2/core.hpp>
#include <ros/ros.h>
#include <ros/package.h>
#include <tf/tf.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include "map.hpp"
#include "particle_filter.hpp"

bool has_odom          = false;
bool has_camera_matrix = false;
bool ready             = false;

cps2::ImageEvaluator *image_evaluator;
cps2::Map *map;
cps2::ParticleFilter *particleFilter;

cv::Mat image;
cv::Point2f pos_relative_vel;
cv::Point3f origin_map;
nav_msgs::Odometry odom_last;
fisheye_camera_matrix::CameraMatrix camera_matrix;
ros::Time stamp_last_odom;
ros::Time stamp_last_image;
geometry_msgs::PoseStamped msg_pose;
ros::Publisher pub;

#ifdef DEBUG_PF
visualization_msgs::MarkerArray msg_markers;
sensor_msgs::ImagePtr msg_best;
ros::Publisher pub_markers;
image_transport::Publisher pub_best;

#ifndef DEBUG_PF_STATIC
std::ofstream file;
#endif
#endif

void callback_odometry(const nav_msgs::Odometry &msg) {
  ros::Time now     = msg.header.stamp;
  ros::Duration dif = now - stamp_last_odom;
  float dt          = dif.sec + dif.nsec / 1000000000.0;

  stamp_last_odom = now;

  // TODO get rid of mysterious overflows on dt

  if(dt > 2 || dt < 0)
    return;

  tf::Quaternion q_last;
  tf::Quaternion q_now;

  tf::quaternionMsgToTF(odom_last.pose.pose.orientation, q_last);
  tf::quaternionMsgToTF(msg.pose.pose.orientation, q_now);

  pos_relative_vel.x  = msg.twist.twist.linear.x;
  pos_relative_vel.y += tf::getYaw(q_now) - tf::getYaw(q_last);
  odom_last           = msg;
  has_odom            = true;
}

void callback_camera_matrix(const fisheye_camera_matrix_msgs::CameraMatrix &msg) {
  fisheye_camera_matrix::CameraMatrix cm(msg);
  camera_matrix     = cm;
  has_camera_matrix = true;
}

void callback_image(const sensor_msgs::ImageConstPtr &msg) {
  cv::cvtColor(cv_bridge::toCvShare(msg, "bgr8")->image, image, CV_BGR2GRAY);

  if(!ready)
    if(has_odom && has_camera_matrix) {

      // TODO use init functions to hide the logic

      map->update(image, cps2::Particle(0, 0, 0), camera_matrix);
      particleFilter->addNewRandomParticles();
      ready = true;
    }
    else
      return;

  ros::Time now     = msg->header.stamp;
  ros::Duration dif = now - stamp_last_image;
  float dt          = dif.sec + dif.nsec / 1000000000.0;

  stamp_last_image = now;

  // TODO get rid of mysterious overflows on dt

  if(dt > 2 || dt < 0)
    return;

  particleFilter->motion_update(dt * pos_relative_vel.x, -pos_relative_vel.y);
  particleFilter->resample();
  particleFilter->evaluate(image);
  
  cps2::Particle best = particleFilter->getBest();

  map->update(image, best, camera_matrix);

  pos_relative_vel.y = 0;

  tf::Quaternion best_q = tf::createQuaternionFromYaw(best.p.z);

  msg_pose.header.seq         = msg->header.seq;
  msg_pose.header.stamp       = msg->header.stamp;
  msg_pose.pose.position.x    = best.p.x;
  msg_pose.pose.position.y    = best.p.y;
  msg_pose.pose.orientation.x = best_q.getX();
  msg_pose.pose.orientation.y = best_q.getY();
  msg_pose.pose.orientation.z = best_q.getZ();
  msg_pose.pose.orientation.w = best_q.getW();
  pub.publish(msg_pose);

#ifdef DEBUG_PF
  int i = 0;

  for(std::vector<cps2::Particle>::iterator it = particleFilter->particles.begin();
      it < particleFilter->particles.end(); ++it) {
    cv::Point3f p     = it->p;
    tf::Quaternion q  = tf::createQuaternionFromYaw(p.z);
    visualization_msgs::Marker *marker = &msg_markers.markers[i];

    marker->header.seq         = msg->header.seq;
    marker->header.stamp       = msg->header.stamp;
    marker->pose.position.x    = p.x;
    marker->pose.position.y    = p.y;
    marker->pose.orientation.x = q.getX();
    marker->pose.orientation.y = q.getY();
    marker->pose.orientation.z = q.getZ();
    marker->pose.orientation.w = q.getW();
    marker->color.r            = 1 - it->belief;
    marker->color.g            = it->belief;

    ++i;
  }

  pub_markers.publish(msg_markers);

  std::vector<cv::Mat> mappieces = map->get_map_pieces(best.p);

  if(!mappieces.empty() ) {
    cv::Mat best_img = mappieces.front();

    msg_best = cv_bridge::CvImage(std_msgs::Header(), "mono8", best_img).toImageMsg();

    msg_best->header.seq   = msg->header.seq;
    msg_best->header.stamp = msg->header.stamp;

    pub_best.publish(msg_best);
  }

#ifndef DEBUG_PF_STATIC
  file << best.p.x << " " << best.p.y << std::endl;
#endif
#endif
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "localization_cps2_publisher");

  if(argc < 19) {
    ROS_ERROR("Please use roslaunch: 'roslaunch cps2 localization_publisher[_debug].launch "
              "[grid_size:=FLOAT] [logfile:=FILE] [errorfunction:=(0|1)] [downscale:=INT] [kernel_size:=INT] "
              "[kernel_stddev:=FLOAT] [particles_num:=INT] [particles_keep:=FLOAT] "
              "[particle_stddev_lin:=FLOAT] [particle_stddev_ang:=FLOAT] [hamid_sampling:=(0|1)] "
              "[bin_size:=FLOAT] [punishEdgeParticlesRate:=FLOAT] [startPos:=BOOL] "
              "[startPosX:= FLOAT] [startPosY:= FLOAT] [startPosZ := FLOAT]'");
    return 1;
  }

  float grid_size               = atof(argv[1]);
  std::string path_log          = ros::package::getPath("cps2") + std::string("/../../../logs/")     + std::string(argv[2]);
  int errorfunction             = atoi(argv[3]);
  int downscale                 = atoi(argv[4]);
  int kernel_size               = atoi(argv[5]);
  float kernel_stddev           = atof(argv[6]);
  int particles_num             = atoi(argv[7]);
  float particles_keep          = atof(argv[8]);
  float particle_belief_scale   = atof(argv[9]);
  float particle_stddev_lin     = atof(argv[10]);
  float particle_stddev_ang     = atof(argv[11]);
  bool hamid_sampling           = atoi(argv[12]) != 0;
  float bin_size                = atof(argv[13]);
  float punishEdgeParticlesRate = atof(argv[14]);
  bool setStartPos             = atoi(argv[15]);
  cv::Point3f startPos;
  startPos.x = atof(argv[16]);
  startPos.y = atof(argv[17]);
  startPos.z = atof(argv[18]);

  ROS_INFO("localization_cps2_publisher: using logfile: %s", path_log.c_str());
  ROS_INFO("localization_cps2_publisher: using grid_size: %f, errorfunction: %s, downscale: %d, "
      "kernel_size: %d, kernel_stddev: %.2f, particles_num: %d, "
      "particles_keep: %.2f, particle_belief_scale: %.2f, particle_stddev_lin: %.2f, "
      "particle_stddev_ang: %.2f, hamid_sampling: %s, bin_size: %.2f, "
      "punishEdgeParticleRate %.2f setStartPos: %d startPosX: %.2f startPosY: %.2f startPosTh: %.2f",
           grid_size, (errorfunction == cps2::IE_MODE_CENTROIDS ? "centroids" : "pixels"), downscale,
           kernel_size, kernel_stddev, particles_num, particles_keep, particle_belief_scale,
           particle_stddev_lin, particle_stddev_ang, hamid_sampling ? "on" : "off", bin_size,
           punishEdgeParticlesRate, setStartPos, startPos.x, startPos.y, startPos.z);

  image_evaluator = new cps2::ImageEvaluator(errorfunction, downscale, kernel_size, kernel_stddev);
  map             = new cps2::Map(grid_size, image_evaluator);
  particleFilter  = new cps2::ParticleFilter(map, image_evaluator,
      particles_num, particles_keep, particle_belief_scale,
      particle_stddev_lin, particle_stddev_ang, hamid_sampling,
      bin_size, punishEdgeParticlesRate, setStartPos, startPos);

  ros::NodeHandle nh;
  image_transport::ImageTransport it(nh);

  odom_last.twist.twist.linear.x    = 0;
  odom_last.pose.pose.orientation.x = 0;
  odom_last.pose.pose.orientation.y = 0;
  odom_last.pose.pose.orientation.z = 0;
  odom_last.pose.pose.orientation.w = 1;
  stamp_last_odom.sec      = 0;
  stamp_last_odom.nsec     = 0;
  stamp_last_image.sec     = 0;
  stamp_last_image.nsec    = 0;
  msg_pose.header.frame_id = "base_link";
  msg_pose.pose.position.z = 0;

  ros::Subscriber sub_odo             = nh.subscribe("/odom", 1, &callback_odometry);
  ros::Subscriber sub_camera_matrix   = nh.subscribe("/usb_cam/camera_matrix", 1, &callback_camera_matrix);
  image_transport::Subscriber sub_img = it.subscribe("/usb_cam/image_raw", 1, &callback_image);

  pub = nh.advertise<geometry_msgs::PoseStamped>("/localization/cps2/pose", 1);

#ifdef DEBUG_PF
  pub_markers = nh.advertise<visualization_msgs::MarkerArray>("/localization/cps2/particles", 1);
  pub_best    = it.advertise("/localization/cps2/pose_image", 1);

  for(int i = 0; i < particleFilter->particles_num; ++i) {
    visualization_msgs::Marker marker;
    marker.header.frame_id = "base_link";
    marker.ns = "cps2";
    marker.id = i;
    marker.type = visualization_msgs::Marker::ARROW;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.z = 0;
    marker.scale.x = 0.2;
    marker.scale.y = 0.1;
    marker.scale.z = 0.1;
    marker.color.a = 1.0;
    marker.color.b = 0.0;
    msg_markers.markers.push_back(marker);
  }

#ifndef DEBUG_PF_STATIC
  file.open(path_log.c_str(), std::ios::out | std::ios::trunc);
#endif
#endif

  ros::spin();
}
