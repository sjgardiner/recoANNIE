// standard library includes
#include <algorithm>
#include <array>

// reco-annie includes
#include "annie/RecoReadout.hh"

// Anonymous namespace for definitions local to this source file
namespace {
  // Lists indices for all of the VME cards that watch ANNIE phase I
  // water tank PMTs
  constexpr std::array<int, 15> water_pmt_cards =
    { 3, 4, 5, 6, 8, 9, 10, 11, 13, 14, 15, 16, 18, 19, 20 };

  // Lists { card, channel } pairs that should be excluded from the water
  // tank PMTs when calculating the tank charge
  constexpr std::array< std::pair<int, int>, 4> excluded_card_channel_pairs =
  {{
     {  4, 1 }, // NCV PMT #1
     {  8, 2 }, // neutron calibration source trigger input
     { 14, 0 }, // cosmic trigger input
     { 18, 0 }, // NCV PMT #1
  }};
}

annie::RecoReadout::RecoReadout(int SequenceID)
  : sequence_id_(SequenceID)
{
}

void annie::RecoReadout::add_pulse(int card_number, int channel_number,
  int minibuffer_number, const annie::RecoPulse& pulse)
{
  if ( !pulses_.count(card_number) ) pulses_.emplace(card_number,
    std::map<int, std::map<int, std::vector<annie::RecoPulse> > >());

  auto& card_map = pulses_.at(card_number);
  if ( !card_map.count(channel_number) ) card_map.emplace(channel_number,
    std::map<int, std::vector<annie::RecoPulse> >());

  auto& channel_map = card_map.at(channel_number);
  if ( !channel_map.count(minibuffer_number) ) channel_map.emplace(
    minibuffer_number, std::vector<annie::RecoPulse>());

  auto& minibuffer_vec = channel_map.at(minibuffer_number);
  minibuffer_vec.push_back(pulse);
}

void annie::RecoReadout::add_pulses(int card_number, int channel_number,
  int minibuffer_number, const std::vector<annie::RecoPulse>& pulses)
{
  if ( !pulses_.count(card_number) ) pulses_.emplace(card_number,
    std::map<int, std::map<int, std::vector<annie::RecoPulse> > >());

  auto& card_map = pulses_.at(card_number);
  if ( !card_map.count(channel_number) ) card_map.emplace(channel_number,
    std::map<int, std::vector<annie::RecoPulse> >());

  auto& channel_map = card_map.at(channel_number);

  // If no pulses are already present for this minibuffer, channel, and
  // card combination, copy the whole vector over at once.
  if ( !channel_map.count(minibuffer_number) ) channel_map.emplace(
    minibuffer_number, pulses);

  // If some pulses are already present, copy them over one-by-one.
  else {
    auto& minibuffer_vec = channel_map.at(minibuffer_number);
    for (const auto& pulse : pulses) minibuffer_vec.push_back(pulse);
  }
}

const std::vector<annie::RecoPulse>& annie::RecoReadout::get_pulses(
  int card_number, int channel_number, int minibuffer_number) const
{
  return pulses_.at(card_number).at(channel_number).at(minibuffer_number);
}

double annie::RecoReadout::tank_charge(int minibuffer_number) const {
  double tank_charge = 0.;

  for (const auto& card_pair : pulses_) {
    // Skip cards that do not receive data from any water tank PMTs
    int card_id = card_pair.first;
    if ( std::find(std::begin(water_pmt_cards), std::end(water_pmt_cards),
      card_id) == std::end(water_pmt_cards) ) continue;

    const auto& channel_map = card_pair.second;

    for (const auto& channel_pair :  channel_map) {
      // Skip channels on the list of channels to exclude (e.g., the NCV PMT
      // channels)
      int channel_id = channel_pair.first;
      if ( std::find(std::begin(excluded_card_channel_pairs),
        std::end(excluded_card_channel_pairs),
        std::make_pair(card_id, channel_id))
        != std::end(excluded_card_channel_pairs) ) continue;

      const auto& minibuffer_map = channel_pair.second;

      for ( const auto& pulse : minibuffer_map.at(minibuffer_number) ) {
        tank_charge += pulse.charge();
      }
    }
  }

  return tank_charge;
}
