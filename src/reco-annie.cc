// standard library includes
#include <iostream>
#include <string>
#include <vector>

// ROOT includes
#include "TFile.h"
#include "TTree.h"

// reco-annie includes
#include "annie/RawAnalyzer.hh"
#include "annie/RawReader.hh"
#include "annie/RawReadout.hh"

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
  out_tree->Branch("pulse", "annie::RecoPulse", &pulse_ptr);
  out_tree->Branch("card_id", &card_id, "card_id/I");
  out_tree->Branch("channel_id", &channel_id, "channel_id/I");

  TTree* readout_tree = new TTree("readout_tree", "recoANNIE RawReadout tree");
  const annie::RawReadout* readout_ptr = nullptr;
  readout_tree->Branch("readout", "annie::RawReadout", &readout_ptr);


  std::vector<std::string> file_names;
  for (int i = 2; i < argc; ++i) file_names.push_back( argv[i] );

  annie::RawReader reader(file_names);

  const auto& analyzer = annie::RawAnalyzer::Instance();

  while (auto readout = reader.next()) {
    std::cout << "Sequence ID = " << readout->sequence_id() << '\n';

    readout_ptr = readout.get();
    readout_tree->Fill();

    for (const auto& card_pair : readout->cards()) {
      const auto& card = card_pair.second;
      for (const auto& channel_pair : card.channels()) {
        const auto& channel = channel_pair.second;

        card_id = card_pair.first;
        channel_id = channel_pair.first;

        if ( !(card_id == 18 && channel_id == 0)
          && !(card_id == 4 && channel_id == 1) ) continue;
        std::vector<annie::RecoPulse> pulses
          = analyzer.find_pulses(channel, 357);
        std::cout << "Found " << pulses.size() << " pulses\n";
        for (const auto& pulse : pulses) {
          std::cout << "  amp = " << pulse.amplitude() * 1e3 << " mV,";
          std::cout << " charge = " << pulse.charge() << " nC,";
          std::cout << " start time = " << pulse.start_time() << " ns\n";

          pulse_ptr = &pulse;
          out_tree->Fill();
        }
      }
    }
  }

  out_tree->Write();
  readout_tree->Write();

  out_file.Close();

  return 0;
}
