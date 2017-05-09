//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_RANGESET_H_INCLUDED
#define RIPPLE_BASICS_RANGESET_H_INCLUDED

#include <boost/icl/closed_interval.hpp>
#include <boost/icl/interval_set.hpp>
#include <cstdint>

namespace ripple
{
/** A sparse set of integers. */
class RangeSet
{
public:
    static const std::uint32_t absent = static_cast<std::uint32_t>(-1);

    RangeSet() = default;

    bool hasValue(std::uint32_t) const;

    // First number in the set
    std::uint32_t getFirst() const;

    // Largest number not in the set that is less than the given number
    std::uint32_t prevMissing(std::uint32_t) const;

    // Add an item to the set
    void setValue(std::uint32_t);

    // Add the closed interval to the set
    void setRange(std::uint32_t, std::uint32_t);

    // Remove the item from the set
    void clearValue(std::uint32_t);

    std::string toString() const;

private:
    using Interval = boost::icl::closed_interval<std::uint32_t>;
    using Map = boost::icl::interval_set<std::uint32_t, std::less, Interval>;
    Map mRanges;
};

}  // namespace ripple
#endif
