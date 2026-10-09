#include <gtest/gtest.h>
#include "tools/build_defs/tests/samples/cc/lib.h"
TEST(Lib, Adds) { EXPECT_EQ(Add(2, 3), 5); }
