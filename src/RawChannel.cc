#include "annie/RawChannel.hh"

annie::RawChannel::RawChannel(int ChannelNumber,
  const std::vector<unsigned short>::const_iterator data_begin,
  const std::vector<unsigned short>::const_iterator data_end,
  unsigned int Rate, size_t MiniBufferCount) : channel_number_(ChannelNumber),
  rate_(Rate), data_(data_begin, data_end), num_minibuffers_(MiniBufferCount)
{
}
