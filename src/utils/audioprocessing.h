#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

#include <cstddef>
#include <deque>
#include <vector>
#include "fftw3.h"
#include <functional>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

class AGC {
  private:
    float desired_level;
    float attack_coeff;
    float release_coeff;
    float fast_attack_coeff;
    float am_attack_coeff;
    float am_release_coeff;
    size_t look_ahead_samples;
    std::vector<float> gains;
    std::deque<float> lookahead_buffer;
    std::deque<float> lookahead_max;
    float sample_rate;
    float max_gain;  // Maximum allowed gain


    // Noise Blanker
    bool nb_enabled;
    size_t nb_fft_size;
    size_t nb_overlap;
    size_t nb_average_windows;
    float nb_threshold;
     int nb_window_size;
    float nb_threshold_factor;
    float nb_smoothing_factor;
    std::vector<float> nb_buffer;
    std::vector<std::vector<float>> nb_spectrum_history;
    std::vector<float> nb_spectrum_average;
    size_t nb_history_index;
    fftwf_plan nb_fft_plan;
    fftwf_plan nb_ifft_plan;
    fftwf_complex *nb_fft_in, *nb_fft_out;
    
    // Hang system
    size_t hang_time;
    size_t hang_counter;
    float hang_threshold;

    void push(float sample);
    void pop();
    float max();
    void applyProgressiveAGC(float desired_gain);
    void applyNoiseBlanker(std::vector<float>& buffer);

  public:
    AGC(float desiredLevel = 0.1f, float attackTimeMs = 50.0f,
        float releaseTimeMs = 300.0f, float lookAheadTimeMs = 10.0f,
        float sr = 44100.0f);
    void process(float *arr, size_t len);
    void reset();
    void set_attack_coeff(float coeff) { attack_coeff = coeff; }
    void set_release_coeff(float coeff) { release_coeff = coeff; }
};

#endif
