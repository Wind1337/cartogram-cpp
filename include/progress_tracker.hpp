#ifndef PROGRESS_TRACKER_H
#define PROGRESS_TRACKER_H

#include "indicators.hpp"
#include "inset_state.hpp"

class ProgressTracker
{
public:
  // Constructor
  explicit ProgressTracker(double);

  // Destructor
  ~ProgressTracker();

  // Method to update the progress
  void print_progress_mid_integration(const InsetState &);
  void update_and_print_progress_end_integration(const InsetState &);

  // Method to print the current progress
  void print_progress(const double);

  // Method to print the progress bar
  void print_progress_bar(const double);

private:
  double total_geo_divs_;  // Total number of GeoDivs to monitor progress
  double progress_;  // Progress measured on a scale from 0 (start) to 1 (end)
  indicators::ProgressBar bar_{
    indicators::option::BarWidth{75},
    indicators::option::Start{"["},
    indicators::option::Fill{"■"},
    indicators::option::Lead{"■"},
    indicators::option::Remainder{"-"},
    indicators::option::End{"]"},
    indicators::option::FontStyles{
      std::vector<indicators::FontStyle>{indicators::FontStyle::bold}},
    indicators::option::ShowPercentage{true},
    indicators::option::ShowElapsedTime{true},
    indicators::option::Stream{std::cerr}};
};

#endif  // PROGRESS_TRACKER_H