// standard library includes
#include <cmath>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

// ROOT includes
#include "TChain.h"
#include "TF1.h"
#include "TFile.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TLegend.h"
#include "TMultiGraph.h"
#include "TTree.h"

// reco-annie includes
#include "annie/Constants.hh"
#include "annie/RecoPulse.hh"
#include "annie/RecoReadout.hh"

constexpr double VETO_TIME = 1e3; // ns

constexpr int BEAM_MINIBUFFER_LABEL = 1;
constexpr int NCV_MINIBUFFER_LABEL = 2;
constexpr int SOURCE_MINIBUFFER_LABEL = 4;

constexpr int NUM_HEFTY_MINIBUFFERS = 40;

constexpr double FREYA_NONHEFTY_TIME_OFFSET = 2e3; // ns
constexpr double FREYA_HEFTY_TIME_OFFSET = 0; // ns

constexpr double NONHEFTY_FIT_START_TIME = 2e4; // ns
constexpr double HEFTY_FIT_START_TIME = 1e4; // ns
constexpr double FIT_END_TIME = 8e4; // ns

constexpr double MM_TO_CM = 1e-1;
constexpr double ASSUMED_NCV_HORIZONTAL_POSITION_ERROR = 3.; // cm
constexpr double ASSUMED_NCV_VERTICAL_POSITION_ERROR = 3.; // cm

constexpr int NUM_TIME_BINS = 100;

constexpr int TANK_CHARGE_WINDOW_LENGTH = 40; // ns
constexpr int UNIQUE_WATER_PMT_CUT = 8; // PMTs
constexpr double TANK_CHARGE_CUT = 3.; // nC

constexpr unsigned short PULSE_TOO_BIG = 430; // ADC counts

constexpr long long COINCIDENCE_TOLERANCE = 40; // ns

constexpr double SIGNAL_WINDOW_TIME = 6e4; // ns

struct ValueAndError {
  double value;
  double error;
  ValueAndError(double val = 0., double err = 0.) : value(val),
    error(err) {}
};

ValueAndError neutron_excess(TH1D& hist, bool hefty_mode)
{
  int b_low, b_high, Nb, s_low, s_high, Ns;
  if (!hefty_mode) {
    // Search for background counts on [800, 8e3) ns
    b_low = hist.FindBin(800.);
    b_high = hist.FindBin(8e3);
    Nb = b_high - b_low;

    // Search for signal counts on [2e4, 8e4) ns
    s_low = hist.FindBin(2e4);
    s_high = hist.FindBin(8e4);
    Ns = s_high - s_low;
  }
  else { // Hefty mode

    // Search for background counts on [7.28e4, 8e4) ns
    b_low = hist.FindBin(7.28e4);
    b_high = hist.FindBin(8e4);
    Nb = b_high - b_low;

    // Search for signal counts on [1e4, 7e4) ns
    // (actually [9.6e3, 6.96e4) when using 100 bins)
    s_low = hist.FindBin(1e4);
    s_high = hist.FindBin(7e4);
    Ns = s_high - s_low;
  }

  double sum_bi = 0.;
  double sum_nj = 0.;

  // Versions of the variables above that always assume an error of 1
  // when the bin count is zero.
  // TODO: revisit this, consider reverting to previous procedure
  double error_sum_bi = 0.;
  double error_sum_nj = 0.;

  // TODO: include background subtraction again

  //for (int i = b_low; i < b_high; ++i) {
  //  sum_bi += hist.GetBinContent(i);
  //  error_sum_bi += std::max(hist.GetBinContent(i), 1.);
  //}

  for (int j = s_low; j < s_high; ++j) {
    sum_nj += hist.GetBinContent(j);
    error_sum_nj += std::max(hist.GetBinContent(j), 1.);
  }

  double Bbin = sum_bi / Nb;

  double S = sum_nj - Ns * Bbin;
  double S_error = std::sqrt( Ns * error_sum_bi / (Nb*Nb) + error_sum_nj);

  return ValueAndError(S, S_error);
}

