#include <limits>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>
#include <ros/package.h>
#include "../map.hpp"

int main(int argc, char* argv[]) {
  ros::init(argc, argv, "stitching");
  ros::NodeHandle nh;

  std::string path_img = ros::package::getPath("cps2")
      + std::string("/../../../captures/test_ie.jpg");

  if(access(path_img.c_str(), R_OK ) == -1) {
    ROS_ERROR("No such file: %s\nPlease give a path relative to package_path/../../../captures/\n", path_img.c_str() );
    return 1;
  }

  cps2::ImageEvaluator image_evaluator(cps2::IE_MODE_PIXELS, 1, 1, 1);
  cps2::Map map(&image_evaluator, false, 1.0, 1, 60);
  fisheye_camera_matrix::CameraMatrix camera_matrix(
      (ros::package::getPath("fisheye_camera_matrix")
      + std::string("/config/default.calib") ).c_str()
  );

  // image 1
  cv::Mat img1;
  cv::cvtColor(cv::imread(path_img), img1, CV_BGR2GRAY);

  // init map
  map.update(img1, cps2::Particle(0, 0, 0), camera_matrix);

  // image 2
  cv::Point3f img2_real_pos_rel(atof(argv[1]), atof(argv[2]), atof(argv[3]) );

  cv::Point2i img2_real_pos_img = camera_matrix.relative2image(
      cv::Point2f(img2_real_pos_rel.x, img2_real_pos_rel.y) );

  cv::Mat img2_real_img = image_evaluator.transform(
      img1, img2_real_pos_img, 0, img2_real_pos_rel.z);

  printf("\nreal vector image1->image2:      %.2f, %.2f, %.2f \n",
      img2_real_pos_rel.x, img2_real_pos_rel.y, img2_real_pos_rel.z);

  cv::Mat canvas1(img1.rows, 2 * img1.cols + 20, CV_8UC1, cv::Scalar(127) );

  img1.copyTo(canvas1(cv::Rect2i(0, 0, img1.cols, img1.rows) ) );
  img2_real_img.copyTo(canvas1(cv::Rect2i(img1.cols + 20, 0, img1.cols, img1.rows) ) );
  cv::imshow("img1 <-> img2 (real)", canvas1);
  cv::waitKey(0);

  // image 2 guessed version
  cv::Point3f img2_guess_pos_rel(atof(argv[4]), atof(argv[5]), atof(argv[6]) );

  printf("guessed vector image1->image2:   %.2f, %.2f, %.2f \n",
      img2_guess_pos_rel.x, img2_guess_pos_rel.y, img2_guess_pos_rel.z);
  
  // image 2 corrected guess
  cv::Point3f img2_corr_pos_rel = map.image_distance(img1, img2_real_img,
      cv::Point3f(0, 0, 0), img2_guess_pos_rel);

  cv::Point2i img2_corr_pos_img = camera_matrix.relative2image(
      cv::Point2f(img2_corr_pos_rel.x, img2_corr_pos_rel.y) );

  cv::Mat img2_corr_img = image_evaluator.transform(
      img1, img2_corr_pos_img, 0, img2_corr_pos_rel.z);

  printf("corrected vector image1->image2: %.2f, %.2f, %.2f \n\n",
      img2_corr_pos_rel.x, img2_corr_pos_rel.y, img2_corr_pos_rel.z);

  cv::Mat canvas2(img1.rows, 2 * img1.cols + 20, CV_8UC1, cv::Scalar(127) );

  img2_real_img.copyTo(canvas2(cv::Rect2i(0, 0, img1.cols, img1.rows) ) );
  img2_corr_img.copyTo(canvas2(cv::Rect2i(img1.cols + 20, 0, img1.cols, img1.rows) ) );

  cv::imshow("img2 (real) <-> img2 (corrected, from img1)", canvas2);
  cv::waitKey(0);

  return 0;
}
