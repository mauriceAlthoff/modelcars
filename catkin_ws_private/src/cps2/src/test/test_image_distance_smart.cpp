#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>
#include <ros/package.h>
#include "../map.hpp"

namespace cv{
  typedef Rect_<int> Rect2i;
}

/* Moved from Map
cv::Point3f Map::image_distance(const cv::Mat &img1, const cv::Mat &img2, const cv::Point3f &pos_prev, const cv::Point3f &pos_now) {
  // differenz zwischen Bildern in Weltkoordinaten

  if(!ready)
    return cv::Point3f(0, 0, 0);

  cv::Point3f flow_corr;
  cv::Point3f flow_est = pos_now - pos_prev;
  cv::Point2f flow_rel(flow_est.x, flow_est.y);
  cv::Point2i flow_img = camera_matrix.relative2image(flow_rel);
  int x = flow_img.x - img1.cols/2;
  int y = flow_img.y - img1.rows/2;
  float theta = flow_est.z;
  cv::Mat part2 = rotate_cut(img2, -theta, -x, -y);
  cv::Mat part1(img1, cv::Rect(std::max(x, 0), std::max(y, 0), img1.cols-abs(x), img1.rows-abs(y)));

  cv::Mat tile1, tile2;
  part1.copyTo(tile1, part2);
  part2.copyTo(tile2, part1);
  cv::imshow("b1",tile1);
  cv::imshow("b2",tile2);
  cv::waitKey(0);

  float grad1 = gradient(tile1);
  float grad2 = gradient(tile2);
  float d_theta = grad1-grad2;
    printf("--%f      %f     %f\n",grad1,grad2,d_theta);

  flow_corr.z = theta + d_theta;

  cv::Mat img2_rot = rotate_img(img2, -flow_corr.z);
  float min_error = 1000;

  for (int dx=-10; dx < 11; ++dx)
  {
    for (int dy=-10; dy < 11; ++dy)
    {
      cv::Mat parta(img1, cv::Rect(std::max(x+dx, 0), std::max(y+dy, 0), img1.cols-abs(x+dx), img1.rows-abs(y+dy)));
      cv::Mat partb(img2_rot, cv::Rect(std::max(-x-dx, 0), std::max(-y-dy, 0), img1.cols-abs(x+dx), img1.rows-abs(y+dy)));

      parta.copyTo(tile1, partb);
      partb.copyTo(tile2, parta);

      cv::Mat t = tile1 - tile2;
      if (min_error > cv::sum(cv::mean(t))[0])
      {
        min_error = cv::sum(cv::mean(t))[0];
        flow_img.x = x+dx + img1.cols/2;
        flow_img.y = y+dy + img1.rows/2;
      }
    }
  }

  flow_rel = camera_matrix.image2relative(flow_img);
  flow_corr.x = cosf(pos_prev.z)*flow_rel.x - sinf(pos_prev.z)*flow_rel.y;
  flow_corr.y = sinf(pos_prev.z)*flow_rel.x + cosf(pos_prev.z)*flow_rel.y;

  return flow_corr;
}

cv::Mat Map::rotate_cut(const cv::Mat &img, const float radiant, const int dx, const int dy) {
  const float r_sin = sinf(radiant);
  const float r_cos = cosf(radiant);

  cv::Mat img_rot(img.rows-abs(dy), img.cols-abs(dx), CV_8UC1);
  int delta_x = 0;
  int delta_y = 0;
  if(dx > 0) delta_x = dx;
  if(dy > 0) delta_y = dy;

  for(int c = 0; c < img.cols-abs(dx); ++c) {
    const int sx = c - img.cols/2;

    for(int r = 0; r < img.rows-abs(dy); ++r) {
      const int sy = r - img.rows/2;
      const float x = sx + delta_x + img.cols/2; // + cosf(radiant)*(x+delta_x-img.cols/2) - sinf(radiant)*(y+delta_y-img.rows/2);
      const float y = sy + delta_y + img.rows/2; // + sinf(radiant)*(x+delta_x-img.cols/2) + cosf(radiant)*(y+delta_y-img.rows/2);
      const float xx = (x - img.cols / 2) * r_cos - (y - img.rows / 2) * r_sin + img.cols/2;
      const float yy = (x - img.cols / 2) * r_sin + (y - img.rows / 2) * r_cos + img.rows/2;

      if(xx >= 0 && yy >= 0 && xx < img.cols && yy < img.rows)
        img_rot.at<uchar>(r, c) = img.at<uchar>(yy, xx);
      else
        img_rot.at<uchar>(r, c) = 0;
    }
  }
  return img_rot;
}

cv::Mat ImageEvaluator::transform(const cv::Mat &img,
    const float ph)
{
  const int cx1   = img.cols / 2;
  const int cy1   = img.rows / 2;
  const int dim_x = img.cols;
  const int dim_y = img.rows;
  const int cx2   = dim_x / 2;
  const int cy2   = dim_y / 2;
  const float phs = sinf(ph);
  const float phc = cosf(ph);

  cv::Mat img_tf(dim_y, dim_x, CV_8UC1);

  for(int c = 0; c < dim_x; ++c) {
    const int sx = c - cx2;

    for(int r = 0; r < dim_y; ++r) {
      const int sy   = r - cy2;
      const float x  = (sx);
      const float y  = (sy);
      const float xx = (x - cx1) * phc - (y - cy1) * phs + cx1;
      const float yy = (x - cx1) * phs + (y - cy1) * phc + cy1;

      if(xx >= 0 && yy >= 0 && xx < img.cols && yy < img.rows)
        img_tf.at<uchar>(r, c) = applyKernel(img, (int)xx, (int)yy);
      else
        img_tf.at<uchar>(r, c) = 0;
    }
  }

  return img_tf;
}

cv::Mat Map::rotate_img(const cv::Mat &img, const float radiant) {
  cv::Mat img_rot(img.rows, img.cols, CV_8UC1);
  for(int x = 0; x < img.cols; ++x) {
    for(int y = 0; y < img.rows; ++y) {
      float x_rot = img.cols/2 + cosf(radiant)*(x-img.cols/2) - sinf(radiant)*(y-img.rows/2);
      float y_rot = img.rows/2 + sinf(radiant)*(x-img.cols/2) + cosf(radiant)*(y-img.rows/2);

      if(x_rot >= 0 && y_rot >= 0 && x_rot < img.cols && y_rot < img.rows)
        img_rot.at<uchar>(y, x) = img.at<uchar>(y_rot, x_rot);
      else
        img_rot.at<uchar>(y, x) = 0;
    }
  }
  return img_rot;
}

float Map::gradient(const cv::Mat &img) {
  cv::Mat mat_x, mat_y, abs_x, abs_y;
  cv::Sobel(img,mat_x, CV_16S, 1, 0, 3, 1, 0, cv::BORDER_DEFAULT);
  cv::Sobel(img,mat_y, CV_16S, 0, 1, 3, 1, 0, cv::BORDER_DEFAULT);
  cv::convertScaleAbs(mat_x, abs_x);
  cv::convertScaleAbs(mat_y, abs_y);
  cv::Mat orientation = cv::Mat::zeros(abs_x.rows, abs_y.cols, CV_32F);
  abs_x.convertTo(mat_x,CV_32F);
  abs_y.convertTo(mat_y,CV_32F);
  cv::phase(mat_x, mat_y, orientation, false);
  return cv::mean(orientation)[0];
}
*/

