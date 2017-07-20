//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_TEST_CSF_RANDOM_H_INCLUDED
#define RIPPLE_TEST_CSF_RANDOM_H_INCLUDED

#include <random>
#include <vector>

namespace ripple {
namespace test {
namespace csf {

/** Return a randomly shuffled copy of vector based on weights w.

    @param v  The set of values
    @param w  The set of weights of each value
    @param g  A pseudo-random number generator
    @return A vector with entries randomly sampled without replacement
            from the original vector based on the provided weights.
            I.e.  res[0] comes from sample v[i] with weight w[i]/sum_k w[k]
*/
template <class T, class G>
std::vector<T>
random_weighted_shuffle(std::vector<T> v, std::vector<double> w, G& g)
{
    using std::swap;

    for (int i = 0; i < v.size() - 1; ++i)
    {
        // pick a random item weighted by w
        std::discrete_distribution<> dd(w.begin() + i, w.end());
        auto idx = dd(g);
        std::swap(v[i], v[idx]);
        std::swap(w[i], w[idx]);
    }
    return v;
}

//------------------------------------------------------------------------------
// Additional distrubtions of interest not defined in in <random>

/** Constant "distribution" that always returns the same value
*/
class ConstantDistribution
{
    double t_;

public:
    ConstantDistribution(double const& t) : t_{t}
    {
    }

    template <class Generator>
    inline double
    operator()(Generator& )
    {
        return t_;
    }
};

/** Power-law distribution with PDF

        P(x) = (x/xmin)^-a

    for a >= 1 and xmin >= 1
 */
class PowerLawDistribution
{
    double xmin_;
    double a_;
    double inv_;
    std::uniform_real_distribution<double> uf_{0, 1};

public:

    using value_type = double;

    PowerLawDistribution(double xmin, double a) : xmin_{xmin}, a_{a}
    {
        inv_ = 1.0 / (1.0 - a_);
    }

    template <class Generator>
    inline double
    operator()(Generator& g)
    {
        // use inverse transform of CDF to sample
        // CDF is P(X <= x): 1 - (x/xmin)^(1-a)
        return xmin_ * std::pow(1 - uf_(g), inv_);
    }
};

}  // csf
}  // test
}  // ripple

#endif