// Put all analysis cuts here (will be applied for both Hefty and non-Hefty
// modes in the same way)
bool approve_event(double event_time, double old_time, const annie::RecoPulse&
  first_ncv1_pulse, const annie::RecoReadout& readout, int minibuffer_index)
{
  if (event_time <= old_time + VETO_TIME) return false;

  int num_unique_water_pmts = BOGUS_INT;
  double tank_charge = readout.tank_charge(minibuffer_index,
    first_ncv1_pulse.start_time(), first_ncv1_pulse.start_time()
    + TANK_CHARGE_WINDOW_LENGTH, num_unique_water_pmts);

  if (num_unique_water_pmts >= UNIQUE_WATER_PMT_CUT) return false;
  if (tank_charge >= TANK_CHARGE_CUT) return false;

  //// Veto the entire readout if there is a really big NCV PMT #1 pulse
  //for (const auto& pulse : readout.get_pulses(4, 1, minibuffer_index)) {
  //  if (pulse.raw_amplitude() > PULSE_TOO_BIG) return false;
  //}

  // NCV coincidence cut
  long long ncv1_time = first_ncv1_pulse.start_time();
  bool found_coincidence = false;
  for ( const auto& pulse : readout.get_pulses(18, 0, minibuffer_index) ) {
    long long ncv2_time = pulse.start_time();
    if ( std::abs( ncv1_time - ncv2_time ) < COINCIDENCE_TOLERANCE ) {
      found_coincidence = true;
      break;
    }
  }

  if (!found_coincidence) return false;

  return true;
}


TH1D make_nonhefty_timing_hist(TChain& reco_readout_chain,
  double norm_factor, const std::string& name, const std::string& title)
{
  TH1D time_hist(name.c_str(), title.c_str(), NUM_TIME_BINS, 0., 8e4);

  annie::RecoReadout* rr = nullptr;
  reco_readout_chain.SetBranchAddress("reco_readout", &rr);

  int num_entries = reco_readout_chain.GetEntries();
  for (int i = 0; i < num_entries; ++i) {
    if (i % 1000 == 0) std::cout << "Entry " << i << " of "
      << num_entries << '\n';
    reco_readout_chain.GetEntry(i);

    const std::vector<annie::RecoPulse>& ncv1_pulses
      = rr->get_pulses(4, 1, 0);

    double old_time = std::numeric_limits<double>::lowest(); // ns
    for (const auto& pulse : ncv1_pulses) {

      double event_time = static_cast<double>( pulse.start_time() );

      if ( approve_event(event_time, old_time, pulse, *rr, 0) ) {

        time_hist.Fill(event_time);

        old_time = event_time;
      }
    }
  }

  time_hist.Scale(norm_factor);
  return time_hist;
}

TH1D make_hefty_timing_hist(TChain& reco_readout_chain,
  TChain& heftydb_chain, double norm_factor, const std::string& name,
  const std::string& title)
{
  TH1D time_hist(name.c_str(), title.c_str(), NUM_TIME_BINS, 0., 8e4);

  annie::RecoReadout* rr = nullptr;
  reco_readout_chain.SetBranchAddress("reco_readout", &rr);

  int db_SequenceID;
  int db_Label[40];
  int db_TSinceBeam[40]; // ns
  int db_More[40]; // Only element 39 is currently meaningful

  heftydb_chain.SetBranchAddress("SequenceID", &db_SequenceID);
  heftydb_chain.SetBranchAddress("Label", &db_Label);
  heftydb_chain.SetBranchAddress("TSinceBeam", &db_TSinceBeam);
  heftydb_chain.SetBranchAddress("More", &db_More);

  int num_entries = reco_readout_chain.GetEntries();
  for (int i = 0; i < num_entries; ++i) {
    if (i % 1000 == 0) std::cout << "Entry " << i << " of "
      << num_entries << '\n';

    reco_readout_chain.GetEntry(i);
    heftydb_chain.GetEntry(i);

    if (db_SequenceID != rr->sequence_id()) {
      throw std::runtime_error("SequenceID mismatch between the RecoReadout"
        " and heftydb trees\n");
    }

    for (int m = 0; m < NUM_HEFTY_MINIBUFFERS; ++m) {

      // Only find events in beam or NCV self-trigger minibuffers
      // (the current Hefty mode timing scripts do not calculate a valid
      // TSinceBeam value for the other minibuffer labels)
      if (db_Label[m] != BEAM_MINIBUFFER_LABEL
        && db_Label[m] != SOURCE_MINIBUFFER_LABEL
        && db_Label[m] != NCV_MINIBUFFER_LABEL) continue;

      const std::vector<annie::RecoPulse>& ncv1_pulses
        = rr->get_pulses(4, 1, m);

      if (ncv1_pulses.empty()) continue;

      double old_time = std::numeric_limits<double>::lowest(); // ns
      for (const auto& pulse : ncv1_pulses) {
        double event_time = static_cast<double>( pulse.start_time() );

        // Add the offset of the current minibuffer to the pulse start time.
        // Assume an offset of zero for source trigger minibuffers (TSinceBeam
        // is not currently calculated for those).
        if (db_Label[m] != SOURCE_MINIBUFFER_LABEL) {
          event_time += db_TSinceBeam[m];
        }

        if ( approve_event(event_time, old_time, pulse, *rr, m) ) {

          time_hist.Fill(event_time);

          old_time = event_time;
        }
      }
    }
  }

  time_hist.Scale(norm_factor);
  return time_hist;
}


