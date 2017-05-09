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
#include <ripple/beast/unit_test.h>

namespace ripple {

class RangeSet_test : public beast::unit_test::suite
{
public:
    void
    testSetAndHas()
    {
        testcase("set and has");

        RangeSet r1, r2;

        BEAST_EXPECT(r1.getFirst() == RangeSet::absent);
        r1.setRange(1, 10);
        r1.clearValue(5);
        r1.setRange(11, 20);

        for (int i = 1; i <= 20; ++i)
        {
            if (i != 5)
                BEAST_EXPECT(r1.hasValue(i));
            else
                BEAST_EXPECT(!r1.hasValue(i));
        }
        BEAST_EXPECT(r1.getFirst() == 1);
        r1.clearValue(1);
        BEAST_EXPECT(r1.getFirst() == 2);

        // Create with gap at 5
        r2.setRange(1, 4);
        r2.setRange(6, 10);
        // Marge with existing range
        r2.setRange(10, 20);
        // subset of existing range
        r2.setRange(11, 13);
        // Extend existing range
        r2.setValue(21);
        for (int i = 1; i <= 21; ++i)
        {
            if (i != 5)
                BEAST_EXPECT(r2.hasValue(i));
            else
                BEAST_EXPECT(!r2.hasValue(i));
        }

        // Adding 5 creates complete range
        r2.setValue(5);
        for (int i = 1; i <= 21; ++i)
        {
            BEAST_EXPECT(r2.hasValue(i));
        }

        // Additional tests to complete coverage

        // Isolate 1
        r2.clearValue(2);
        for (int i = 1; i <= 21; ++i)
        {
            if (i != 2)
                BEAST_EXPECT(r2.hasValue(i));
            else
                BEAST_EXPECT(!r2.hasValue(i));
        }
        // Remove 1 as well and shrink 21
        r2.clearValue(1);
        r2.clearValue(21);
        for (int i = 1; i <= 21; ++i)
        {
            if (i > 2 && i < 21)
                BEAST_EXPECT(r2.hasValue(i));
            else
                BEAST_EXPECT(!r2.hasValue(i));
        }


    }

    void
    testOverlappingRanges()
    {
        RangeSet r;

        r.setRange(4,9);
        for (int i = 1; i <= 10; ++i)
        {
            if (i >=4 && i <= 9)
                BEAST_EXPECT(r.hasValue(i));
            else
                BEAST_EXPECT(!r.hasValue(i));
        }

        // Add overlap range with front end 
        r.setRange(1,5);
        for (int i = 1; i <= 10; ++i)
        {
            if (i <= 9)
                BEAST_EXPECT(r.hasValue(i));
            else
                BEAST_EXPECT(!r.hasValue(i));
        }
        
        // Add overlap with end
        r.setRange(7,10);
        for (int i = 1; i <= 10; ++i)
        {
            if(!BEAST_EXPECT(r.hasValue(i)))
                std::cout << i << "\n";
        }

        // Add existing range in middle
        r.setRange(5,7);
        for (int i = 1; i <= 10; ++i)
        {
            BEAST_EXPECT(r.hasValue(i));
        }
    }

    void
    testPrevMissing()
    {
        testcase("prevMissing");

        // Set will include:
        // [ 0, 5]
        // [10,15]
        // [20,25]
        // etc...

        RangeSet set;
        for (int i = 0; i < 10; ++i)
            set.setRange(10 * i, 10 * i + 5);

        for (int i = 0; i < 100; ++i)
        {
            int const oneBelowRange = (10 * (i / 10)) - 1;

            int const expectedPrevMissing =
                ((i % 10) > 6) ? (i - 1) : oneBelowRange;

            BEAST_EXPECT(set.prevMissing(i) == expectedPrevMissing);
        }
    }

    void
    testToString()
    {
        testcase("toString");

        RangeSet set;
        BEAST_EXPECT(set.toString() == "empty");

        set.setValue(1);
        BEAST_EXPECT(set.toString() == "1");

        set.setRange(4, 6);
        BEAST_EXPECT(set.toString() == "1,4-6");

        set.setValue(2);
        BEAST_EXPECT(set.toString() == "1-2,4-6");

        set.clearValue(4);
        set.clearValue(5);
        BEAST_EXPECT(set.toString() == "1-2,6");
    }
    void
    run()
    {
        testSetAndHas();
        testOverlappingRanges();
        testPrevMissing();
        testToString();
    }
};

BEAST_DEFINE_TESTSUITE(RangeSet, ripple_basics, ripple);

}  // namespace ripple
