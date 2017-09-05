#include <ros/ros.h>
#include <ros/package.h>
#include <tf/tf.h>
#include <geometry_msgs/Point.h>
#include <std_msgs/Bool.h>
#include <opencv2/core/core.hpp>
#include <math.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

class TrackControl {
 public:
  TrackControl(ros::NodeHandle nh): current(0) {
    n_.param<std::string>("/control_track_node/trackFile", trackFile, "track02.csv");
    
    subDst_ = n_.subscribe("/localization/control/destination/reached",1,&TrackControl::setDestination,this);
    pubDst_ = nh.advertise<geometry_msgs::Point>(nh.resolveName("/localization/control/dest"), 1);

    msg_dst.z = 0;

    std::string path = ros::package::getPath("control") + std::string("/config/") + trackFile;
    
    std::ifstream file;
    std::string line_str;

    float _x = 0, _y = 0;
    
    file.open(path.c_str(), std::ios::in);
    while(file.good()){
      getline(file, line_str);
      std::stringstream line_ss(line_str);

      line_ss >> _x >> _y;
      ROS_INFO("control_track_node point %lu (%.2f/%.2f)", point_list.size(), _x, _y);

      point_list.push_back(cv::Point3f(_x, _y, 0.0));
    }
    file.close();
    ROS_INFO("control_track_node initialization Num Points: %lu path: %s", point_list.size(), path.c_str());

    // send first destination
    msg_dst.x = point_list.begin()->x;
    msg_dst.y = point_list.begin()->y;

#ifdef DEBUG_CONTROL
    ROS_INFO("control_track_node setDestination current: %d dst(%.2f/%.2f)", current, msg_dst.x, msg_dst.y);
#endif

    pubDst_.publish(msg_dst);
  }
  ~TrackControl(){}

  void setDestination(const std_msgs::Bool& msg_dst_reached) {
    msg_dst.x = point_list.at(current).x;
    msg_dst.y = point_list.at(current).y;

    current = (current + 1) % point_list.size();
    pubDst_.publish(msg_dst);
#ifdef DEBUG_CONTROL
    ROS_INFO("control_track_node setDestination current: %d dst(%.2f/%.2f)", current, msg_dst.x, msg_dst.y);
#endif
  }

  std::string trackFile;  
 protected:
  ros::Publisher pubDst_;
  geometry_msgs::Point msg_dst;
  unsigned current;
  std::vector<cv::Point3f> point_list;
  ros::Subscriber subDst_;
  ros::NodeHandle n_; 

};//End of class auto_stop

int main(int argc, char **argv) {
  ros::init(argc, argv, "control_track_node");
  ros::NodeHandle nh; 
  TrackControl path(nh);
 
#ifdef DEBUG_CONTROL
    ROS_INFO("control_track_node DEBUG_MODE");
#endif
 
  ROS_INFO("control_track_node initialization track file: %s", path.trackFile.c_str());
  
  while(ros::ok()) {
    ros::spin();
  }
  return 0;
}