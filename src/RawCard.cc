// reco-annie includes
#include "annie/RawCard.hh"

annie::RawCard::RawCard(int CardID, unsigned long long LastSync,
  int StartTimeSec, int StartTimeNSec, unsigned long long StartCount,
  int Channels, int BufferSize, int MiniBufferSize,
  const std::vector<unsigned short>& Data,
  const std::vector<unsigned long long>& TriggerCounts,
  const std::vector<unsigned int>& Rates) : card_id_(CardID),
  last_sync_(LastSync), start_time_sec_(StartTimeSec),
  start_time_nsec_(StartTimeNSec), start_count_(StartCount),
  trigger_counts_(TriggerCounts)
{
  if (Channels != static_cast<int>(Data.size()) / BufferSize) throw
    std::runtime_error("Mismatch between number of channels and"
    " channel buffer size in annie::RawCard::RawCard()");

  if ( TriggerCounts.size() != static_cast<size_t>(BufferSize
    / MiniBufferSize) )
  {
    throw std::runtime_error("Mismatch between number of minibuffers and"
    " minibuffer size in annie::RawCard::RawCard()");
  }

  for (int c = 0; c < Channels; ++c) add_channel(c, Data, BufferSize,
    Rates.at(c));
}


void annie::RawCard::add_channel(int channel_number,
  const std::vector<unsigned short>& full_buffer_data,
  int channel_buffer_size, unsigned int rate, bool overwrite_ok)
{
  auto iter = channels_.find(channel_number);
  if ( iter != channels_.end() ) {
    if (!overwrite_ok) throw std::runtime_error("RawChannel overwrite"
      " attempted in annie::RawCard::add_channel()");
    else channels_.erase(iter);
  }

  size_t start_index = channel_number*channel_buffer_size;
  size_t end_index = (channel_number + 1)*channel_buffer_size;

  if ( full_buffer_data.size() < end_index ) {
    throw std::runtime_error("Missing data for channel "
      + std::to_string(channel_number) + " encountered in annie::RawCard"
      "::add_channel()");
  }

  // The channel data are stored out of order (half at the beginning and
  // half midway through) so get iterators to both starting locations
  auto channel_begin = full_buffer_data.cbegin() + start_index;
  auto channel_halfway = channel_begin + channel_buffer_size / 2;
  channels_.emplace( std::make_pair(channel_number,
    annie::RawChannel(channel_number, channel_begin, channel_halfway,
    rate, trigger_counts_.size())) );
}
