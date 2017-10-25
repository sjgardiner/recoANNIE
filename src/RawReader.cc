// standard library includes
#include <stdexcept>

// reco-annie includes
#include "annie/Constants.hh"
#include "annie/RawReader.hh"

// anonymous namespace for definitions local to this source file
namespace {
  // Converts the value from the Eventsize branch to the minibuffer size (in
  // samples)
  constexpr int EVENT_SIZE_TO_MINIBUFFER_SIZE = 4;
}

annie::RawReader::RawReader(const std::string& file_name)
  : pmt_data_chain_("PMTData"), current_entry_(0)
{
  pmt_data_chain_.Add( file_name.c_str() );

  set_branch_addresses();
}

annie::RawReader::RawReader(const std::vector<std::string>& file_names)
  : pmt_data_chain_("PMTData"), current_entry_(0)
{
  for (const auto& file_name : file_names)
    pmt_data_chain_.Add( file_name.c_str() );

  set_branch_addresses();
}

void annie::RawReader::set_branch_addresses() {
  pmt_data_chain_.SetBranchAddress("LastSync", &br_LastSync_);
  pmt_data_chain_.SetBranchAddress("SequenceID", &br_SequenceID_);
  pmt_data_chain_.SetBranchAddress("StartTimeSec", &br_StartTimeSec_);
  pmt_data_chain_.SetBranchAddress("StartTimeNSec", &br_StartTimeNSec_);
  pmt_data_chain_.SetBranchAddress("StartCount", &br_StartCount_);
  pmt_data_chain_.SetBranchAddress("TriggerNumber", &br_TriggerNumber_);
  pmt_data_chain_.SetBranchAddress("CardID", &br_CardID_);
  pmt_data_chain_.SetBranchAddress("Channels", &br_Channels_);
  pmt_data_chain_.SetBranchAddress("BufferSize", &br_BufferSize_);
  pmt_data_chain_.SetBranchAddress("FullBufferSize", &br_FullBufferSize_);
  pmt_data_chain_.SetBranchAddress("Eventsize", &br_EventSize_);
  std::vector<unsigned short> br_Data_; // [FullBufferSize]
  std::vector<unsigned long long> br_TriggerCounts_; // [TriggerNumber]
  std::vector<unsigned int> br_Rates_; // [Channels]
}

std::unique_ptr<annie::RawReadout> annie::RawReader::next() {
  return load_next_entry(false);
}

std::unique_ptr<annie::RawReadout> annie::RawReader::previous() {
  return load_next_entry(true);
}

std::unique_ptr<annie::RawReadout> annie::RawReader::load_next_entry(
  bool reverse)
{
  int step = 1;
  if (reverse) {
    step = -1;
    if (current_entry_ <= 0) return nullptr;
    else --current_entry_;
  }

  auto raw_readout = std::make_unique<annie::RawReadout>();

  int first_sequence_id = BOGUS_INT;
  bool loaded_first_card = false;

  // Loop indefinitely until the SequenceID changes (we've finished
  // loading a full DAQ readout) or we run out of TChain entries.
  while (true) {
    // TChain::LoadTree returns the entry number that should be used with
    // the current TTree object, which (together with the TBranch objects
    // that it owns) doesn't know about the other TTrees in the TChain.
    // If the return value is negative, there was an I/O error, or we've
    // attempted to read past the end of the TChain.
    int local_entry = pmt_data_chain_.LoadTree(current_entry_);
    if (local_entry < 0) {
      // If we've reached the end of the TChain (or encountered an I/O error)
      // without loading data from any of the VME cards, return a nullptr.
      if (!loaded_first_card) return nullptr;
      // If we've loaded at least one card, exit the loop, which will allow
      // this function to return the completed RawReadout object (which was
      // possibly truncated by an unexpected end-of-file)
      else break;
    }

    // Load all of the branches except for the variable-length arrays, which
    // we handle separately below using the sizes obtained from this call
    // to TChain::GetEntry().
    pmt_data_chain_.GetEntry(current_entry_);

    // Continue iterating over the tree until we find a readout other
    // than the one that was last loaded
    if (br_SequenceID_ == last_sequence_id_) {
      current_entry_ += step;
      continue;
    }

    // Check that the variable-length array sizes are nonnegative. If one
    // of them is negative, complain.
    if (br_FullBufferSize_ < 0) throw std::runtime_error("Negative"
      " FullBufferSize value encountered in annie::RawReader::next()");
    else if (br_TriggerNumber_ < 0) throw std::runtime_error("Negative"
      " TriggerNumber value encountered in annie::RawReader::next()");
    else if (br_Channels_ < 0) throw std::runtime_error("Negative"
      " Channels value encountered in annie::RawReader::next()");

    // Check the variable-length array sizes and adjust the vector dimensions
    // as needed before loading the corresponding branches.
    size_t fbs_temp = static_cast<size_t>(br_FullBufferSize_);
    if ( br_Data_.size() != fbs_temp) br_Data_.resize(fbs_temp);

    size_t tn_temp = static_cast<size_t>(br_TriggerNumber_);
    if ( br_TriggerCounts_.size() != tn_temp) br_TriggerCounts_.resize(tn_temp);

    size_t cs_temp = static_cast<size_t>(br_Channels_);
    if ( br_Rates_.size() != cs_temp) br_Rates_.resize(cs_temp);

    // Load the variable-length arrays from the current entry. The C++ standard
    // guarantees that std::vector elements are stored contiguously in memory
    // (something that is not true of std::deque elements), so we can use
    // a pointer to the first element of each vector as each branch address.
    TTree* temp_tree = pmt_data_chain_.GetTree();
    temp_tree->SetBranchAddress("Data", br_Data_.data());
    temp_tree->SetBranchAddress("TriggerCounts", br_TriggerCounts_.data());
    temp_tree->SetBranchAddress("Rates", br_Rates_.data());

    temp_tree->GetEntry(local_entry);

    // If this is the first card to be loaded, store its SequenceID for
    // reference.
    if (!loaded_first_card) {
      first_sequence_id = br_SequenceID_;
      loaded_first_card = true;
      raw_readout->set_sequence_id(first_sequence_id);
    }
    // When we encounter a new SequenceID value, we've finished loading a full
    // readout and can exit the loop.
    else if (first_sequence_id != br_SequenceID_) break;

    // Add the current card to the incomplete RawReadout object
    raw_readout->add_card(br_CardID_, br_LastSync_, br_StartTimeSec_,
      br_StartTimeNSec_, br_StartCount_, br_Channels_,
      br_BufferSize_, br_EventSize_ * EVENT_SIZE_TO_MINIBUFFER_SIZE,
      br_Data_, br_TriggerCounts_, br_Rates_);

    // Move on to the next TChain entry
    current_entry_ += step;
  }

  // Remember the SequenceID of the last raw readout to be successfully loaded
  last_sequence_id_ = raw_readout->sequence_id();
  return raw_readout;
}
