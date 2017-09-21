// standard library includes
#include <iostream>
#include <string>
#include <vector>

// reco-annie includes
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

  while (auto out = reader.next()) {
    std::cout << out->sequence_id() << '\n';
  }

  return 0;
}