// Returns the approximate lower bound on the efficiency of Hefty mode
double make_efficiency_plot(TFile& output_file) {

  std::cout << "Opening position #1 source data\n";
  TChain source_data_chain("reco_readout_tree");
  source_data_chain.Add("/annie/data/users/gardiner/reco-annie/"
    "source_data_pos1.root");

  std::cout << "Analyzing position #1 source data\n";
  TH1D source_data_hist = make_nonhefty_timing_hist(source_data_chain,
    1. / source_data_chain.GetEntries(), "nonhefty_pos1_source_data_hist",
    "Position #1 source data event times");

  // TODO: go back to using position #8 source data when you finish
  // the new RAT-PAC simulation

  //std::cout << "Opening position #8 source data\n";
  //TChain source_data_chain("reco_readout_tree");
  //source_data_chain.Add("/annie/data/users/gardiner/reco-annie/"
  //  "nonhefty_source_data_pos8.root");

  //std::cout << "Analyzing position #8 source data\n";
  //TH1D source_data_hist = make_nonhefty_timing_hist(source_data_chain,
  //  1. / source_data_chain.GetEntries(), "nonhefty_pos8_source_data_hist",
  //  "Position #8 source data event times");

  std::cout << "Opening FREYA + RAT-PAC simulation results\n";
  TFile freya_file("/annie/app/users/gardiner/ratpac_ana/"
    "NEW_freya_evap_capture_times.root", "read");
  TTree* freya_tree = nullptr;
  freya_file.GetObject("capture_times_tree", freya_tree);

  double freya_capture_time = 0.;
  freya_tree->SetBranchAddress("capture_time", &freya_capture_time);

  TH1D freya_hist("freya_hist", "FREYA + RATPAC capture times", NUM_TIME_BINS,
    0., 8e4);
  int num_entries = freya_tree->GetEntries();
  for (int i = 0; i < num_entries; ++i) {
    freya_tree->GetEntry(i);
    freya_hist.Fill(freya_capture_time + FREYA_NONHEFTY_TIME_OFFSET);
  }
  freya_hist.Scale(1e-6);

  std::cout << "Fitting simulation + flat background to data\n";
  auto temp_func = [&freya_hist](double* x, double* p) {
    double xx = x[0];
    int bin = freya_hist.FindBin(xx);
    return p[0] * freya_hist.GetBinContent(bin) + p[1];
  };

  TF1 eff_fit_func("eff_fit_func", temp_func, 0., 1e5, 2);
  eff_fit_func.SetParameters(1., 1e-3);

  source_data_hist.Fit(&eff_fit_func, "", "", 2400, 8e4);

  double efficiency_lower_bound = eff_fit_func.GetParameter(0);
  std::cout << "Estimate of NCV efficiency = " << efficiency_lower_bound
    << '\n';

  std::unique_ptr<TH1D> eff_hist( dynamic_cast<TH1D*>(
    freya_hist.Clone("eff_hist") ) );
  eff_hist->Scale(efficiency_lower_bound);
  for (int b = 1; b <= eff_hist->GetNbinsX(); ++b) eff_hist->SetBinContent(b,
    eff_hist->GetBinContent(b) + eff_fit_func.GetParameter(1));

  source_data_hist.SetLineColor(kBlack);
  source_data_hist.SetLineWidth(2);
  source_data_hist.GetFunction("eff_fit_func")->SetBit(TF1::kNotDraw);
  //source_data_hist.GetFunction("eff_fit_func")->Delete();

  eff_hist->SetLineWidth(2);
  eff_hist->SetLineColor(kBlue);
  eff_hist->SetTitle("Scaled FREYA/RAT-PAC prediction + flat background");

  output_file.cd();

  eff_fit_func.Write();
  source_data_hist.Write();
  eff_hist->Write();

  return efficiency_lower_bound;
}

