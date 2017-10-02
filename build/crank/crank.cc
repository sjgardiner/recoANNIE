// standard library includes
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
#include "TMultiGraph.h"
#include "TTree.h"

// reco-annie includes
#include "annie/Constants.hh"
#include "annie/RecoPulse.hh"
#include "annie/RecoReadout.hh"

constexpr double VETO_TIME = 1e3; // ns

constexpr int BEAM_MINIBUFFER_LABEL = 1;
constexpr int NCV_MINIBUFFER_LABEL = 2;

constexpr int NUM_HEFTY_MINIBUFFERS = 40;

constexpr double FREYA_TIME_OFFSET = 2e3; // ns

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

  for (int i = b_low; i < b_high; ++i) {
    sum_bi += hist.GetBinContent(i);
    error_sum_bi += std::max(hist.GetBinContent(i), 1.);
  }

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
        && db_Label[m] != NCV_MINIBUFFER_LABEL) continue;

      const std::vector<annie::RecoPulse>& ncv1_pulses
        = rr->get_pulses(4, 1, m);

      if (ncv1_pulses.empty()) continue;

      double old_time = std::numeric_limits<double>::lowest(); // ns
      for (const auto& pulse : ncv1_pulses) {
        double event_time = static_cast<double>( pulse.start_time() );

        // Add the offset of the current minibuffer to the pulse start time
        event_time += db_TSinceBeam[m];

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


void make_efficiency_plot(TFile& output_file) {

  std::cout << "Opening position #1 source data\n";
  TChain source_data_chain("reco_readout_tree");
  source_data_chain.Add("/annie/data/users/gardiner/reco-annie/"
    "source_data_pos1.root");

  std::cout << "Analyzing position #1 source data\n";
  TH1D source_data_hist = make_nonhefty_timing_hist(source_data_chain,
    1. / source_data_chain.GetEntries(), "pos1_source_data_hist",
    "Position #1 source data event times");

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
    freya_hist.Fill(freya_capture_time + FREYA_TIME_OFFSET);
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

  std::cout << "Estimate of NCV efficiency = " << eff_fit_func.GetParameter(0)
    << '\n';

  std::unique_ptr<TH1D> eff_hist( dynamic_cast<TH1D*>(
    freya_hist.Clone("eff_hist") ) );
  eff_hist->Scale( eff_fit_func.GetParameter(0) );
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

  source_data_hist.Write();
  eff_hist->Write();
}

// Returns the estimated neutron event rate (in neutrons / POT)
ValueAndError make_timing_distribution(
  const std::initializer_list<int>& runs, int ncv_position, TFile& output_file,
  bool hefty_mode, double norm_factor)
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

  rate_with_error.value *= norm_factor;
  rate_with_error.error *= norm_factor;

  temp_hist->Scale(norm_factor);
  temp_hist->GetXaxis()->SetTitle("time (ns)");
  temp_hist->GetYaxis()->SetTitle("counts / POT");

  output_file.cd();
  temp_hist->Write();

  return rate_with_error;
}

int main(int argc, char* argv[]) {

  if (argc < 2) {
    std::cout << "Usage: crank OUTPUT_FILE\n";
    return 1;
  }

  TFile out_file(argv[1], "recreate");

  make_efficiency_plot(out_file);

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
  };

  // Make the rate plots
  std::map<int, ValueAndError> positions_and_rates = {
    { 1, make_timing_distribution( { 650 }, 1, out_file, false, 1. / 1.33e18) },
    { 2, make_timing_distribution( { 798 }, 2, out_file, true, 1. / 1.42e19)  },
    { 3, make_timing_distribution( { 803 }, 3, out_file, true, 1. / 1.33e19)  },
    { 4, make_timing_distribution( { 808, 812 }, 4, out_file,
      true, 1. / 2.43e19) },
    { 5, make_timing_distribution( { 813 }, 5, out_file, true, 1. / 1.34e19)  },
    { 6, make_timing_distribution( { 814 }, 6, out_file, true, 1. / 6.20e18)  },
    { 7, make_timing_distribution( { 815 }, 7, out_file, true, 1. / 4.05e18)  },
  };

  TMultiGraph horizontal_graph;
  TMultiGraph vertical_graph;

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
  }

  out_file.cd();

  horizontal_graph.SetTitle("NCV background neutron rates;"
    " NCV horizontal distance from tank center (cm); counts / POT");

  vertical_graph.SetTitle("NCV background neutron rates;"
    " NCV center height (cm); counts / POT");

  horizontal_graph.Write("horizontal_graph");
  vertical_graph.Write("vertical_graph");

  return 0;
}