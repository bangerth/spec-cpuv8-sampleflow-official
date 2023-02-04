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

#ifndef SAMPLEFLOW_CONSUMERS_MEAN_VALUE_H
#define SAMPLEFLOW_CONSUMERS_MEAN_VALUE_H

#include <sampleflow/consumer.h>
#include <sampleflow/types.h>
#include <mutex>


namespace SampleFlow
{
  namespace Consumers
  {
    /**
     * A Consumer class that implements computing the running mean value
     * over all samples seen so far. The last value so computed can be
     * obtained by calling the get() function.
     *
     * This class uses the following formula to update the mean $\bar x_k$
     * after seeing $k$ samples $x_1\ldots x_k$ (see
     * https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm):
     * @f{align*}{
     *      \bar x_1 &= x_1,
     *   \\ \bar x_k &= \bar x_{k-1} + \frac{1}{k} (x_k - \bar x_{k-1}).
     * @f}
     *
     * This formula can be derived by considering the following relationships:
     * @f{align*}{
     *      \bar x_k &= \frac{1}{k} \sum_{j=1}^k x_j
     *      \\       &= \frac{1}{k} \left( \sum_{j=1}^{k-1} x_j + x_k \right)
     *      \\       &= \frac{1}{k} \left( (k-1)\bar x_{k-1} + x_k \right)
     *      \\       &= \frac{k-1}{k} \bar x_{k-1} + \frac{1}{k} x_k
     *      \\       &= \bar x_{k-1} - \frac{1}{k} \bar x_{k-1} + \frac{1}{k} x_k
     *      \\       &= \bar x_{k-1} + \frac{1}{k} (x_k - \bar x_{k-1}).
     * @f}
     *
     *
     * ### Threading model ###
     *
     * The implementation of this class is thread-safe, i.e., its
     * consume() member function can be called concurrently and from multiple
     * threads.
     *
     *
     * @tparam InputType The C++ type used for the samples $x_k$. In
     *   order to compute mean values, this type must allow taking
     *   the sum of samples, and division by an integer scalar. In practice
     *   this means that if the `InputType` is an integer (or a vector over
     *   integers, or more generally a module over the integers but not
     *   a vector space or module over the rational numbers when expressed
     *   in mathematical language), then the division by $k$ in the computation
     *   of the update above will be performed in integer arithmetic
     *   and will, consequently, not likely be what you expect. For example,
     *   if the samples correspond to the number of dots on a dice and
     *   are represented by integers, then computing the mean above will
     *   correspond to computing $\frac{1}{k} (x_k - \bar x_{k-1})$ in
     *   integer arithmetic and will yield no change at all once $k\ge 6$ since
     *   $x_k - \bar x_{k-1}$ is always between $-5$ and $5$, and consequently
     *   the integer division by a $k$ greater than 5 will yield a zero update.
     *   This may not be what you want -- the mean value of the number of
     *   dots you get when rolling a perfect dice is 3.5 -- but simply
     *   reflects that this mean value is just not an element of the set
     *   of possible outcomes and, more generally, an element in the set
     *   used to represent the outcomes (the integers). To avoid this,
     *   you will want to convert the sample type to one that can represent
     *   these things, for example `double`. This can be done using the
     *   Filters::Conversion filter class, for example.
     */
    template <typename InputType>
    class MeanValue : public Consumer<InputType>
    {
      public:
        /**
         * The type of the information generated by this class, i.e., in which
         * the mean value is computed. This is of course the InputType.
         */
        using value_type = InputType;

        /**
         * Constructor.
         *
         * This class does not care in which order samples are processed, and
         * consequently calls the base class constructor with
         * `ParallelMode::synchronous | ParallelMode::asynchronous` as argument.
         */
        MeanValue ();

        /**
         * Destructor. This function also makes sure that all samples this
         * object may have received have been fully processed. To this end,
         * it calls the Consumers::disconnect_and_flush() function of the
         * base class.
         */
        virtual ~MeanValue ();

        /**
         * Process one sample by updating the previously computed mean value
         * using this one sample.
         *
         * @param[in] sample The sample to process.
         * @param[in] aux_data Auxiliary data about this sample. The current
         *   class does not know what to do with any such data and consequently
         *   simply ignores it.
         */
        virtual
        void
        consume (InputType     sample,
                 AuxiliaryData aux_data) override;

        /**
         * A function that returns the mean value computed from the samples
         * seen so far. If no samples have been processed so far, then a
         * default-constructed object of type InputType will be returned.
         *
         * @return The computed mean value.
         */
        value_type
        get () const;

      private:
        /**
         * A mutex used to lock access to all member variables when running
         * on multiple threads.
         */
        mutable std::mutex mutex;

        /**
         * The current value of $\bar x_k$ as described in the introduction
         * of this class.
         */
        InputType           current_mean;

        /**
         * The number of samples processed so far.
         */
        types::sample_index n_samples;
    };



    template <typename InputType>
    MeanValue<InputType>::
    MeanValue ()
      :
      Consumer<InputType>(ParallelMode(static_cast<int>(ParallelMode::synchronous)
                                       |
                                       static_cast<int>(ParallelMode::asynchronous))),
      n_samples (0)
    {}



    template <typename InputType>
    MeanValue<InputType>::
    ~MeanValue ()
    {
      this->disconnect_and_flush();
    }



    template <typename InputType>
    void
    MeanValue<InputType>::
    consume (InputType sample, AuxiliaryData /*aux_data*/)
    {
      std::lock_guard<std::mutex> lock(mutex);

      // If this is the first sample we see, initialize the current-mean with
      // this sample.
      if (n_samples == 0)
        {
          n_samples = 1;
          current_mean = std::move(sample);
        }
      else
        {
          // Otherwise update the previously computed mean by the current
          // sample.
          ++n_samples;

          InputType update = std::move(sample);
          update -= current_mean;
          update /= n_samples;

          current_mean += update;
        }
    }



    template <typename InputType>
    typename MeanValue<InputType>::value_type
    MeanValue<InputType>::
    get () const
    {
      std::lock_guard<std::mutex> lock(mutex);

      return current_mean;
    }

  }
}

#endif
