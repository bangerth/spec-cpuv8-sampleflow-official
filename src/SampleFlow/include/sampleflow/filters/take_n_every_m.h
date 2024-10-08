// ---------------------------------------------------------------------
//
// Copyright (C) 2019 by the SampleFlow authors.
//
// This file is part of the SampleFlow library.
//
// The SampleFlow library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE.md at
// the top level directory of SampleFlow.
//
// ---------------------------------------------------------------------

#ifndef SAMPLEFLOW_FILTERS_TAKE_N_EVERY_M_H
#define SAMPLEFLOW_FILTERS_TAKE_N_EVERY_M_H

#include <sampleflow/filter.h>
#include <sampleflow/types.h>

#include <mutex>

namespace SampleFlow
{
  namespace Filters
  {
    /**
     * An implementation of the Filter interface in which starting
     * with every $m$th sample, we passed on the next $n$ samples, and
     * all other samples are simply discarded. This filter is useful
     * to reduce the amount of data produced by a sampling
     * algorithm. This is often warranted in Markov Chain sampling
     * algorithms in which samples are highly correlated and
     * consequently not every sample individually carries a lot of
     * information. Only samples separated by at least one
     * "correlation length" carry independent information, and
     * consequently skipping most samples does not reduce the amount
     * of information available in a chain.
     *
     *
     * ### Threading model ###
     *
     * The implementation of this class is thread-safe, i.e., its
     * filter() member function can be called concurrently and from multiple
     * threads.
     *
     *
     * @tparam InputType The C++ type used to describe the incoming samples.
     *   For the current class, this is of course also the type used for
     *   the outgoing samples. Consequently, this class is a model of the
     *   Filter base class where both input and output type use this
     *   template type as template arguments.
     */
    template <typename InputType>
    class TakeNEveryM : public Filter<InputType, InputType>
    {
      public:
        /**
         * Constructor.
         */
        TakeNEveryM (const types::sample_index every_mth,
                     const types::sample_index n_samples);

        /**
         * Destructor. This function also makes sure that all samples this
         * object may have received have been fully processed. To this end,
         * it calls the Consumers::disconnect_and_flush() function of the
         * base class.
         */
        virtual ~TakeNEveryM ();

        /**
         * Process one sample by checking whether it is an $n$th sample
         * and if so, pass it on to downstream consumers. If it isn't,
         * return an empty object which the caller of this function in the
         * base class will interpret as the instruction to discard the
         * sample from further processing.
         *
         * @param[in] sample The sample to process.
         * @param[in] aux_data Auxiliary data about this sample. The current
         *   class does not know what to do with any such data and consequently
         *   simply passed it on.
         *
         * @return The sample and its auxiliary data if this is the $k$th
         *   sample and $k \mod n = 0$. Otherwise, an empty object.
         */
        virtual
        boost::optional<std::pair<InputType, AuxiliaryData> >
        filter (InputType sample,
                AuxiliaryData aux_data) override;

      private:
        /**
         * A mutex used to lock access to all member variables when running
         * on multiple threads.
         */
        std::mutex mutex;

        /**
         * A counter counting how many samples we have seen so far.
         */
        types::sample_index counter;

        /**
         * The variable storing how often we are to forward a received
         * sample to downstream consumers.
         */
        const types::sample_index every_mth;
        const types::sample_index n_samples;
    };



    template <typename InputType>
    TakeNEveryM<InputType>::
    TakeNEveryM (const types::sample_index every_mth,
                 const types::sample_index n)
      : counter (0),
        every_mth (every_mth),
        n_samples (n)
    {}



    template <typename InputType>
    TakeNEveryM<InputType>::
    ~TakeNEveryM ()
    {
      this->disconnect_and_flush();
    }



    template <typename InputType>
    boost::optional<std::pair<InputType, AuxiliaryData> >
    TakeNEveryM<InputType>::
    filter (InputType sample,
            AuxiliaryData aux_data)
    {
      types::sample_index my_counter;
      {
        std::lock_guard<std::mutex> lock(mutex);
        my_counter = counter;
        ++counter;
      }
      
      if (my_counter % every_mth < n_samples)
        {
          return
          {{ std::move(sample), std::move(aux_data)}};
        }
      else
        return
          {};
    }

  }
}

#endif
