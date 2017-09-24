// Object representing a reconstructed DAQ readout
//
// Steven Gardiner <sjgardiner@ucdavis.edu>
#pragma once

// standard library includes
#include <map>
#include <vector>

// reco-annie includes
#include "annie/Constants.hh"
#include "annie/RecoPulse.hh"

namespace annie {

  class RecoReadout {

    public:

      RecoReadout(int SequenceID = BOGUS_INT);

      void add_pulse(int card_number, int channel_number,
        int minibuffer_number, const annie::RecoPulse& pulse);

      void add_pulses(int card_number, int channel_number,
        int minibuffer_number, const std::vector<annie::RecoPulse>& pulses);

      const std::vector<annie::RecoPulse>& get_pulses(int card_number,
        int channel_number, int minibuffer_number) const;

      const std::map<int, std::map<int, std::map<int,
        std::vector<annie::RecoPulse> > > >& pulses() const { return pulses_; }

      double tank_charge(int minibuffer_number) const;

    protected:

      // @brief Integer identifier for this readout that is unique within a run
      int sequence_id_;

      /// @brief Reconstructed pulses on each channel
      /// @details The keys (from outer to inner) are (card index, channel
      /// index, minibuffer index). The values are vectors of reconstructed
      /// pulse objects.
      std::map<int, std::map<int, std::map<int,
        std::vector<annie::RecoPulse> > > > pulses_;
  };

}
