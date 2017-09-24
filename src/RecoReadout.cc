// reco-annie includes
#include "annie/RecoReadout.hh"

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
