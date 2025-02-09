/*
 *  Copyright © 2017-2024 Wellington Wallace
 *
 *  This file is part of Easy Effects.
 *
 *  Easy Effects is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Easy Effects is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Easy Effects. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <atomic>
#include <fftw3.h>
#include <sigc++/signal.h>
#include <sys/types.h>
#include <deque>
#include <span>
#include <string>
#include <vector>
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_statistics.h>
#include <memory>
#include <cmath>
#include "pipe_manager.hpp"
#include "plugin_base.hpp"

class AudioStatistics {
public:
    AudioStatistics(size_t window_size, size_t histogram_bins)
        : window_size_(window_size) {
        // Initialize histogram
        g_message("AudioStatistics gsl_histogram_alloc bins=%zu window_size=%zu",
                  histogram_bins, window_size);
        update_histogram_bins(histogram_bins);
        // Initialize circular buffer for moving window
        buffer_.resize(window_size);
        current_pos_ = 0;
    }

    ~AudioStatistics() {
        if (hist_) {
            gsl_histogram_free(hist_);
            hist_ = nullptr;
        }
    }

    size_t get_window_size() const { return window_size_; }
    size_t get_histogram_bins() const { return hist_->n; }

    // Get moving window statistics
    double get_mean() const {
        if (total_samples_ == 0) return 0.0;
        return gsl_stats_mean(buffer_.data(), 1, total_samples_);
    }

   double get_kurtosis() const {
     if (total_samples_ < 4) return 0.0;

     // Convert dB values (assumed computed as 20*log10(|x|)) back to linear amplitude.
     std::vector<double> linear(total_samples_);
     for (size_t i = 0; i < total_samples_; i++) {
         linear[i] = pow(10.0, buffer_[i] / 20.0);
     }

     // Compute mean and standard deviation on the linear data.
     double mean = gsl_stats_mean(linear.data(), 1, total_samples_);
     double variance = gsl_stats_variance(linear.data(), 1, total_samples_);
     double sd = std::sqrt(variance);
     if (sd == 0.0)
         return 0.0;

     // Use the optimized function that computes kurtosis with precomputed mean and sd.
     return gsl_stats_kurtosis_m_sd(linear.data(), 1, total_samples_, mean, sd);
   }

    void update_histogram_bins(size_t new_bin_count) {
      if (hist_) {
          gsl_histogram_free(hist_);
      }
      g_message("update_histogram_bins %zu", new_bin_count);
      hist_ = gsl_histogram_alloc(new_bin_count);
      this->reset();
      gsl_histogram_set_ranges_uniform(hist_, -120.0, 0.0);
    }

    std::vector<double> get_histogram_data() const {
      std::vector<double> data(hist_->n);

      // Compute the maximum bin count across all bins
      double max_value = 0.0;
      for (size_t i = 0; i < hist_->n; i++) {
          if (hist_->bin[i] > max_value) {
              max_value = hist_->bin[i];
          }
      }

      // Normalize each bin uniformly to [0, 1]
      for (size_t i = 0; i < hist_->n; i++) {
          data[i] = (max_value > 0.0) ? hist_->bin[i] / max_value : 0.0;
      }
      return data;
    }

    void add_sample(float sample) {
      // Validate and clamp the input
      if (std::isnan(sample)) {
          sample = -120.0f;  // Treat NaN as silence
      }

      // Clamp to valid dB range
      double db = std::clamp(static_cast<double>(sample), -120.0, 0.0);
      //g_message("gsl_histogram_increment db=%f current_pos_=%zu", db, current_pos_);
      // Add to circular buffer
      buffer_[current_pos_] = db;
      current_pos_ = (current_pos_ + 1) % window_size_;
      // Update sample count
      if (total_samples_ < window_size_) {
          total_samples_++;
      }
      gsl_histogram_increment(hist_, db);
    }

    // Reset statistics
    void reset() {
        gsl_histogram_reset(hist_);
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        current_pos_ = 0;
        total_samples_ = 0;
    }

private:
    gsl_histogram* hist_ = nullptr;
    std::vector<double> buffer_;
    size_t window_size_;
    size_t current_pos_;
    size_t total_samples_ = 0;
};

class Spectrum : public PluginBase {
 public:
  Spectrum(const std::string& tag,
           const std::string& schema,
           const std::string& schema_path,
           PipeManager* pipe_manager,
           PipelineType pipe_type);
  Spectrum(const Spectrum&) = delete;
  auto operator=(const Spectrum&) -> Spectrum& = delete;
  Spectrum(const Spectrum&&) = delete;
  auto operator=(const Spectrum&&) -> Spectrum& = delete;
  ~Spectrum() override;

  void setup() override;

  void process(std::span<float>& left_in,
               std::span<float>& right_in,
               std::span<float>& left_out,
               std::span<float>& right_out) override;

  auto get_latency_seconds() -> float override;

  std::tuple<uint, uint, double*> compute_magnitudes();  // rate, nbands, magnitudes

  const AudioStatistics& get_statistics() const { return *statistics_; }
  void update_statistics(const std::vector<float>& magnitudes);
  sigc::signal<void(size_t)> signal_histogram_bins_changed;

 private:
  std::atomic<bool> fftw_ready = false;

  fftwf_plan plan = nullptr;

  fftwf_complex* complex_output = nullptr;

  static constexpr uint n_bands = 8192U;

  std::array<float, n_bands> real_input;
  std::array<double, n_bands / 2U + 1U> output;
  
  std::vector<float> left_delayed_vector;
  std::vector<float> right_delayed_vector;
  std::span<float> left_delayed;
  std::span<float> right_delayed;

  std::array<float, n_bands> latest_samples_mono;

  std::array<float, n_bands> hann_window;
  std::unique_ptr<AudioStatistics> statistics_;

  enum {
    DB_BIT_IDX = (1 << 0),      // To which db_buffers array process() should write.
    DB_BIT_NEWDATA = (1 << 1),  // If new data has been written by process().
    DB_BIT_BUSY = (1 << 2),     // If process() is currently writing data.
  };

  std::array<std::array<float, n_bands>, 2> db_buffers;
  std::atomic<int> db_control = {0};
  static_assert(std::atomic<int>::is_always_lock_free);
};
