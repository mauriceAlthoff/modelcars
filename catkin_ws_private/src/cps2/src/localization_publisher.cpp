#include <stdlib.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/PoseStamped.h>
#include <image_transport/image_transport.h>
#include <nav_msgs/Odometry.h>
#include <opencv2/core/core.hpp>
#include <ros/ros.h>
#include <ros/package.h>
#include <tf/tf.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include "map.hpp"
#include "particle_filter.hpp"
#include <cps2_particle_msgs/particle_msgs.h>

bool has_odom          = false;
bool has_camera_matrix = false;
bool ready             = false;

cps2::ImageEvaluator *image_evaluator;
cps2::Map *map;
cps2::ParticleFilter *particleFilter;

cv::Mat image;
cv::Point3f pos_start;
cv::Point2f pos_relative_vel;
nav_msgs::Odometry odom_last;
fisheye_camera_matrix::CameraMatrix camera_matrix;
ros::Time stamp_last_odom;
ros::Time stamp_last_image;
geometry_msgs::PoseStamped msg_pose;
cps2_particle_msgs::particle_msgs msg_particle;
ros::Publisher pub;
ros::Publisher pub_particle;

#ifdef DEBUG_PF
visualization_msgs::MarkerArray msg_markers_particles;
visualization_msgs::MarkerArray msg_markers_mappieces;
sensor_msgs::ImagePtr msg_best;
ros::Publisher pub_markers_particles;
ros::Publisher pub_markers_mappieces;
image_transport::Publisher pub_best;

#ifndef DEBUG_PF_STATIC
std::ofstream file;
#endif
#endif

void callback_odometry(const nav_msgs::Odometry &msg) {
  if(stamp_last_odom.isZero() ) {
    stamp_last_odom = msg.header.stamp;
    return;
  }

  ros::Time now = msg.header.stamp;
  float dt      = (now - stamp_last_odom).toSec();

  stamp_last_odom = now;

  tf::Quaternion q_last;
  tf::Quaternion q_now;

  tf::quaternionMsgToTF(odom_last.pose.pose.orientation, q_last);
  tf::quaternionMsgToTF(msg.pose.pose.orientation, q_now);

  pos_relative_vel.x  = msg.twist.twist.linear.x;
  pos_relative_vel.y -= tf::getYaw(q_now) - tf::getYaw(q_last);
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

      map->update(image, cps2::Particle(pos_start.x, pos_start.y, pos_start.z), camera_matrix);
      particleFilter->addNewRandomParticles();
      ready = true;
    }
    else
      return;

  if(stamp_last_image.isZero() ) {
    stamp_last_image = msg->header.stamp;
    return;
  }

  ros::Time now = msg->header.stamp;
  float dt      = (now - stamp_last_image).toSec();

  stamp_last_image = now;

  particleFilter->motion_update(dt * pos_relative_vel.x, -pos_relative_vel.y);
  particleFilter->resample();
  particleFilter->evaluate(image);
  
  cps2::Particle best = particleFilter->getBest();

  map->update(image, best, camera_matrix);

  pos_relative_vel.y = 0;

  tf::Quaternion best_q = tf::createQuaternionFromYaw(best.p.z);

  msg_particle.header.seq         = msg->header.seq;
  msg_particle.header.stamp       = msg->header.stamp;
  msg_particle.pose.position.x    = best.p.x;
  msg_particle.pose.position.y    = best.p.y;
  msg_particle.pose.orientation.x = best_q.getX();
  msg_particle.pose.orientation.y = best_q.getY();
  msg_particle.pose.orientation.z = best_q.getZ();
  msg_particle.pose.orientation.w = best_q.getW();
  msg_particle.belief = particleFilter->getBestSignle().belief;
  pub_particle.publish(msg_particle);