float orientation(const cv::Mat &img) {
  cv::Moments m = cv::moments(img, false);
  float cen_x=m.m10/m.m00;
  float cen_y=m.m01/m.m00;
  float m_11= 2*m.m11-m.m00*(cen_x*cen_x+cen_y*cen_y);// m.mu11/m.m00;    
  float m_02=m.m02-m.m00*cen_y*cen_y;// m.mu02/m.m00;
  float m_20=m.m20-m.m00*cen_x*cen_x;//m.mu20/m.m00;    
  float theta = m_20==m_02?0:atan2(m_11, m_20-m_02)/2.0;
    //  theta = (theta / PI) * 180.0; //if you want in radians.(or vice versa, not sure)
  //return cv::Point3f(cen_x,cen_y,theta);
  return theta;
}

float apply_kernel(const cv::Mat &kernel, const cv::Mat &img, const int x, const int y) {
  int ks2     = kernel.rows/2;
  int x_lb    = std::max(0, x - ks2);
  int x_ub    = std::min(img.cols, x + ks2 + 1);
  int y_lb    = std::max(0, y - ks2);
  int y_ub    = std::min(img.rows, y + ks2 + 1);
  float acc_v = 0;

  for(int r = y_lb; r < y_ub; ++r) {
    int ky = r - y + ks2;

    for(int c = x_lb; c < x_ub; ++c) {
      if(img.at<uchar>(r,c)==0) {
        return 0;
      }
      int kx = c - x + ks2;
      acc_v += kernel.at<float>(ky, kx) * img.at<uchar>(r, c);
    }
  }
  return acc_v;
}

