// standard library includes
#include <stdexcept>

// reco-annie includes
#include "annie/RawChannel.hh"

annie::RawChannel::RawChannel(int ChannelNumber,
  const std::vector<unsigned short>::const_iterator data_begin,
  const std::vector<unsigned short>::const_iterator data_end,
  unsigned int Rate, size_t MiniBufferCount) : channel_number_(ChannelNumber),
  rate_(Rate), data_(data_begin, data_end), num_minibuffers_(MiniBufferCount)
{
}

std::vector<unsigned short> annie::RawChannel::minibuffer_data(size_t mb_index)
  const
{
  if (mb_index >= num_minibuffers_) throw std::runtime_error("MiniBuffer index"
    " out-of-range in annie::RawChannel::minibuffer_data()");

  size_t mb_size = data_.size() / num_minibuffers_;

  size_t start_index = mb_size * mb_index;
  size_t end_index = mb_size * (mb_index + 1);

  const auto& begin = data_.cbegin() + start_index;
  const auto& end = data_.cbegin() + end_index;

  std::vector<unsigned short> mb_data(begin, end);
  return mb_data;
}