#ifdef DEBUG_PF
  // publish pose topic 
  msg_pose.header = msg_particle.header;
  msg_pose.pose = msg_particle.pose;

  pub.publish(msg_pose);

  // draw particles
  int i = 0;

  for(std::vector<cps2::Particle>::iterator it = particleFilter->particles.begin();
      it < particleFilter->particles.end(); ++it) {
    tf::Quaternion q  = tf::createQuaternionFromYaw(it->p.z);
    visualization_msgs::Marker *marker = &msg_markers_particles.markers[i];

    marker->header.seq         = msg->header.seq;
    marker->header.stamp       = msg->header.stamp;
    marker->pose.position.x    = it->p.x;
    marker->pose.position.y    = it->p.y;
    marker->pose.orientation.x = q.getX();
    marker->pose.orientation.y = q.getY();
    marker->pose.orientation.z = q.getZ();
    marker->pose.orientation.w = q.getW();
    marker->color.r            = 1 - it->belief;
    marker->color.g            = it->belief;

    ++i;
  }

  pub_markers_particles.publish(msg_markers_particles);

  // draw mappieces
  msg_markers_mappieces.markers.clear();

  i = 0;

  for(std::vector<std::vector<cps2::MapPiece> >::const_iterator it1 = map->grid.begin();
      it1 != map->grid.end(); ++it1)
    for(std::vector<cps2::MapPiece>::const_iterator it2 = it1->begin();
        it2 != it1->end(); ++it2)
      if(it2->is_set) {
        visualization_msgs::Marker marker;

        tf::Quaternion q = tf::createQuaternionFromYaw(it2->pos_world.z);

        marker.header.frame_id    = "base_link";
        marker.header.seq         = msg->header.seq;
        marker.header.stamp       = msg->header.stamp;
        marker.ns                 = "cps2";
        marker.id                 = i++;
        marker.type               = visualization_msgs::Marker::ARROW;
        marker.action             = visualization_msgs::Marker::ADD;
        marker.pose.position.x    = it2->pos_world.x;
        marker.pose.position.y    = it2->pos_world.y;
        marker.pose.position.z    = 0;
        marker.pose.orientation.x = q.getX();
        marker.pose.orientation.y = q.getY();
        marker.pose.orientation.z = q.getZ();
        marker.pose.orientation.w = q.getW();
        marker.color.a            = 1.0;
        marker.color.r            = 0.0;
        marker.color.g            = 0.8;
        marker.color.b            = 1.0;
        marker.scale.x            = 0.4;
        marker.scale.y            = 0.2;
        marker.scale.z            = 0.2;

        msg_markers_mappieces.markers.push_back(marker);
      }

  pub_markers_mappieces.publish(msg_markers_mappieces);

  // publish image of map at localized pos
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

  if(argc < 18) {
    ROS_ERROR("Please use roslaunch: 'roslaunch cps2 localization_publisher[_debug].launch "
              "[big_map:=INT] [grid_size:=FLOAT] [update_interval_min:=FLOAT] [update_interval_max:=FLOAT] [logfile:=FILE] [errorfunction:=(0|1)] [downscale:=INT] [kernel_size:=INT] "
              "[kernel_stddev:=FLOAT] [particles_num:=INT] [particles_keep:=FLOAT] "
              "[particle_stddev_lin:=FLOAT] [particle_stddev_ang:=FLOAT] [hamid_sampling:=(0|1)] "
              "[bin_size:=FLOAT] [punishEdgeParticlesRate:=FLOAT] [startPos:=BOOL]'");
    return 1;
  }

  bool big_map                  = atoi(argv[1]) != 0;
  float grid_size               = atof(argv[2]);
  float update_interval_min     = atof(argv[3]);
  float update_interval_max     = atof(argv[4]);
  std::string path_log          = ros::package::getPath("cps2") + std::string("/../../../logs/")
                         + std::string(argv[5]);
  int errorfunction             = atoi(argv[6]);
  int downscale                 = atoi(argv[7]);
  int kernel_size               = atoi(argv[8]);
  float kernel_stddev           = atof(argv[9]);
  int particles_num             = atoi(argv[10]);
  float particles_keep          = atof(argv[11]);
  float particle_belief_scale   = atof(argv[12]);
  float particle_stddev_lin     = atof(argv[13]);
  float particle_stddev_ang     = atof(argv[14]);
  bool hamid_sampling           = atoi(argv[15]) != 0;
  float bin_size                = atof(argv[16]);
  float punishEdgeParticlesRate = atof(argv[17]);
  bool setStartPos              = atoi(argv[18]) != 0;

  ROS_INFO("localization_cps2_publisher: using logfile: %s", path_log.c_str());
  ROS_INFO("localization_cps2_publisher: using big_map: %s, grid_size: %f, update_interval_min: %f, "
      "update_interval_max: %f, errorfunction: %s, downscale: %d, kernel_size: %d, "
      "kernel_stddev: %.2f, particles_num: %d, particles_keep: %.2f, particle_belief_scale: %.2f, "
      "particle_stddev_lin: %.2f, particle_stddev_ang: %.2f, hamid_sampling: %s, bin_size: %.2f, "
      "punishEdgeParticleRate %.2f, setStartPos: %d",
           (big_map ? "yes" : "no"), grid_size, update_interval_min, update_interval_max,
           (errorfunction == cps2::IE_MODE_CENTROIDS ? "centroids" : "pixels"), downscale,
           kernel_size, kernel_stddev, particles_num, particles_keep, particle_belief_scale,
           particle_stddev_lin, particle_stddev_ang, hamid_sampling ? "on" : "off", bin_size,
           punishEdgeParticlesRate, setStartPos);

  pos_start = cv::Point3f(grid_size / 2, grid_size / 2, 0);

  image_evaluator = new cps2::ImageEvaluator(errorfunction, downscale, kernel_size, kernel_stddev);
  map             = new cps2::Map(image_evaluator, big_map, grid_size, update_interval_min, update_interval_max);
  particleFilter  = new cps2::ParticleFilter(map, image_evaluator,
      particles_num, particles_keep, particle_belief_scale,
      particle_stddev_lin, particle_stddev_ang, hamid_sampling,
      bin_size, punishEdgeParticlesRate, setStartPos, pos_start);

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
  msg_particle.header.frame_id = "base_link";
  msg_particle.pose.position.z = 0;

  
  ros::Subscriber sub_odo             = nh.subscribe("/odom", 1, &callback_odometry);
  ros::Subscriber sub_camera_matrix   = nh.subscribe("/usb_cam/camera_matrix", 1, &callback_camera_matrix);
  image_transport::Subscriber sub_img = it.subscribe("/usb_cam/image_undistorted", 1, &callback_image);

  pub_particle = nh.advertise<cps2_particle_msgs::particle_msgs>("/localization/cps2/particle", 1);

#ifdef DEBUG_PF
  msg_pose.header.frame_id = "base_link";
  msg_pose.pose.position.z = 0;
  pub = nh.advertise<geometry_msgs::PoseStamped>("/localization/cps2/pose", 1);  

  pub_markers_particles = nh.advertise<visualization_msgs::MarkerArray>("/localization/cps2/particles", 1);
  pub_markers_mappieces = nh.advertise<visualization_msgs::MarkerArray>("/localization/cps2/mappieces", 1);
  pub_best              = it.advertise("/localization/cps2/pose_image", 1);

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
    msg_markers_particles.markers.push_back(marker);
  }

#ifndef DEBUG_PF_STATIC
  file.open(path_log.c_str(), std::ios::out | std::ios::trunc);
  file << "0.0 0.0 0.0" << std::endl;
#endif
#endif

  ros::spin();
}
