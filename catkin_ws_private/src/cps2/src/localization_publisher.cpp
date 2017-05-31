#include <stdlib.h>
#include <vector>
#include <ros/package.h>
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

bool ready = false;

cps2::Map *map;
cps2::ParticleFilter3f *particleFilter;

cv::Mat image;
cv::Point3f pose;
cv::Point3f pose_velocities;
cv::Point3f origin_map;
ros::Time stamp_last_callback;
geometry_msgs::PoseStamped msg_pose;
ros::Publisher pub;

#ifdef DEBUG_PF
visualization_msgs::MarkerArray msg_markers;
ros::Publisher pub_markers;
#endif

void callback_odometry(const nav_msgs::Odometry &msg) {
  pose_velocities.x = msg.twist.twist.linear.x;
  pose_velocities.y = msg.twist.twist.linear.y;
  pose_velocities.z = msg.twist.twist.angular.z;
  pose_velocities   = map->world2map(pose_velocities) - origin_map;
  ready             = true;
}

void callback_image(const sensor_msgs::ImageConstPtr &msg) {
  if (!ready)
    return;

  if(!stamp_last_callback.isValid()) {
    stamp_last_callback = msg->header.stamp;
    return;
  }

  image = cv_bridge::toCvShare(msg, "bgr8")->image;

  ros::Time now     = msg->header.stamp;
  ros::Duration dif = now - stamp_last_callback;

  float dt = dif.sec + dif.nsec / 1000000000.0;

  particleFilter->evaluate(image, dt * pose_velocities.x, dt * pose_velocities.y);

  pose                = particleFilter->getBest();
  stamp_last_callback = now;

  tf::Quaternion q = tf::createQuaternionFromYaw(pose.z);

  msg_pose.header.seq = msg->header.seq;
  msg_pose.header.stamp = msg->header.stamp;
  msg_pose.pose.position.x = pose.x;
  msg_pose.pose.position.y = pose.y;
  msg_pose.pose.orientation.x = q.getX();
  msg_pose.pose.orientation.y = q.getY();
  msg_pose.pose.orientation.z = q.getZ();
  msg_pose.pose.orientation.w = q.getW();
  pub.publish(msg_pose);

#ifdef DEBUG_PF
  for(int i = 0; i < particleFilter->particles_num; ++i) {
    cv::Point3f p = map->map2world(particleFilter->particles[i].p);
    tf::Quaternion q = tf::createQuaternionFromYaw(p.z);
    visualization_msgs::Marker *marker = &msg_markers.markers[i];
    marker->header.seq = msg->header.seq;
    marker->header.stamp = msg->header.stamp;
    marker->pose.position.x = p.x;
    marker->pose.position.y = p.y;
    marker->pose.orientation.x = q.getX();
    marker->pose.orientation.y = q.getY();
    marker->pose.orientation.z = q.getZ();
    marker->pose.orientation.w = q.getW();
  }

  pub_markers.publish(msg_markers);
#endif
}

int main(int argc, char **argv) {
  if(argc < 4) {
    ROS_ERROR("Please use roslaunch as entry point.");
    return 1;
  }

  std::string path_map = ros::package::getPath("cps2") + std::string("/../../../captures/") + std::string(argv[1]);
  int particles_num    = atoi(argv[2]);
  int particles_keep   = atof(argv[3]);
  int particle_stddev  = atof(argv[4]);

  if(access(path_map.c_str(), R_OK ) == -1) {
    ROS_ERROR("No such file: %s\nPlease give a path relative to catkin_ws/../captures/.\n", path_map.c_str() );
    return 1;
  }

  map            = new cps2::Map(path_map.c_str());
  particleFilter = new cps2::ParticleFilter3f(map, particles_num, particles_keep, particle_stddev);

  ros::init(argc, argv, "localization_cps2_publisher");
  ros::NodeHandle nh;
  image_transport::ImageTransport it(nh);
  cv::Point3f origin_world(0, 0, 0);

  stamp_last_callback.sec  = 0;
  stamp_last_callback.nsec = 0;
  origin_map               = map->world2map(origin_world);
  msg_pose.header.frame_id = "base_link";
  msg_pose.pose.position.z = 0;

  ros::Subscriber sub_odo             = nh.subscribe("/odom", 1, &callback_odometry);
  image_transport::Subscriber sub_img = it.subscribe("/usb_cam/image_undistorted", 1, &callback_image);

  pub = nh.advertise<geometry_msgs::PoseStamped>("/localization/cps2/pose", 1);

#ifdef DEBUG_PF
  pub_markers = nh.advertise<visualization_msgs::MarkerArray>("/localization/cps2/particles", 1);

  for(int i = 0; i < particleFilter->particles_num; ++i) {
    visualization_msgs::Marker marker;
    marker.header.frame_id = "base_link";
    marker.ns = "cps2";
    marker.id = i;
    marker.type = visualization_msgs::Marker::ARROW;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.z = 0;
    marker.scale.x = 0.1;
    marker.scale.y = 0.1;
    marker.scale.z = 0.1;
    marker.color.a = 1.0;
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    msg_markers.markers.push_back(marker);
  }
#endif

  ros::spin();
}