std::vector<float> new_gradient(const cv::Mat &img) {  
  std::vector<float> gradienten;
  // blur
  int k_size = 5;
  float acc = 0;
  cv::Mat blur(k_size,k_size,CV_32FC1);
  for(int r = 0; r < k_size; ++r) {
    int y = r- k_size/2;
    for (int c = 0; c < k_size; ++c) {
      int x = c- k_size/2;
      blur.at<float>(r,c) = exp( (-x*x -y*y) / (2*2.5*2.5) );
      acc += blur.at<float>(r,c);
    }
  }
  cv::Mat img_blur = cv::Mat::zeros(img.size(), CV_8UC1);
  for(int x = 0; x < img.cols; ++x) {
    for(int y = 0; y < img.rows; ++y) {
      if(img.at<uchar>(y,x) != 0) {
        img_blur.at<uchar>(y,x) = (int)(apply_kernel(blur, img, x, y) / acc);
      }
    }
  }
  //cv::imshow("blur",img_blur);
  //cv::GaussianBlur(img,img,cv::Size(3,3),0,0, cv::BORDER_DEFAULT);
  // Sobel
  cv::Mat sobel_x = (cv::Mat_<float>(3,3) << -1, 0, 1, -2, 0, 2, -1, 0, 1);
  cv::Mat sobel_y = (cv::Mat_<float>(3,3) << -1, -2, -1, 0, 0, 0, 1, 2, 1);
  cv::Mat kanten = cv::Mat::zeros(img.size(), CV_8UC1);
  float B = sqrt(2* 16*255*255);
  float x_b_a = 0; float x__a = 0; float x_b_ = 0; float x__ = 0;
  float y_b_a = 0; float y__a = 0; float y_b_ = 0; float y__ = 0;
  int min = 1000000; int max = 0;
  
  for(int x = 1; x < img_blur.cols-1; ++x) {
    for(int y = 1; y < img_blur.rows-1; ++y) {
      if(img_blur.at<uchar>(y,x) != 0) {
        int s_x = (int)apply_kernel(sobel_x, img_blur, x, y);
        int s_y = (int)apply_kernel(sobel_y, img_blur, x, y);
        float belief = sqrt(s_x*s_x + s_y*s_y);
        x_b_a += abs(s_x) * belief/B;
        y_b_a += abs(s_y) * belief/B;
        x_b_ += s_x * belief/B;
        y_b_ += s_y * belief/B;
        kanten.at<uchar>(y,x) = (int)(s_x * belief/B) + (s_y * belief/B);
        if(s_x < min) {
          min = s_x;
        }
        else if(s_x > max) {
          max = s_x;
        }
      }
      if(img.at<uchar>(y,x) != 0) {
        int s_x = (int)apply_kernel(sobel_x, img, x, y);
        int s_y = (int)apply_kernel(sobel_y, img, x, y);
        float belief = sqrt(s_x*s_x + s_y*s_y);
        x__a += abs(s_x) * belief/B;
        y__a += abs(s_y) * belief/B;
        x__ += s_x * belief/B;
        y__ += s_y * belief/B;
      }
    }
  }
  //cv::imshow("Kantenbild",kanten);
  //cv::imwrite("/home/osboxes/kanten.png",kanten);
  //printf("min %d            max %d\n",min,max);
  //printf("acc_x %f          acc_y %f\n",acc_x,acc_y);
  //cv::waitKey(0);
  gradienten.push_back(atan2(y_b_a, x_b_a));
  gradienten.push_back(atan2(y_b_, x_b_));
  gradienten.push_back(atan2(y__a, x__a));
  gradienten.push_back(atan2(y__, x__));
  return gradienten;
}

