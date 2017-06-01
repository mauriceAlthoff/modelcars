#ifndef PARTICLE_FILTER_DUMMY_HPP_
#define PARTICLE_FILTER_DUMMY_HPP_

#include <math.h>
#include <random>
#include <vector>
#include <opencv2/core.hpp>
#include "map.hpp"
#include "image_evaluator.hpp"

namespace dummy {

struct Particle {
  cv::Point3f p;
  float weight;
};

class ParticleFilter {
public:

ParticleFilter(cps2::Map *_map, int errorfunction, int _particles_num, float particles_keep, float particle_stddev) {
  particles_num = _particles_num;
  map           = _map;
  ie            = new cps2::ImageEvaluator(*map, errorfunction);

  std::uniform_real_distribution<float> distx(0, map->img_gray.cols);
  std::uniform_real_distribution<float> disty(0, map->img_gray.rows);
  std::uniform_real_distribution<float> distt(0, 6.28);

  for(int i = 0; i < particles_num; ++i) {
    Particle pp;
    pp.p = cv::Point3f(distx(gen), disty(gen), distt(gen));
    particles.push_back(pp);
  }
}

~ParticleFilter() {

}

void evaluate(cv::Mat img, float dx, float dy) {
  best.weight = 1;

  for(std::vector<Particle>::iterator p = particles.begin(); p < particles.end(); ++p) {
    p->p.x = fmax(0, fmin(map->img_gray.cols, p->p.x + dx) );
    p->p.y = fmax(0, fmin(map->img_gray.rows, p->p.y + dy) );

    p->weight = ie->evaluate(img, p->p);

    if(p->weight < best.weight)
      best = *p;
  }

  for(std::vector<Particle>::iterator p = particles.begin(); p < particles.end(); ++p) {
    std::normal_distribution<float> distx(p->p.x, 100 * p->weight);
    std::normal_distribution<float> disty(p->p.y, 100 * p->weight);
    std::normal_distribution<float> distt(p->p.z,   1 * p->weight);
    p->p = cv::Point3f(distx(gen), disty(gen), distt(gen) );
  }
}

Particle getBest() {
  return best;
}

int particles_num;
std::vector<Particle> particles;
std::default_random_engine gen;
cps2::ImageEvaluator *ie;
cps2::Map *map;
Particle best;
};
}

#endif /* PARTICLE_FILTER_DUMMY_HPP_ */
