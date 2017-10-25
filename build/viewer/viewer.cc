// standard library includes
#include <memory>
#include <string>
#include <vector>

// ROOT includes
#include "TApplication.h"

// viewer includes
#include "RawViewer.hh"

int main(int argc, char** argv) {

  // Create a TApplication object. This allows us to use ROOT GUI features from
  // a stand-alone compiled application.
  TApplication app("test_app", &argc, argv);

  std::vector<std::string> input_file_names;

  for (int i = 1; i < app.Argc(); ++i) {
    input_file_names.push_back( app.Argv(i) );
  }

  auto viewer = std::make_unique<annie::RawViewer>(input_file_names);

  app.Run();
  return 0;
}