float gradient(const cv::Mat &img, const int treshold) {
  cv::Mat mat_x, mat_y, abs_x, abs_y;
//  cv::Mat part(img, cv::Rect(img.cols*0.15, img.rows*0.15, img.cols*0.7, img.rows*0.7));
  cv::Mat img_blur;
  cv::GaussianBlur(img,img_blur,cv::Size(3,3),0,0, cv::BORDER_DEFAULT);
  cv::Sobel(img_blur,mat_x, CV_16S, 1, 0, 3, 1, 0, cv::BORDER_DEFAULT);
  cv::Sobel(img_blur,mat_y, CV_16S, 0, 1, 3, 1, 0, cv::BORDER_DEFAULT);
  cv::convertScaleAbs(mat_x, abs_x);
  cv::convertScaleAbs(mat_y, abs_y);
  cv::Mat test;
  cv::addWeighted(abs_x, 0.5, abs_y, 0.5, 0, test);
//  cv::imshow("test",test);
  int min = 1000000; int max = 0;
  int B_x = 0; int B_y = 0;
  for (int x = 0; x < img.cols; ++x) {
    for (int y = 0; y < img.rows; ++y) {
      if(abs_x.at<uchar>(y,x)>treshold) {
        B_x = B_x + abs_x.at<uchar>(y,x);
      } else {
        abs_x.at<uchar>(y,x) = 0;
      }
      if(abs_y.at<uchar>(y,x)>treshold) {
        B_y = B_y + abs_y.at<uchar>(y,x);
      }else {
        abs_y.at<uchar>(y,x) = 0;
      }
      if(abs_x.at<uchar>(y,x) < min) {
        min = abs_x.at<uchar>(y,x);
      }
      else if(abs_x.at<uchar>(y,x) > max) {
        max = abs_x.at<uchar>(y,x);
      }
    }
  }
//  cv::addWeighted(abs_x, 0.5, abs_y, 0.5, 0, test);
//  cv::imshow("Sobel - threshold",test);
//  cv::imshow("new",part);
//  cv::waitKey(0);
//  printf("min %d            max %d\n",min,max);
//  printf("--belief x: %d,     belief y: %d\n",B_x, B_y);
  float orient = atan2(B_y, B_x);
  return orient;
}

