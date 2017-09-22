// standard library includes
#include <iostream>
#include <string>
#include <vector>

// reco-annie includes
#include "annie/RawAnalyzer.hh"
#include "annie/RawReader.hh"
#include "annie/RawReadout.hh"

int main(int argc, char* argv[]) {

  if (argc < 2) {
    std::cout << "Usage: reco-annie INPUT_FILE...\n";
    return 1;
  }

  std::vector<std::string> file_names;
  for (int i = 1; i < argc; ++i) file_names.push_back( argv[i] );

  annie::RawReader reader(file_names);

  const auto& analyzer = annie::RawAnalyzer::Instance();

  while (auto readout = reader.next()) {
    std::cout << "Sequence ID = " << readout->sequence_id() << '\n';

    for (const auto& card_pair : readout->cards()) {
      const auto& card = card_pair.second;
      for (const auto& channel_pair : card.channels()) {
        const auto& channel = channel_pair.second;

        std::cout << "cardID = " << card.card_id() << ", channel = "
          << channel.channel_number() << ", baseline = "
          << analyzer.ze3ra_baseline(channel) << '\n';
      }
    }
  }

  return 0;
}
