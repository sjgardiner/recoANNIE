// standard library includes
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

// reco-annie includes
#include "annie/annie_math.hh"
#include "annie/RawAnalyzer.hh"
#include "annie/RawChannel.hh"

// Anonymous namespace for definitions local to this source file
namespace {

  // The number of samples to use per minibuffer when computing baseline
  // means using the ZE3RA method
  constexpr size_t NUM_BASELINE_SAMPLES = 25;

  // All F-distribution probabilities below this value will pass the
  // variance consistency test in ze3ra_baseline()
  constexpr double Q_CRITICAL = 1e-4;

  // Computes the sample mean and sample var for a vector of numerical
  // values. Based on http://tinyurl.com/mean-var-onl-alg.
  template<typename ElementType> void compute_mean_and_var(
    const std::vector<ElementType>& data, double& mean, double& var,
    size_t sample_cutoff = std::numeric_limits<size_t>::max())
  {
    if ( data.empty() || sample_cutoff == 0) {
      mean = std::numeric_limits<double>::quiet_NaN();
      var = mean;
      return;
    }
    else if (data.size() == 1 || sample_cutoff == 1) {
      mean = data.front();
      var = 0.;
      return;
    }

    size_t num_samples = 0;
    double mean_x2 = 0.;
    mean = 0.;

    for (const ElementType& x : data) {
      ++num_samples;
      double delta = x - mean;
      mean += delta / num_samples;
      double delta2 = x - mean;
      mean_x2 = delta * delta2;
      if (num_samples == sample_cutoff) break;
    }

    var = mean_x2 / (num_samples - 1);
    return;
  }

}

annie::RawAnalyzer::RawAnalyzer()
{
}

const annie::RawAnalyzer& annie::RawAnalyzer::Instance() {

  // Create the raw analyzer using a static variable. This ensures
  // that the singleton instance is only created once.
  static std::unique_ptr<annie::RawAnalyzer>
    the_instance( new annie::RawAnalyzer() );

  // Return a reference to the singleton instance
  return *the_instance;
}

double annie::RawAnalyzer::ze3ra_baseline(const annie::RawChannel& channel)
  const
{
  // Signal ADC means, variances, and F-distribution probability values
  // ("Q") for the first NUM_BASELINE_SAMPLES from each minibuffer
  std::vector<double> means;
  std::vector<double> variances;
  std::vector<double> Qs;

  // Compute the signal ADC mean and variance for each raw data minibuffer
  for (size_t mb = 0; mb < channel.num_minibuffers(); ++mb) {
    auto mb_data = channel.minibuffer_data(mb);

    double mean, var;
    compute_mean_and_var(mb_data, mean, var, NUM_BASELINE_SAMPLES);

    means.push_back(mean);
    variances.push_back(var);
  }

  // Compute probabilities for the F-distribution test for each minibuffer
  for (size_t j = 0; j < variances.size() - 1; ++j) {
    double sigma2_j = variances.at(j);
    double sigma2_jp1 = variances.at(j + 1);
    double F;
    if (sigma2_j > sigma2_jp1) F = sigma2_j / sigma2_jp1;
    else F = sigma2_jp1 / sigma2_j;

    double nu = (NUM_BASELINE_SAMPLES - 1) / 2.;
    double Q = std::tgamma(2*nu)
      * annie_math::Incomplete_Beta_Function(1. / (1. + F), nu, nu)
      / (2. * std::tgamma(nu));

    Qs.push_back(Q);
  }

  // Compute the mean and standard deviation of the baseline signal
  // for this RawChannel using the mean and standard deviation from
  // each minibuffer whose F-distribution probability falls below
  // the critical value.
  double baseline_mean = 0.;
  double baseline_std_dev = 0.;
  size_t num_passing = 0;
  for (size_t k = 0; k < Qs.size(); ++k) {
    if (Qs.at(k) < Q_CRITICAL) {
      ++num_passing;
      baseline_mean += means.at(k);
      baseline_std_dev += std::sqrt( variances.at(k) );
    }
  }

  if (num_passing > 0) {
    baseline_mean /= num_passing;
    baseline_std_dev /= num_passing;
  }
  else {
    // If none of the minibuffers passed the F-distribution test,
    // choose the one closest to passing and adopt its baseline statistics
    // TODO: consider changing this approach
    auto min_iter = std::min_element(Qs.cbegin(), Qs.cend());
    int min_index = std::distance(Qs.cbegin(), min_iter);

    baseline_mean = means.at(min_index);
    baseline_std_dev = std::sqrt( variances.at(min_index) );
  }

  return baseline_mean;
}
