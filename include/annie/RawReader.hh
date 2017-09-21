// Class that reads ANNIE non-post-processed raw data files
// and creates RawReadout objects representing each readout
// from the phase I DAQ.
//
// Steven Gardiner <sjgardiner@ucdavis.edu>
#pragma once

// standard library includes
#include <memory>

// ROOT includes
#include "TBranch.h"
#include "TChain.h"
#include "TFile.h"
#include "TTree.h"

// reco-annie includes
#include "annie/RawReadout.hh"

namespace annie {

  class RawReader {

    public:

      // Because we are using a TChain internally, the file name(s)
      // passed to the constructors may contain wildcards.
      RawReader(const std::string& file_name);
      RawReader(const std::vector<std::string>& file_names);

      std::unique_ptr<RawReadout> next();

    protected:

      void set_branch_addresses();

      TChain pmt_data_chain_;

      long long current_entry_ = 0; // index of the current TChain entry

      // Variables used to read from each branch of the PMTData TChain
      unsigned long long br_LastSync_;
      int br_SequenceID_;
      int br_StartTimeSec_;
      int br_StartTimeNSec_;
      unsigned long long br_StartCount_;
      int br_TriggerNumber_;
      int br_CardID_;
      int br_Channels_;
      int br_BufferSize_;
      int br_FullBufferSize_;
      int br_EventSize_;
      std::vector<unsigned short> br_Data_; // [FullBufferSize]
      std::vector<unsigned long long> br_TriggerCounts_; // [TriggerNumber]
      std::vector<unsigned int> br_Rates_; // [Channels]
  };
}
