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

#include <BeastConfig.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/beast/core/LexicalCast.h>

namespace ripple
{
bool
RangeSet::hasValue(std::uint32_t v) const
{
    return boost::icl::contains(mRanges, v);
}

std::uint32_t
RangeSet::getFirst() const
{
    if (mRanges.empty())
        return absent;
    return boost::icl::first(mRanges);
}

// largest number not in the set that is less than the given number
std::uint32_t
RangeSet::prevMissing(std::uint32_t v) const
{
    if (v == 0)
        return absent;
    // Find returns an iterator pointing to the interval containing v-1
    auto it = mRanges.find(v - 1);
    if (it != mRanges.end())
        return it->first() - 1;
    // If no interval overlaps, then v-1 is the largest number less than v
    // not in the set
    return v - 1;
}

// Add an item to the set
void
RangeSet::setValue(std::uint32_t v)
{
    mRanges.insert(Interval(v));
}

// Add the closed interval to the set
void
RangeSet::setRange(std::uint32_t minv, std::uint32_t maxv)
{
    mRanges.insert(Interval(minv, maxv));
}

// Remove the item from the set
void
RangeSet::clearValue(std::uint32_t v)
{
    mRanges.erase(Interval(v));
}

std::string
RangeSet::toString() const
{
    std::string ret;
    for (auto const& it : mRanges)
    {
        if (!ret.empty())
            ret += ",";

        if (it.first() == it.last())
            ret += beast::lexicalCastThrow<std::string>((it.first()));
        else
            ret += beast::lexicalCastThrow<std::string>(it.first()) + "-" +
                beast::lexicalCastThrow<std::string>(it.last());
    }

    if (ret.empty())
        return "empty";

    return ret;
}

}  // ripple
