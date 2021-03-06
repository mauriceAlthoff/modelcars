#include <math.h>
#include <opencv2/imgproc/imgproc.hpp>

#ifdef DEBUG_IE
#include <stdio.h>
#include <opencv2/highgui/highgui.hpp>
#endif

#include "image_evaluator.hpp"

namespace cps2 {

#ifdef DEBUG_IE
int d_img_width  = 360;
int d_img_height = 240;
cv::Size d_win_size(d_img_width, d_img_height);
cv::Mat d_win_img(d_img_height, 2 * d_img_width + 20, CV_8UC1);
#endif

void ImageEvaluator::generateKernel() {
  int center = kernel_size / 2;
  float acc  = 0;

  cv::Mat kernel(kernel_size, kernel_size, CV_32FC1);

  for(int r = 0; r < kernel_size; ++r) {
    int y = r - center;

    for(int c = 0; c < kernel_size; ++c) {
      int x = c - center;
      kernel.at<float>(r, c) = exp( (-x*x - y*y) / (2 * kernel_stddev*kernel_stddev) );
      acc += kernel.at<float>(r, c);
    }
  }

  this->kernel = kernel;
}

int ImageEvaluator::applyKernel(const cv::Mat &img, int x, int y) {
  int ks2     = kernel_size / 2;
  int x_lb    = std::max(0, x - ks2);
  int x_ub    = std::min(img.cols, x + ks2 + 1);
  int y_lb    = std::max(0, y - ks2);
  int y_ub    = std::min(img.rows, y + ks2 + 1);
  float acc_k = 0;
  float acc_v = 0;

  for(int r = y_lb; r < y_ub; ++r) {
    int ky = r - y + ks2;

    for(int c = x_lb; c < x_ub; ++c) {
      int kx = c - x + ks2;

      acc_k += kernel.at<float>(ky, kx);
      acc_v += kernel.at<float>(ky, kx) * img.at<uchar>(r, c);
    }
  }

  return (int)(acc_v / acc_k);
}

ImageEvaluator::ImageEvaluator(int _mode, int _resize_scale, int _kernel_size, float _kernel_stddev) :
    mode(_mode),
    resize_scale(_resize_scale),
    kernel_stddev(_kernel_stddev)
{
  kernel_size = 2 * (_kernel_size / 2) + 1;

  generateKernel();
}

ImageEvaluator::~ImageEvaluator() {

}

cv::Mat ImageEvaluator::transform(const cv::Mat &img, const cv::Point2i &pos_image,
    const float th, const float ph, const int rows, const int cols)
{
  const int cx1   = img.cols / 2;
  const int cy1   = img.rows / 2;
  const int dim_x = cols / resize_scale;
  const int dim_y = rows / resize_scale;
  const int cx2   = dim_x / 2;
  const int cy2   = dim_y / 2;
  const float ths = sinf(th);
  const float thc = cosf(th);
  const float phs = sinf(ph);
  const float phc = cosf(ph);

  cv::Mat img_tf(dim_y, dim_x, CV_8UC1);

  for(int c = 0; c < dim_x; ++c) {
    const int sx = c - cx2;

    for(int r = 0; r < dim_y; ++r) {
      const int sy   = r - cy2;
      const float x  = resize_scale * (sx * thc - sy * ths ) + pos_image.x - cx1;
      const float y  = resize_scale * (sx * ths + sy * thc ) + pos_image.y - cy1;
      const float xx = x * phc - y * phs + cx1;
      const float yy = x * phs + y * phc + cy1;

      if(xx >= 0 && yy >= 0 && xx < img.cols && yy < img.rows)
        img_tf.at<uchar>(r, c) = applyKernel(img, (int)xx, (int)yy);
      else
        img_tf.at<uchar>(r, c) = 0;
    }
  }

  return img_tf;
}

cv::Mat ImageEvaluator::transform(const cv::Mat &img,
    const cv::Point2i &pos_image, const float th, const float ph)
{
  return transform(img, pos_image, th, ph, img.rows, img.cols);
}

float ImageEvaluator::evaluate(const cv::Mat &img1, const cv::Mat &img2) {
  float error_pixels = 0;

#ifdef DEBUG_IE
  int mode_backup = mode;
  mode = IE_MODE_PIXELS;
#endif

  if(mode == IE_MODE_PIXELS) {
    int pixels = img1.rows * img1.cols;

    for(int r = 0; r < img1.rows; ++r)
      for(int c = 0; c < img1.cols; ++c)
        if(img1.at<uchar>(r, c) == 0 || img2.at<uchar>(r, c) == 0)
          --pixels;
        else
          error_pixels += fabs( (float)img1.at<uchar>(r, c) - img2.at<uchar>(r, c) );

    if(pixels == 0)
      error_pixels = 1;
    else
      error_pixels /= 255 * pixels;
  }

  float error_centroids = 0;

#ifdef DEBUG_IE
  mode = IE_MODE_CENTROIDS;
#endif

  if(mode == IE_MODE_CENTROIDS) {
    float map_m00 = 0;
    float map_m10 = 0;
    float map_m01 = 0;
    float img_m00 = 0;
    float img_m10 = 0;
    float img_m01 = 0;

    for(int r = 0; r < img1.rows; ++r)
      for(int c = 0; c < img1.cols; ++c) {
        if(img1.at<uchar>(r, c) != 0) {
          map_m00 += img1.at<uchar>(r, c);
          map_m10 += img1.at<uchar>(r, c) * c;
          map_m01 += img1.at<uchar>(r, c) * r;
        }

        if(img2.at<uchar>(r, c) != 0) {
          img_m00 += img2.at<uchar>(r, c);
          img_m10 += img2.at<uchar>(r, c) * c;
          img_m01 += img2.at<uchar>(r, c) * r;
        }
      }

    error_centroids = fabs(map_m10 / map_m00 - img_m10 / img_m00) / img1.cols
      + fabs(map_m01 / map_m00 - img_m01 / img_m00) / img1.rows;
  }

#ifdef DEBUG_IE
  cv::resize(img2, d_win_img(cv::Rect(0 ,0, 360, 240) ), d_win_size, 0, 0, cv::INTER_NEAREST);
  cv::resize(img1, d_win_img(cv::Rect(380 ,0, 360, 240) ), d_win_size, 0, 0, cv::INTER_NEAREST);
  cv::imshow("test", d_win_img);

  mode = mode_backup;

  printf("\n========== error: ==========\n");
  printf("pixelwise: %f\n", error_pixels);
  printf("centroids: %f\n", error_centroids);
#endif

  if(mode == IE_MODE_CENTROIDS)
    return error_centroids;

  return error_pixels;
}

} /* namespace cps2 */