// Returns the approximate lower bound on the efficiency of HeftySource mode
double make_hefty_efficiency_plot(TFile& output_file) {

  std::cout << "Opening position #8 source data\n";
  TChain source_data_chain("reco_readout_tree");
  source_data_chain.Add("/annie/data/users/gardiner/reco-annie/"
    "r830.root");

  std::cout << "Opening position #8 hefty timing data\n";
  TChain source_heftydb_chain("heftydb");
  source_heftydb_chain.Add("/annie/data/users/gardiner/reco-annie/timing/"
    "timing_r830.root");

  // TODO: remove hard-coded calibration trigger label here
  long number_of_source_triggers = source_heftydb_chain.Draw("Label[]",
    "Label[] == 4", "goff");
  double norm_factor = 1. / number_of_source_triggers;

  std::cout << "Analyzing position #8 source data\n";
  TH1D source_data_hist = make_hefty_timing_hist(source_data_chain,
    source_heftydb_chain, norm_factor, "hefty_pos8_source_data_hist",
    "Position #8 source data event times");

  // TODO: redo simulation with position #8 HeftySource configuration
  std::cout << "Opening FREYA + RAT-PAC simulation results\n";
  TFile freya_file("/annie/app/users/gardiner/ratpac_ana/"
    "NEW_freya_evap_capture_times_POS8.root", "read");
  TTree* freya_tree = nullptr;
  freya_file.GetObject("capture_times_tree", freya_tree);

  double freya_capture_time = 0.;
  freya_tree->SetBranchAddress("capture_time", &freya_capture_time);

  TH1D freya_hist("freya_hist", "FREYA + RATPAC capture times", NUM_TIME_BINS,
    0., 8e4);
  int num_entries = freya_tree->GetEntries();
  for (int i = 0; i < num_entries; ++i) {
    freya_tree->GetEntry(i);
    freya_hist.Fill(freya_capture_time + FREYA_HEFTY_TIME_OFFSET);
  }
  freya_hist.Scale(1e-6);

  std::cout << "Fitting simulation + flat background to data\n";
  auto temp_func = [&freya_hist](double* x, double* p) {
    double xx = x[0];
    int bin = freya_hist.FindBin(xx);
    return p[0] * freya_hist.GetBinContent(bin) + p[1];
  };

  TF1 hefty_eff_fit_func("hefty_eff_fit_func", temp_func, 0., 1e5, 2);
  hefty_eff_fit_func.SetParameters(1., 1e-3);

  source_data_hist.Fit(&hefty_eff_fit_func, "", "", 800, 8e4);

  double efficiency_lower_bound = hefty_eff_fit_func.GetParameter(0);
  std::cout << "Estimate of NCV efficiency = " << efficiency_lower_bound
    << '\n';

  std::unique_ptr<TH1D> hefty_eff_hist( dynamic_cast<TH1D*>(
    freya_hist.Clone("hefty_eff_hist") ) );
  hefty_eff_hist->Scale(efficiency_lower_bound);
  for (int b = 1; b <= hefty_eff_hist->GetNbinsX(); ++b)
    hefty_eff_hist->SetBinContent(b, hefty_eff_hist->GetBinContent(b)
      + hefty_eff_fit_func.GetParameter(1));

  source_data_hist.SetLineColor(kBlack);
  source_data_hist.SetLineWidth(2);
  source_data_hist.GetFunction("hefty_eff_fit_func")->SetBit(TF1::kNotDraw);
  //source_data_hist.GetFunction("hefty_eff_fit_func")->Delete();

  hefty_eff_hist->SetLineWidth(2);
  hefty_eff_hist->SetLineColor(kBlue);
  hefty_eff_hist->SetTitle("Scaled FREYA/RAT-PAC prediction + flat background");

  output_file.cd();

  hefty_eff_fit_func.Write();
  source_data_hist.Write();
  hefty_eff_hist->Write();

  return efficiency_lower_bound;
}

