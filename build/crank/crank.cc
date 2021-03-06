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

// Hefty mode minibuffer labels
constexpr int UNKNOWN_MINIBUFFER_LABEL = 0;
constexpr int BEAM_MINIBUFFER_LABEL = 1;
constexpr int NCV_MINIBUFFER_LABEL = 2;
constexpr int SOURCE_MINIBUFFER_LABEL = 4;
constexpr int COSMIC_MINIBUFFER_LABEL = 3;
constexpr int PERIODIC_MINIBUFFER_LABEL = 5;
constexpr int SOFTWARE_MINIBUFFER_LABEL = 7;
constexpr int MINRATE_MINIBUFFER_LABEL = 6;

constexpr int NUM_HEFTY_MINIBUFFERS = 40;
constexpr double HEFTY_MINIBUFFER_TIME = 2e3; // ns

constexpr double FREYA_NONHEFTY_TIME_OFFSET = 2e3; // ns
constexpr double FREYA_HEFTY_TIME_OFFSET = 0; // ns

constexpr double MM_TO_CM = 1e-1;
constexpr double CM_TO_IN = 1. / 2.54;
constexpr double ASSUMED_NCV_HORIZONTAL_POSITION_ERROR = 3.; // cm
constexpr double ASSUMED_NCV_VERTICAL_POSITION_ERROR = 3.; // cm

constexpr int NUM_TIME_BINS = 100;

constexpr int TANK_CHARGE_WINDOW_LENGTH = 40; // ns
constexpr int UNIQUE_WATER_PMT_CUT = 8; // PMTs
constexpr double TANK_CHARGE_CUT = 3.; // nC

constexpr long long COINCIDENCE_TOLERANCE = 40; // ns

constexpr unsigned int NONHEFTY_BACKGROUND_START_TIME = 10; // ns
constexpr unsigned int NONHEFTY_BACKGROUND_END_TIME = 8000; // ns

constexpr unsigned int NONHEFTY_SIGNAL_START_TIME = 20000; // ns
constexpr unsigned int NONHEFTY_SIGNAL_END_TIME = 80000; // ns

// These times are relative to the start of a beam minibuffer
constexpr unsigned int HEFTY_BACKGROUND_START_TIME = 10; // ns
constexpr unsigned int HEFTY_BACKGROUND_END_TIME = 300; // ns

constexpr unsigned int HEFTY_SIGNAL_START_TIME = 10000; // ns
constexpr unsigned int HEFTY_SIGNAL_END_TIME = 70000; // ns

struct ValueAndError {
  double value;
  double error;

  ValueAndError(double val = 0., double err = 0.) : value(val),
    error(err) {}

  void clear() {
    value = 0.;
    error = 0.;
  }

  ValueAndError& operator*=(double factor) {
    value *= factor;
    error *= factor;
    return *this;
  }

  ValueAndError& operator/=(double factor) {
    value /= factor;
    error /= factor;
    return *this;
  }

  ValueAndError operator-(const ValueAndError& other) {
    return ValueAndError(value - other.value,
      std::sqrt( std::pow(error, 2) + std::pow(other.error, 2) ));
  }

  ValueAndError operator*(double factor) {
    return ValueAndError(factor * value, factor * error);
  }

  ValueAndError operator/(double factor) {
    return ValueAndError(value / factor, error / factor);
  }

};

std::ostream& operator<<(std::ostream& out, const ValueAndError& ve) {
  out << ve.value << " ± " << ve.error;
  return out;
}

//// Only find Hefty mode signal events in beam, neutron calibration source, or
//// NCV self-trigger minibuffers (the current Hefty mode timing scripts do not
//// calculate a valid TSinceBeam value for the other minibuffer labels)
//bool is_signal_minibuffer(int label) {
//  if (label == BEAM_MINIBUFFER_LABEL || label == SOURCE_MINIBUFFER_LABEL
//    || label == NCV_MINIBUFFER_LABEL) return true;
//  else return false;
//}

