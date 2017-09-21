// standard library includes
#include <iostream>

// reco-annie includes
#include "annie/RawReader.hh"
#include "annie/RawReadout.hh"

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "Usage: reco-annie INPUT_FILE...\n";
    return 1;
  }

  annie::RawReader reader(argv[1]);

  while (auto out = reader.next()) {
    std::cout << out->sequence_id() << '\n';
  }

  return 0;
}
