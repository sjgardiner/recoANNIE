// Singleton class that contains reconstruction algorithms to apply
// to RawReadout objects.
//
// Steven Gardiner <sjgardiner@ucdavis.edu>
#pragma once

namespace annie {

  // Forward-declare the RawChannel class
  class RawChannel;

  /// @brief Singleton analyzer class for reconstructing ANNIE events
  /// from the raw data
  class RawAnalyzer {

    public:

      /// @brief Deleted copy constructor
      RawAnalyzer(const RawAnalyzer&) = delete;

      /// @brief Deleted move constructor
      RawAnalyzer(RawAnalyzer&&) = delete;

      /// @brief Deleted copy assignment operator
      RawAnalyzer& operator=(const RawAnalyzer&) = delete;

      /// @brief Deleted move assignment operator
      RawAnalyzer& operator=(RawAnalyzer&&) = delete;

      /// @brief Get a const reference to the singleton instance of the
      /// RawAnalyzer
      static const RawAnalyzer& Instance();

      /// @brief Compute the baseline for a particular RawChannel
      /// object using a technique taken from the ZE3RA code.
      /// @details See section 2.2 of https://arxiv.org/pdf/1106.0808.pdf for a
      /// full description of the algorithm.
      double ze3ra_baseline(const annie::RawChannel& channel) const;

    protected:

      /// @brief Create the singleton RawAnalyzer object
      RawAnalyzer();

    //private:

  };

}