int main(int argc, char* argv[]) {
  ros::init(argc, argv, "stitching");
  ros::NodeHandle nh;

  cps2::ImageEvaluator image_evaluator(cps2::IE_MODE_PIXELS, 1, 1, 1);
  cps2::Map map(&image_evaluator, false, 1.0, 1, 60);

  fisheye_camera_matrix::CameraMatrix camera_matrix(
      (ros::package::getPath("fisheye_camera_matrix")
      + std::string("/config/default.calib") ).c_str()
  );

  printf("Bild  alt        blur+abs   blur       abs        -           moment\n");
  std::string path = ros::package::getPath("cps2") + std::string("/../../../captures/img");
for(int i = 1; i < 21; ++i) {
  std::string path_img = path + std::to_string(i) + std::string(".png");
  if(access(path_img.c_str(), R_OK ) == -1)
  {
    ROS_ERROR("No such file: %s\nPlease give a path relative to modelcars/captures/.\n", path_img.c_str() );
    return 1;
  }

  // image 1
  cv::Mat img1;
  cv::cvtColor(cv::imread(path_img), img1, CV_BGR2GRAY);
  //cv::imwrite("/home/osboxes/urspr.png",img1);

  // init map
  map.update(img1, cps2::Particle(0, 0, 0), camera_matrix);

  // image 2
  cv::Point3f img2_real_pos_rel(atof(argv[2]), atof(argv[3]), atof(argv[4]) );

  cv::Point2i img2_real_pos_img = camera_matrix.relative2image(
      cv::Point2f(img2_real_pos_rel.x, img2_real_pos_rel.y) );

  cv::Mat img2_real_img = image_evaluator.transform(
      img1, img2_real_pos_img, 0, img2_real_pos_rel.z);
  //cv::cvtColor(cv::imread(path+std::string("test_2.png")), img2_real_img, CV_BGR2GRAY);

  //printf("\nreal vector image1->image2:      %.2f, %.2f, %.2f \n",
   //   img2_real_pos_rel.x, img2_real_pos_rel.y, img2_real_pos_rel.z);

  // image 2 guessed version
  cv::Point3f img2_guess_pos_rel(atof(argv[5]), atof(argv[6]), atof(argv[7]) );

  //printf("guessed vector image1->image2:   %.2f, %.2f, %.2f \n",
   //   img2_guess_pos_rel.x, img2_guess_pos_rel.y, img2_guess_pos_rel.z);
  
  cv::imshow("img1",img1);
//  cv::imwrite("/home/osboxes/bild.png",img2_real_img);
  cv::imshow("img2_real_img",img2_real_img);
  cv::waitKey(0);
  int treshold = atof(argv[8]);
  float g1 = gradient(img1,treshold);
  float g2 = gradient(img2_real_img,treshold);
  printf("   %d  %f   ",i, g1-g2);
  std::vector<float> ng1 = new_gradient(img1);
  std::vector<float> ng2 = new_gradient(img2_real_img);
  printf("%f   %f   %f   %f   ", ng1[0]-ng2[0], ng1[1]-ng2[1], ng1[2]-ng2[2], ng1[3]-ng2[3]);
  
  cv::Mat tile1,tile2;
  img1.copyTo(tile1, img2_real_img);
  img2_real_img.copyTo(tile2, img1);

  printf("%f\n", orientation(img1) - orientation(img2_real_img));
}
/*
  // image 2 corrected guess
  cv::Point3f img2_corr_pos_rel = map.image_distance(
      img1, img2_real_img, cv::Point3f(0, 0, 0), img2_guess_pos_rel);

  cv::Point2i img2_corr_pos_img = camera_matrix.relative2image(
      cv::Point2f(img2_corr_pos_rel.x, img2_corr_pos_rel.y) );

  cv::Mat img2_corr_img = image_evaluator.transform(
      img1, img2_corr_pos_img, 0, img2_corr_pos_rel.z);

  printf("corrected vector image1->image2: %.2f, %.2f, %.2f \n\n",
      img2_corr_pos_rel.x, img2_corr_pos_rel.y, img2_corr_pos_rel.z);

  cv::Mat canvas(img1.rows, 2 * img1.cols + 20, CV_8UC1, cv::Scalar(127) );

  img2_real_img.copyTo(canvas(cv::Rect2i(0, 0, img1.cols, img1.rows) ) );
  img2_corr_img.copyTo(canvas(cv::Rect2i(img1.cols + 20, 0, img1.cols, img1.rows) ) );

  cv::imshow("test - images should look the same", canvas);
  cv::waitKey(0);
  */
  return 0;
}