// Returns the estimated neutron event rate (in neutrons / POT)
ValueAndError make_timing_distribution(
  const std::initializer_list<int>& runs, int ncv_position, TFile& output_file,
  bool hefty_mode, long spills, double pot, double efficiency,
  double background_events_per_ns)
{
  TChain rch("reco_readout_tree");
  TChain hch("heftydb");

  for (const auto& run : runs) {
    std::stringstream temp_ss;

    temp_ss << "/annie/data/users/gardiner/reco-annie/r" << run << ".root";
    rch.Add( temp_ss.str().c_str() );

    if (hefty_mode) {
      temp_ss = std::stringstream();

      temp_ss << "/annie/data/users/gardiner/reco-annie/timing/timing_r"
        << run << ".root";
      //temp_ss << "/pnfs/annie/persistent/users/gardiner/hefty_timing/";
      //temp_ss << std::setfill('0') << std::setw(4);
      //temp_ss << run << "/*.root";

      hch.Add( temp_ss.str().c_str() );
    }
  }

  std::string pos_str = std::to_string(ncv_position);
  std::string name("pos_" + pos_str + "_time_hist");
  std::string title("position " + pos_str + " event time distribution");

  std::cout << "Creating " << title << '\n';

  std::string func_name = "f1_pos" + pos_str;
  TF1 temp_f1(pos_str.c_str(), "[0]*exp(-x / [1]) + [2]", 0., 1e5);
  temp_f1.SetParameters(500., 1e4, 20.);

  std::unique_ptr<TH1D> temp_hist = std::make_unique<TH1D>();
  ValueAndError rate_with_error;
  if (!hefty_mode) {
    *temp_hist = make_nonhefty_timing_hist(rch, 1., name, title);
    temp_hist->Fit(&temp_f1, "IL", "", NONHEFTY_FIT_START_TIME, FIT_END_TIME);
    // TODO: do something with the fit result
    rate_with_error = neutron_excess(*temp_hist, false);
  }
  else {
    *temp_hist = make_hefty_timing_hist(rch, hch, 1., name, title);
    temp_hist->Fit(&temp_f1, "IL", "", HEFTY_FIT_START_TIME, FIT_END_TIME);
    // TODO: do something with the fit result
    rate_with_error = neutron_excess(*temp_hist, true);
  }

  // TODO: scrutinize this carefully
  double expected_background_events = SIGNAL_WINDOW_TIME
    * background_events_per_ns * spills;
  std::cout << "DEBUG: rate = " << rate_with_error.value << " +- "
    << rate_with_error.error << ", bckgd = " << expected_background_events
    << '\n';

  // Background event counts are distributed according to a Poisson
  // distribution, so the error on the expected number of background counts is
  // the square root of the mean
  //DEBUG rate_with_error.value -= expected_background_events;
  //DEBUG rate_with_error.error += std::sqrt(expected_background_events);

  double norm_factor = 1. / (pot * efficiency);

  rate_with_error.value *= norm_factor;
  rate_with_error.error *= norm_factor;

  temp_hist->Scale(norm_factor);
  temp_hist->GetXaxis()->SetTitle("time (ns)");
  temp_hist->GetYaxis()->SetTitle("events / POT");

  output_file.cd();
  temp_hist->Write();

  return rate_with_error;
}

// Returns the "soft" event rate in events / ns
double compute_nonhefty_soft_rate() {
  std::cout << "Opening position #8 soft data\n";
  TChain soft_chain("reco_readout_tree");
  soft_chain.Add("/annie/data/users/gardiner/reco-annie/"
    "r856.root");

  annie::RecoReadout* rr = nullptr;
  soft_chain.SetBranchAddress("reco_readout", &rr);

  std::cout << "Computing background pulse rate using soft data\n";
  long num_pulses = 0;
  long num_entries = soft_chain.GetEntries();
  for (long i = 0; i < num_entries; ++i) {
    if (i % 1000 == 0) std::cout << "Entry " << i << " of "
      << num_entries << '\n';
    soft_chain.GetEntry(i);

    const std::vector<annie::RecoPulse>& ncv1_pulses
      = rr->get_pulses(4, 1, 0);

    double old_time = std::numeric_limits<double>::lowest(); // ns
    for (const auto& pulse : ncv1_pulses) {

      double event_time = static_cast<double>( pulse.start_time() );

      if ( approve_event(event_time, old_time, pulse, *rr, 0) ) {
        ++num_pulses;
        old_time = event_time;
      }
    }
  }

  double soft_rate = static_cast<double>(num_pulses) / (num_entries * 8e4);

  std::cout << "Found " << num_pulses << " pulses in " << num_entries
    << " soft triggers\n";
  std::cout << "Background pulse rate = " << soft_rate << " pulses / ns\n";

  return soft_rate;
}

