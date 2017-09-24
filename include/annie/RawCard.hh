// Class that represents a full readout of raw data from all channels
// that are monitored by a single DAQ VME card.
//
// Steven Gardiner <sjgardiner@ucdavis.edu>
#pragma once

// standard library includes
#include <map>

// reco-annie includes
#include "annie/RawChannel.hh"

namespace annie {

  class RawCard {

    public:

      RawCard() {}

      RawCard(int CardID, unsigned long long LastSync, int StartTimeSec,
        int StartTimeNSec, unsigned long long StartCount, int Channels,
        int BufferSize, int MiniBufferSize,
        const std::vector<unsigned short>& Data,
        const std::vector<unsigned long long>& TriggerCounts,
        const std::vector<unsigned int>& Rates);

      inline unsigned int card_id() const { return card_id_; }

      inline const std::map<int, annie::RawChannel>& channels() const
        { return channels_; }

      inline const annie::RawChannel& channel(int index) const
        { return channels_.at(index); }

    protected:

      void add_channel(int channel_number,
        const std::vector<unsigned short>& full_buffer_data,
        int channel_buffer_size, unsigned int rate, bool overwrite_ok = false);

      /// @brief The index of this VME card
      unsigned card_id_;

      unsigned long long last_sync_;
      int start_time_sec_;
      int start_time_nsec_;
      unsigned long long start_count_;
      std::vector<unsigned long long> trigger_counts_;

      /// @brief Raw data for each of the channels read out by this card
      /// @details Keys are channel IDs, values are RawChannel objects
      /// that store the associated data from the PMTData tree.
      std::map<int, annie::RawChannel> channels_;
  };
}
