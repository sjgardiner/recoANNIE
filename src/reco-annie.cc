// standard library includes
#include <iostream>
#include <string>
#include <vector>

// ROOT includes
#include "TFile.h"
#include "TTree.h"

// reco-annie includes
#include "annie/Constants.hh"
#include "annie/RawAnalyzer.hh"
#include "annie/RawReader.hh"
#include "annie/RawReadout.hh"
#include "annie/RecoPulse.hh"
#include "annie/RecoReadout.hh"

constexpr size_t TANK_CHARGE_TIME_WINDOW = 40; // ns

int main(int argc, char* argv[]) {

  if (argc < 3) {
    std::cout << "Usage: reco-annie OUTPUT_FILE INPUT_FILE...\n";
    return 1;
  }

  TFile out_file(argv[1], "recreate");
  TTree* out_tree = new TTree("pulse_tree", "recoANNIE pulse tree");

  const annie::RecoPulse* pulse_ptr = nullptr;
  int card_id = 0;
  int channel_id = 0;
  int sequence_id = 0;
  out_tree->Branch("pulse", "annie::RecoPulse", &pulse_ptr);
  out_tree->Branch("card_id", &card_id, "card_id/I");
  out_tree->Branch("channel_id", &channel_id, "channel_id/I");
  out_tree->Branch("sequence_id", &sequence_id, "sequence_id/I");

  TTree* reco_readout_tree = new TTree("reco_readout_tree",
    "recoANNIE RecoReadout tree");
  const annie::RecoReadout* reco_readout_ptr = nullptr;
  reco_readout_tree->Branch("reco_readout", "annie::RecoReadout",
    &reco_readout_ptr);

  TTree* tank_charge_tree = new TTree("tank_charge_tree", "recoANNIE tank"
    " charge tree");
  double tank_charge = 0.;
  int num_unique_pmts = 0;
  tank_charge_tree->Branch("tank_charge", &tank_charge, "tank_charge/D");
  tank_charge_tree->Branch("num_unique_pmts", &num_unique_pmts,
    "num_unique_pmts/I");

  std::vector<std::string> file_names;
  for (int i = 2; i < argc; ++i) file_names.push_back( argv[i] );

  annie::RawReader reader(file_names);

  const auto& analyzer = annie::RawAnalyzer::Instance();

  while (auto readout = reader.next()) {

    sequence_id = readout->sequence_id();
    std::cout << "Sequence ID = " << sequence_id << '\n';

    auto reco_readout = analyzer.find_pulses(*readout);

    reco_readout_ptr = reco_readout.get();
    reco_readout_tree->Fill();

    // NCV PMT #1
    card_id = 4;
    channel_id = 1;
    for (const auto& pair : reco_readout->pulses().at(card_id).at(channel_id))
    {
      int minibuffer_id = pair.first;
      const auto& ncv1_pulses = pair.second;
      std::cout << "Found " << ncv1_pulses.size() << " pulses on NCV PMT #1"
        << " in minibuffer " << minibuffer_id << '\n';

      for (const auto& pulse : ncv1_pulses) {
        tank_charge = reco_readout->tank_charge(minibuffer_id,
          pulse.start_time(), pulse.start_time() + TANK_CHARGE_TIME_WINDOW,
          num_unique_pmts);
        std::cout << "  start time = " << pulse.start_time() << ", amp = "
          << pulse.amplitude() << ", charge = " << pulse.charge()
          << ", tank charge = " << tank_charge << " nC\n";
        tank_charge_tree->Fill();

        pulse_ptr = &pulse;
        out_tree->Fill();
      }

    }

    // NCV PMT #2
    card_id = 18;
    channel_id = 0;
    for (const auto& pair : reco_readout->pulses().at(card_id).at(channel_id))
    {
      int minibuffer_id = pair.first;
      const auto& ncv2_pulses = pair.second;
      std::cout << "Found " << ncv2_pulses.size() << " pulses on NCV PMT #2"
        << " in minibuffer " << minibuffer_id << '\n';

      for (const auto& pulse : ncv2_pulses) {
        tank_charge = reco_readout->tank_charge(minibuffer_id,
          pulse.start_time(), pulse.start_time() + TANK_CHARGE_TIME_WINDOW,
          num_unique_pmts);
        std::cout << "  start time = " << pulse.start_time() << ", amp = "
          << pulse.amplitude() << ", charge = " << pulse.charge()
          << ", tank charge = " << tank_charge << " nC\n";
        tank_charge_tree->Fill();

        pulse_ptr = &pulse;
        out_tree->Fill();
      }

    }

  }

  out_tree->Write();
  reco_readout_tree->Write();
  tank_charge_tree->Write();

  out_file.Close();

  return 0;
}