int main(int argc, char* argv[]) {

  if (argc < 2) {
    std::cout << "Usage: crank OUTPUT_FILE\n";
    return 1;
  }

  TFile out_file(argv[1], "recreate");

  double nonhefty_soft_rate = compute_nonhefty_soft_rate();

  double nonhefty_efficiency = make_efficiency_plot(out_file);

  // TODO: return to using this when you get a reliable simulated
  // neutron flux for position #8
  //double hefty_efficiency = make_hefty_efficiency_plot(out_file);
  double hefty_efficiency = nonhefty_efficiency;

  // Cartesian coordinates (mm) of the NCV center for each position. Taken
  // from the RAT-PAC simulation by V. Fischer.
  const std::map<int, std::array<double, 3> > position_coordinates = {
    { 1, { -146.1, 0.0, 1597.18 } },
    { 2, { -146.1, 0.0, 276.38  } },
    { 3, {  793.7, 0.0, 276.38  } },
    { 4, { -146.1, 0.0, 1292.33 } },
    { 5, { -146.1, 0.0, 987.53  } },
    { 6, { -146.1, 0.0, 1444.78 } },
    { 7, {  311.1, 0.0, 276.38  } },
    { 8, {  311.1, 0.0, 1597.18 } }
    // "Position 9" is non-Hefty data at position #2
    // { 9, { -146.1, 0.0, 276.38  } },
  };

  // Make the rate plots
  std::map<int, ValueAndError> positions_and_rates = {

    { 1, make_timing_distribution( { 650, 653 }, 1, out_file, false,
      621744, 2.49e18, nonhefty_efficiency, nonhefty_soft_rate ) },

    { 2, make_timing_distribution( { 798 }, 2, out_file, true,
      2938556, 1.42e19, hefty_efficiency, nonhefty_soft_rate )  },

    { 3, make_timing_distribution( { 803 }, 3, out_file, true,
      2296022, 1.33e19, hefty_efficiency, nonhefty_soft_rate )  },

    { 4, make_timing_distribution( { 808, 812 }, 4, out_file, true,
      3801388, 2.43e19, hefty_efficiency, nonhefty_soft_rate ) },

    { 5, make_timing_distribution( { 813 }, 5, out_file, true,
      2233860, 1.34e19, hefty_efficiency, nonhefty_soft_rate ) },

    { 6, make_timing_distribution( { 814 }, 6, out_file, true,
      1070723, 6.20e18, hefty_efficiency, nonhefty_soft_rate ) },

    { 7, make_timing_distribution( { 815 }, 7, out_file, true,
      697089, 4.05e18, hefty_efficiency, nonhefty_soft_rate ) },

    // "Position 9" is non-Hefty data at position #2 (for testing)
    //{ 9, make_timing_distribution( { 705 }, 9, out_file, false,
    //  179272, 6.6195e17, nonhefty_efficiency, nonhefty_soft_rate ) },
  };

  TMultiGraph horizontal_graph;
  TMultiGraph vertical_graph;

  TLegend lg(0.2, 0.2, 0.5, 0.5);
  lg.SetHeader("NCV position");

  std::cout << "*** Estimated neutron event rates ***\n";
  for (const auto& pair : positions_and_rates) {
    int pos = pair.first;
    double rate = pair.second.value;
    double rate_error = pair.second.error;
    std::cout << "NCV position #" << pos << ": " << rate << " neutrons / POT\n";

    TGraphErrors* horiz_gr = new TGraphErrors(1);
    horiz_gr->SetPoint(0, MM_TO_CM * position_coordinates.at(pair.first).at(0),
      rate);
    horiz_gr->SetPointError(0, ASSUMED_NCV_HORIZONTAL_POSITION_ERROR,
      rate_error);
    horiz_gr->SetMarkerColor(pos);
    horiz_gr->SetMarkerStyle(20);

    TGraphErrors* vert_gr = new TGraphErrors(1);
    vert_gr->SetPoint(0, MM_TO_CM * position_coordinates.at(pair.first).at(2),
      rate);
    vert_gr->SetPointError(0, ASSUMED_NCV_VERTICAL_POSITION_ERROR, rate_error);
    vert_gr->SetMarkerColor(pos);
    vert_gr->SetMarkerStyle(20);

    horizontal_graph.Add(horiz_gr);
    vertical_graph.Add(vert_gr);

    lg.AddEntry(vert_gr, std::to_string(pos).c_str(), "lep");
  }

  out_file.cd();

  horizontal_graph.SetTitle("NCV background neutron rates;"
    " NCV horizontal distance from tank center (cm); neutrons / POT");

  vertical_graph.SetTitle("NCV background neutron rates;"
    " NCV center height (cm); neutrons / POT");

  horizontal_graph.Write("horizontal_graph");
  vertical_graph.Write("vertical_graph");
  lg.Write("legend_for_rate_graphs");

  return 0;
}
