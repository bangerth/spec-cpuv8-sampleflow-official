// ---------------------------------------------------------------------
//
// Copyright (C) 2023 by the SampleFlow authors.
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

#ifndef SAMPLEFLOW_RANDOM_H
#define SAMPLEFLOW_RANDOM_H


namespace SampleFlow
{
  namespace random
  {
    /**
     * While the C++ standard defines the random number generators in
     * a way that ensures that they are platform independent (i.e.,
     * produce a predictable sequence of random numbers), it doesn't
     * do that for the *distributions*. As a consequence, one can
     * obtain a predictable sequence of random integers on all
     * compilers and systems, but not a predictable sequence of
     * 'double' values. That seems like an oversight in the standard,
     * but provides a problem for a benchmark like SPEC. Consequently,
     * implement a distribution class that is predictable between
     * compilers -- by virtue of being implemented here, as part of
     * this benchmark.
     */
    template<typename RealType = double>
    class uniform_real_distribution
    {
    public:
      explicit
      uniform_real_distribution(RealType a, RealType b)
      : a(a), b(b)
        { }

      template<typename RNG>
      RealType
      operator()(RNG & rng)
        {
          const RealType s = RealType(1.) * (rng()-rng.min()) / (rng.max()-rng.min());
          return s * (b - a) + a;
        }
      
    private:
      RealType a,b;
    };


    template<typename IntType = int>
    class uniform_int_distribution
    {
    public:
      explicit
      uniform_int_distribution(IntType a, IntType b)
      : a(a), b(b)
        { }

      template<typename RNG>
      IntType
      operator()(RNG & rng)
        {
          // The following does not generate uniformly distributed
          // numbers if (rng.max()-rng.min()) isn't a multiple of
          // (b-a). So this won't make for a good random number
          // generator for integers, but it is perfectly acceptable
          // for a SPEC benchmar :-)
          const IntType s = (rng()-rng.min()) % (b-a);
          return s + a;
        }
      
    private:
      IntType a,b;
    };
  }
}


#endif
