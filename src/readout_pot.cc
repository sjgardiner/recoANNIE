// standard library includes
#include <csignal>
#include <ctime>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// ROOT includes
#include "TBranch.h"
#include "TFile.h"
#include "TTree.h"

// recoANNIE includes
#include "annie/BeamStatus.hh"
#include "annie/IFBeamDataPoint.hh"
#include "annie/RawReader.hh"

const unsigned long long FIVE_SECONDS = 5000ull; // ms

// Used to convert from seconds and nanoseconds to milliseconds below
const unsigned long long THOUSAND = 1000ull;
const unsigned long long MILLION = 1000000ull;

// Card to use when computing trigger times for each minibuffer
const size_t TRIGGER_TIME_CARD = 4;

namespace {
  volatile std::sig_atomic_t interrupted = false;

  void signal_handler(int) { interrupted = true; }
}

std::string make_time_string(unsigned long long ms_since_epoch) {
  time_t s_since_epoch = ms_since_epoch / THOUSAND;
  std::string time_string = std::asctime( std::gmtime(&s_since_epoch) );
  time_string.pop_back(); // remove the trailing \n from the time string
  return time_string;
}

void readout_pot(annie::RawReader& reader,
  const std::string& beam_data_filename, const std::string& output_filename)
{
  TFile beam_file(beam_data_filename.c_str(), "read");
  TTree* beam_tree;

  beam_file.GetObject("BeamData", beam_tree);
  if (!beam_tree) throw std::runtime_error("Failed to load the beam data"
    " TTree from the file \"" + beam_data_filename +'\"');

  std::map<std::string, std::map<unsigned long long, IFBeamDataPoint> >*
    beam_data = nullptr;
  TBranch* beam_branch = beam_tree->GetBranch("beam_data");
  beam_branch->SetAddress(&beam_data);

  int beam_branch_entries = beam_branch->GetEntries();

  // Load an index for the beam branch to avoid lengthy searches.
  // Keys are entry numbers, values are start and end times for each
  // entry (in ms since the Unix epoch).
  std::cout << "Loading beam database index\n";

  std::map<int, std::pair<unsigned long long, unsigned long long> >*
    beam_index = nullptr;
  beam_file.GetObject("BeamDataIndex", beam_index);
  if (!beam_index) throw std::runtime_error("Failed to load the beam data"
    " index from the file \"" + beam_data_filename +'\"');

  TFile out_file(output_filename.c_str(), "recreate");
  TTree* out_tree = new TTree("pot_tree", "Protons on target data");

  annie::BeamStatus beam_status;
  annie::BeamStatus* bs_ptr = &beam_status;
  out_tree->Branch("beam_status", "annie::BeamStatus", &bs_ptr);

  int chain_entry = 0;
  out_tree->Branch("chain_entry", &chain_entry, "chain_entry/I");

  int trigger_num = 0;
  out_tree->Branch("trigger_num", &trigger_num, "trigger_num/I");

  int trigger_time_sec = 0;
  out_tree->Branch("trigger_time_sec", &trigger_time_sec, "trigger_time_sec/I");

  int beam_entry = -1;

  bool need_new_beam_data = true;

  // Use our signal handler function to handle SIGINT signals (e.g., the
  // user pressing ctrl+C)
  std::signal(SIGINT, signal_handler);

  int readout_entry = -1;

  while ( auto raw_readout = reader.next() ) {
    if (interrupted) break;
    ++readout_entry;

    std::cout << "Retrieved raw readout entry "
      << readout_entry << '\n';

    size_t num_minibuffers
      = raw_readout->card(TRIGGER_TIME_CARD).num_minibuffers();

    // Loop over each of the minibuffers for the current readout
    for (size_t mb = 0; mb < num_minibuffers; ++mb) {

      // Use the first card's trigger time to get the milliseconds since the
      // Unix epoch for the trigger corresponding to the current event
      // TODO: consider using an average over the cards or something else more
      // sophisticated
      // TODO: consider rounding to the nearest ms instead of truncating

      unsigned long long ms_since_epoch
        = raw_readout->card(TRIGGER_TIME_CARD).trigger_time(mb) / MILLION;

      std::cout << "Finding beam status information for "
        << make_time_string(ms_since_epoch) << '\n';

      // If we don't have any times stored in the beam data that would come
      // close to matching this minibuffer, then ask the beam database for a
      // new data set.
      bool looped_once = false;
      bool found_ok = true;
      int old_beam_entry = beam_entry;
      do {
        if (need_new_beam_data) {

          // The files in the chain will not necessarily be in time order, so
          // loop back to the beginning of the beam index if we reach the end
          // without finding the correct time
          ++beam_entry;
          if (beam_entry >= beam_branch_entries) {

            if (looped_once) {
              std::cerr << "ERROR: looped twice without finding a suitable beam"
                << " map entry for " << ms_since_epoch << " ms since the Unix"
                << " epoch (" << make_time_string(ms_since_epoch) << ")\n";
              found_ok = false;
              break;
            }

            looped_once = true;
            beam_entry = 0;
          }
        }
        const auto& ms_range_pair = beam_index->at(beam_entry);
        unsigned long long start_ms = ms_range_pair.first;
        unsigned long long end_ms = ms_range_pair.second;
        need_new_beam_data = (ms_since_epoch < start_ms)
          || (ms_since_epoch + FIVE_SECONDS > end_ms);
      } while (need_new_beam_data);

      // Find the POT value for the current minibuffer
      try {

        if (!found_ok) throw std::runtime_error("Unable to find"
          " a suitable entry in the beam database");

        if (old_beam_entry != beam_entry) {
          beam_branch->GetEntry(beam_entry);
          std::cout << "Loaded beam database entry " << beam_entry << '\n';
        }

        // TODO: remove hard-coded device name here
        // Get protons-on-target (POT) information from the parsed data
        const std::map<unsigned long long, IFBeamDataPoint>& pot_map
          = beam_data->at("E:TOR875");

        // Find the POT entry with the closest time to that requested by the
        // user, and use it to create the annie::BeamStatus object that will be
        // returned
        std::map<unsigned long long, IFBeamDataPoint>::const_iterator
          low = pot_map.lower_bound(ms_since_epoch);

        if (low == pot_map.cend()) {

          std::cerr << "WARNING: IF beam database did not have any information"
            << " for " << ms_since_epoch << " ms after the Unix epoch ("
            << make_time_string(ms_since_epoch) << ")\n";

          beam_status = annie::BeamStatus();
        }

        else if (low == pot_map.cbegin()) {
          beam_status = annie::BeamStatus(low->first, low->second.value);
        }

        // We're between two time values, so we need to figure out which is
        // closest to the value requested by the user
        else {

          std::map<unsigned long long, IFBeamDataPoint>::const_iterator
            prev = low;
          --prev;

          if ((ms_since_epoch - prev->first) < (low->first - ms_since_epoch)) {
            beam_status = annie::BeamStatus(prev->first, prev->second.value);
          }
          else {
            beam_status = annie::BeamStatus(low->first, low->second.value);
          }
        }

      }

      catch (const std::exception& e) {
        std::cerr << "WARNING: problem encountered while querying IF beam"
          " database:\n  " << e.what() << '\n';

        // Use a default-constructed annie::BeamStatus object since there was a
        // problem. The ok_ member will be set to false by default, which will
        // flag the object as problematic.
        beam_status = annie::BeamStatus();
      }

      out_tree->Fill();
    }
  }

  out_file.cd();
  out_tree->Write();

  beam_file.Close();
  out_file.Close();
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cout << "Usage: readout_pot BEAM_DATA_FILE OUTPUT_FILE RAW_FILE...\n";
    return 1;
  }

  std::string beam_data_filename(argv[1]);

  std::string output_filename(argv[2]);

  std::vector<std::string> input_filenames;
  for (int i = 3; i < argc; ++i) {
    input_filenames.push_back(argv[i]);
  }

  annie::RawReader reader(input_filenames);
  readout_pot(reader, beam_data_filename, output_filename);

  return 0;
}