// Use the software and periodic minibuffers from Hefty mode to
// estimate random-in-time backgrounds (minrate buffers had the LEDs enabled)
bool is_background_minibuffer(int label) {
  if (label == SOFTWARE_MINIBUFFER_LABEL || label == PERIODIC_MINIBUFFER_LABEL
    || label == MINRATE_MINIBUFFER_LABEL)
    return true;
  else return false;
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

TH1D make_nonhefty_timing_hist(
  std::vector<std::unique_ptr<TChain> >& reco_readout_chains,
  double norm_factor, const std::string& name, const std::string& title,
  ValueAndError& raw_signal, ValueAndError& background)
{
  raw_signal.clear();
  background.clear();

  TH1D time_hist(name.c_str(), title.c_str(), NUM_TIME_BINS, 0., 8e4);

  annie::RecoReadout* rr = nullptr;

  int chain_index = 0;

  long long total_entries = 0;
  for (std::unique_ptr<TChain>& reco_readout_chain : reco_readout_chains) {
    reco_readout_chain->SetBranchAddress("reco_readout", &rr);
    std::cout << "Reading chain " << chain_index << '\n';

    long long num_entries = reco_readout_chain->GetEntries();
    total_entries += num_entries;
    for (int i = 0; i < num_entries; ++i) {
      if (i % 1000 == 0) std::cout << "Entry " << i << " of "
        << num_entries << '\n';
      reco_readout_chain->GetEntry(i);

      const std::vector<annie::RecoPulse>& ncv1_pulses
        = rr->get_pulses(4, 1, 0);

      double old_time = std::numeric_limits<double>::lowest(); // ns
      for (const auto& pulse : ncv1_pulses) {

        double event_time = static_cast<double>( pulse.start_time() );

        if ( approve_event(event_time, old_time, pulse, *rr, 0) ) {

          time_hist.Fill(event_time);

          old_time = event_time;

          size_t start_time = pulse.start_time();
          if (start_time >= NONHEFTY_BACKGROUND_START_TIME
            && start_time < NONHEFTY_BACKGROUND_END_TIME)
          {
            background.value += 1.;
          }

          if (start_time >= NONHEFTY_SIGNAL_START_TIME
            && start_time < NONHEFTY_SIGNAL_END_TIME) raw_signal.value += 1.;
        }
      }
    }

    ++chain_index;
  }

  // Poisson errors
  background.error = std::sqrt(background.value);
  raw_signal.error = std::sqrt(raw_signal.value);

  std::cout << "Found " << background << " background events in "
    << total_entries << " non-Hefty buffers\n";

  std::cout << "Found " << raw_signal << " raw signal events in "
    << total_entries << " non-Hefty buffers\n";

  std::cout << "Background rate = " << background
    / ( static_cast<double>(NONHEFTY_BACKGROUND_END_TIME
    - NONHEFTY_BACKGROUND_START_TIME) * total_entries ) << " events / ns\n";

  double background_factor = static_cast<double>(NONHEFTY_SIGNAL_END_TIME
    - NONHEFTY_SIGNAL_START_TIME) / (NONHEFTY_BACKGROUND_END_TIME
    - NONHEFTY_BACKGROUND_START_TIME);

  std::cout << "Expected background counts = "
    << background * background_factor << '\n';

  background *= background_factor * norm_factor;

  raw_signal *= norm_factor;

  time_hist.Scale(norm_factor);
  return time_hist;
}

// Returns a histogram of the event time distribution for Hefty mode data.
TH1D make_hefty_timing_hist(
  std::vector<std::unique_ptr<TChain> >& reco_readout_chains,
  std::vector<std::unique_ptr<TChain> >& heftydb_chains, double norm_factor,
  const std::string& name, const std::string& title, ValueAndError& raw_signal,
  ValueAndError& background)
{
  if ( reco_readout_chains.size() != heftydb_chains.size() ) {
    throw std::runtime_error("TChain size mismatch in make_hefty_"
      + std::string("timing_hist()"));
  }

  raw_signal.clear();
  background.clear();

  // Extra estimate of the background, this time using the (very small)
  // pre-beam region of beam minibuffers
  ValueAndError pre_beam_background;

  TH1D time_hist(name.c_str(), title.c_str(), NUM_TIME_BINS, 0., 8e4);

  // Variables to read from TChain branches
  annie::RecoReadout* rr = nullptr;

  int db_SequenceID;
  int db_Label[40];
  int db_TSinceBeam[40]; // ns
  int db_More[40]; // Only element 39 is currently meaningful
  unsigned long long db_Time[40]; // ns since Unix epoch

  long long num_background_minibuffers = 0;
  long long num_beam_minibuffers = 0;

  for (size_t c = 0; c < heftydb_chains.size(); ++c) {

    std::cout << "Reading chain #" << c << '\n';
    auto& reco_readout_chain = reco_readout_chains.at(c);
    auto& heftydb_chain = heftydb_chains.at(c);

    reco_readout_chain->SetBranchAddress("reco_readout", &rr);

    heftydb_chain->SetBranchAddress("SequenceID", &db_SequenceID);
    heftydb_chain->SetBranchAddress("Label", &db_Label);
    heftydb_chain->SetBranchAddress("TSinceBeam", &db_TSinceBeam);
    heftydb_chain->SetBranchAddress("More", &db_More);
    heftydb_chain->SetBranchAddress("Time", &db_Time);

    // Build index to ensure that you always step through the chains
    // in time order (even if they've been hadd'ed together in some other
    // order). We can exploit the auto-sorting of std::map keys here.
    int num_heftydb_entries = heftydb_chain->GetEntries();

    int num_reco_readout_entries = reco_readout_chain->GetEntries();
    if (num_heftydb_entries != num_reco_readout_entries) {
      throw std::runtime_error("Entry number mismatch between Hefty timing and"
        " annie::RecoReadout chains");
    }

    // Keys are SequenceIDs, values are TChain entry indices
    std::map<int, int> sequenceID_to_entry;
    std::cout << "Building SequenceID index\n";
    for (int idx = 0; idx < num_heftydb_entries; ++idx) {
      heftydb_chain->GetEntry(idx);
      // SequenceIDs should be unique within a run. If we've mixed runs
      // or otherwise mixed them up, complain.
      if ( sequenceID_to_entry.count(db_SequenceID) ) throw std::runtime_error(
        "Duplicate SequenceID value " + std::to_string(db_SequenceID)
        + " encountered!");

      sequenceID_to_entry[db_SequenceID] = idx;
    }
    int last_sequence_id = sequenceID_to_entry.crbegin()->first;

    // TODO: consider whether you should reset this to zero for each readout.
    // Some readouts do not contain any beam trigger minibuffers.
    unsigned long long last_beam_time = 0;

    for (const auto& index_pair : sequenceID_to_entry) {

      int chain_index = index_pair.second;
      reco_readout_chain->GetEntry(chain_index);
      heftydb_chain->GetEntry(chain_index);

      if (db_SequenceID % 1000 == 0) std::cout << "SequenceID "
        << db_SequenceID << " of " << last_sequence_id << '\n';

      if (db_SequenceID != rr->sequence_id()) {
        throw std::runtime_error("SequenceID mismatch between the RecoReadout"
          " and heftydb trees\n");
      }

      for (int m = 0; m < NUM_HEFTY_MINIBUFFERS; ++m) {

        if ( is_background_minibuffer(db_Label[m]) )
          ++num_background_minibuffers;

        // TODO: fix this for HeftySource mode
        else if ( db_Label[m] == BEAM_MINIBUFFER_LABEL) {
          ++num_beam_minibuffers;
          last_beam_time = db_Time[m];
        }

        /// ******** REMOVE AFTER DEBUG

        //if (m == 0) {
        //  std::cerr << "\nSequenceID = " << db_SequenceID;
        //  for (int k = 0; k < NUM_HEFTY_MINIBUFFERS; ++k) std::cerr << ' '
        //    << db_Label[k];
        //}

        //const std::vector<annie::RecoPulse>& rwm_pulses
        //  = rr->get_pulses(21, 2, m);
        //bool found_rwm_pulse = false;
        //for (const auto& pulse : rwm_pulses) {
        //  if (pulse.raw_amplitude() > 2000u) found_rwm_pulse = true;
        //}
        //if (found_rwm_pulse && db_Label[m] == BEAM_MINIBUFFER_LABEL)
        //  std::cerr << " SequenceID = " << db_SequenceID << " minibuffer = "
        //    << m << " has an empty BEAM minibuffer!\n";
        //else if (found_rwm_pulse && db_Label[m] != BEAM_MINIBUFFER_LABEL)
        //  std::cerr << " SequenceID = " << db_SequenceID << " minibuffer = "
        //  << m << " has a NON-beam minibuffer " << db_Label[m] << " with an"
        //    << " RWM pulse!\n";
        /// ******** END REMOVE AFTER DEBUG

        const std::vector<annie::RecoPulse>& ncv1_pulses
          = rr->get_pulses(4, 1, m);

        if (ncv1_pulses.empty()) continue;

        double old_time = std::numeric_limits<double>::lowest(); // ns
        for (const auto& pulse : ncv1_pulses) {
          double event_time = static_cast<double>( pulse.start_time() ); // ns

          // Add the offset of the current minibuffer to the pulse start time.
          // Assume an offset of zero for source trigger minibuffers
          // (TSinceBeam is not currently calculated for those).
          if (db_Label[m] != SOURCE_MINIBUFFER_LABEL) {

            if (last_beam_time == 0) {
              std::cerr << "WARNING: Missing beam time!\n";
            }
            if (db_Time[m] < last_beam_time) throw std::runtime_error(
              "Invalid minibuffer timestamp encountered!");

            // Use the minibuffer timestamps to approximate the time since the
            // beam trigger
            event_time += db_Time[m] - last_beam_time;
          }

          if ( approve_event(event_time, old_time, pulse, *rr, m) ) {

            // Only trust the event time if we know when the last beam spill
            // occurred
            if (last_beam_time != 0) {
              time_hist.Fill(event_time);

              old_time = event_time;

              if (event_time >= HEFTY_SIGNAL_START_TIME
                && event_time < HEFTY_SIGNAL_END_TIME) raw_signal.value += 1.;

              // Find background events
              // TODO: remove hard-coded value and restore time cut
              if ( is_background_minibuffer(db_Label[m])
                /*&& event_time > 1e5*/)
              {
                background.value += 1.;
              }
            }

            else std::cerr << "WARNING: event with unknown beam spill time\n";

            if (db_Label[m] == BEAM_MINIBUFFER_LABEL) {
              size_t mb_start_time = pulse.start_time();
              if (mb_start_time >= HEFTY_BACKGROUND_START_TIME
                && mb_start_time < HEFTY_BACKGROUND_END_TIME)
              {
                pre_beam_background.value += 1.;
              }
            }

          }
        }
      }
    }
  }

  // Poisson errors
  // TODO: consider whether you should enforce an error of 1 for zero counts
  // as you do here.
  background.error = std::max( 1., std::sqrt(background.value) );
  raw_signal.error = std::max( 1., std::sqrt(raw_signal.value) );

  pre_beam_background.error = std::max( 1.,
    std::sqrt(pre_beam_background.value) );

  std::cout << "Found " << background << " background events in "
    << num_background_minibuffers << " minibuffers\n";

  std::cout << "Found " << raw_signal << " raw signal events in "
    << num_beam_minibuffers << " beam spills\n";

  // Convert the raw number of background counts into a rate per nanosecond
  background /= HEFTY_MINIBUFFER_TIME * num_background_minibuffers;

  std::cout << "Background rate = " << background << " events / ns\n";
  std::cout << "Raw signal counts = " << raw_signal << '\n';

  double background_factor = static_cast<double>(HEFTY_SIGNAL_END_TIME
    - HEFTY_SIGNAL_START_TIME) * num_beam_minibuffers;
  std::cout << "Expected background counts = "
    << background * background_factor << '\n';

  std::cout << "Pre-beam background rate = " << pre_beam_background
    / ( static_cast<double>(HEFTY_BACKGROUND_END_TIME
    - HEFTY_BACKGROUND_START_TIME) * num_beam_minibuffers ) << " events / ns\n";

  background *= background_factor * norm_factor;
  raw_signal *= norm_factor;

  time_hist.Scale(norm_factor);

  return time_hist;
}


// Returns the approximate lower bound on the efficiency of Hefty mode
double make_efficiency_plot(TFile& output_file) {

  std::cout << "Opening position #1 source data\n";

  std::vector<std::unique_ptr<TChain> > source_data_chains;
  source_data_chains.emplace_back(new TChain("reco_readout_tree"));
  source_data_chains.back()->Add("/annie/data/users/gardiner/reco-annie/"
    "source_data_pos1.root");

  ValueAndError dummy1, dummy2;
  std::cout << "Analyzing position #1 source data\n";

  long long total_entries = 0;
  for (const auto& sch : source_data_chains) {
    total_entries += sch->GetEntries();
  }

  TH1D source_data_hist = make_nonhefty_timing_hist(source_data_chains,
    1. / total_entries, "nonhefty_pos1_source_data_hist",
    "Position #1 source data event times", dummy1, dummy2);

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
  std::vector<std::unique_ptr<TChain> > source_data_chains;
  source_data_chains.emplace_back(new TChain("reco_readout_tree"));
  source_data_chains.back()->Add("/annie/data/users/gardiner/reco-annie/"
    "r830.root");

  std::cout << "Opening position #8 hefty timing data\n";
  std::vector<std::unique_ptr<TChain> > source_heftydb_chains;
  source_heftydb_chains.emplace_back(new TChain("heftydb"));
  source_heftydb_chains.back()->Add("/annie/data/users/gardiner/reco-annie/"
    "timing/timing_r830.root");

  // TODO: remove hard-coded calibration trigger label here
  long long number_of_source_triggers = 0;
  for (auto& sch : source_heftydb_chains) {
    number_of_source_triggers += sch->Draw("Label[]", "Label[] == 4", "goff");
  }
  double norm_factor = 1. / number_of_source_triggers;

  ValueAndError dummy1, dummy2;
  std::cout << "Analyzing position #8 source data\n";
  TH1D source_data_hist = make_hefty_timing_hist(source_data_chains,
    source_heftydb_chains, norm_factor, "hefty_pos8_source_data_hist",
    "Position #8 source data event times", dummy1, dummy2);

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
  bool hefty_mode, long /*spills*/, double pot, double efficiency)
{
  std::vector<std::unique_ptr<TChain> > reco_chains;
  std::vector<std::unique_ptr<TChain> > hefty_timing_chains;

  for (const auto& run : runs) {
    std::stringstream temp_ss;

    temp_ss << "/annie/data/users/gardiner/reco-annie/r" << run << ".root";

    reco_chains.emplace_back(new TChain("reco_readout_tree"));
    reco_chains.back()->Add( temp_ss.str().c_str() );

    if (hefty_mode) {

      temp_ss = std::stringstream();

      temp_ss << "/annie/data/users/gardiner/reco-annie/timing/timing_r"
        << run << ".root";
      //temp_ss << "/pnfs/annie/persistent/users/gardiner/hefty_timing/";
      //temp_ss << std::setfill('0') << std::setw(4);
      //temp_ss << run << "/*.root";

      hefty_timing_chains.emplace_back(new TChain("heftydb"));
      hefty_timing_chains.back()->Add( temp_ss.str().c_str() );
    }
  }

  std::string pos_str = std::to_string(ncv_position);
  std::string name("pos_" + pos_str + "_time_hist");
  std::string title("position " + pos_str + " event time distribution");

  std::cout << "Creating " << title << '\n';

  std::unique_ptr<TH1D> temp_hist = std::make_unique<TH1D>();
  ValueAndError raw_signal;
  ValueAndError background;

  double norm_factor = 1. / (pot * efficiency);

  if (!hefty_mode) *temp_hist = make_nonhefty_timing_hist(reco_chains,
    norm_factor, name, title, raw_signal, background);

  else *temp_hist = make_hefty_timing_hist(reco_chains, hefty_timing_chains,
    norm_factor, name, title, raw_signal, background);

  temp_hist->GetXaxis()->SetTitle("time (ns)");
  temp_hist->GetYaxis()->SetTitle("events / POT");

  output_file.cd();
  temp_hist->Write();

  std::cout << "Raw event rate = " << raw_signal << " events / POT\n";
  std::cout << "Background = " << background << " events / POT\n";

  return raw_signal; //DEBUG - background;
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

  std::cout << std::scientific;

  if (argc < 2) {
    std::cout << "Usage: crank OUTPUT_FILE\n";
    return 1;
  }

  TFile out_file(argv[1], "recreate");

  //double nonhefty_soft_rate = compute_nonhefty_soft_rate();
  compute_nonhefty_soft_rate();

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

  // Water thickness (inches) for each NCV position. The first value is
  // vertical (overburden), the second is horizontal (shielding on beam side)
  const std::map<int, std::array<double, 2> > position_water_thickness {
    { 1, {  2.25, 40.8125 } },
    { 2, { 54.25, 40.8125 } },
    { 3, { 54.25,  3.8125 } },
    { 4, { 14.25, 40.8125 } },
    { 5, { 26.25, 40.8125 } },
    { 6, {  8.25, 40.8125 } },
    { 7, { 54.25, 22.8125 } },
    { 8, {  2.25, 22.8125 } }
    // "Position 9" is non-Hefty data at position #2
    // { 9, { 54.25, 40.8125 } },
  };

  // Make the rate plots
  std::map<int, ValueAndError> positions_and_rates = {

    { 1, make_timing_distribution( { 650, 653 }, 1, out_file, false,
      621744, 2.676349e18, nonhefty_efficiency ) },

    { 2, make_timing_distribution( { 798 }, 2, out_file, true,
      2938556, 1.42e19, hefty_efficiency )  },

    { 3, make_timing_distribution( { 803 }, 3, out_file, true,
      2296022, 1.33e19, hefty_efficiency )  },

    { 4, make_timing_distribution( { 808, 812 }, 4, out_file, true,
      3801388, 2.43e19, hefty_efficiency ) },

    { 5, make_timing_distribution( { 813 }, 5, out_file, true,
      2233860, 1.34e19, hefty_efficiency ) },

    { 6, make_timing_distribution( { 814 }, 6, out_file, true,
      1070723, 6.20e18, hefty_efficiency ) },

    { 7, make_timing_distribution( { 815 }, 7, out_file, true,
      697089, 4.05e18, hefty_efficiency ) },

    // "Position 9" is non-Hefty data at position #2 (for testing)
    //{ 9, make_timing_distribution( { 705 }, 9, out_file, false,
    //  179272, 6.6195e17, nonhefty_efficiency ) },
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
    std::cout << "NCV position #" << pos << ": " << pair.second
      << " neutrons / POT\n";

    TGraphErrors* horiz_gr = new TGraphErrors(1);
    horiz_gr->SetPoint(0, position_water_thickness.at(pair.first).at(1),
      rate);
    horiz_gr->SetPointError(0, CM_TO_IN * ASSUMED_NCV_HORIZONTAL_POSITION_ERROR,
      rate_error);
    horiz_gr->SetMarkerColor(pos);
    horiz_gr->SetMarkerStyle(20);

    TGraphErrors* vert_gr = new TGraphErrors(1);
    vert_gr->SetPoint(0, position_water_thickness.at(pair.first).at(0),
      rate);
    vert_gr->SetPointError(0, CM_TO_IN * ASSUMED_NCV_VERTICAL_POSITION_ERROR,
      rate_error);
    vert_gr->SetMarkerColor(pos);
    vert_gr->SetMarkerStyle(20);

    horizontal_graph.Add(horiz_gr);
    vertical_graph.Add(vert_gr);

    lg.AddEntry(vert_gr, std::to_string(pos).c_str(), "lep");
  }

  out_file.cd();

  horizontal_graph.SetTitle("NCV background neutron rates;"
    " Water thickness between NCV and beam side of tank (in); neutrons / POT");

  vertical_graph.SetTitle("NCV background neutron rates;"
    " NCV water overburden (in); neutrons / POT");

  horizontal_graph.Write("horizontal_graph");
  vertical_graph.Write("vertical_graph");
  lg.Write("legend_for_rate_graphs");

  return 0;
}
