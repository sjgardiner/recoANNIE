// Class that represents a full readout of raw data from a single channel of
// one of the DAQ VME cards.
//
// Steven Gardiner <sjgardiner@ucdavis.edu>
#pragma once

// standard library includes
#include <vector>

namespace annie {

  class RawChannel {

    public:

      RawChannel(int ChannelNumber,
        const std::vector<unsigned short>::const_iterator data_begin,
        const std::vector<unsigned short>::const_iterator data_end,
        unsigned int Rate, size_t MiniBufferCount);

      unsigned int channel_number() const { return channel_number_; }
      void set_channel_number( unsigned int cn) { channel_number_ = cn; }

      unsigned int rate() const { return rate_; }
      void set_rate( unsigned int r) { rate_ = r; }

      const std::vector<short>& data() const { return data_; }

      size_t num_minibuffers() const { return num_minibuffers_; }

    protected:

      /// @brief The index of this channel in the full waveform buffer
      /// of its VME card
      unsigned channel_number_;

      /// @brief The rate for this channel
      unsigned rate_;

      /// @brief Raw ADC counts from the full readout for this channel
      std::vector<short> data_;

      /// @brief The number of minibuffers recorded in this readout
      size_t num_minibuffers_;
  };
}
