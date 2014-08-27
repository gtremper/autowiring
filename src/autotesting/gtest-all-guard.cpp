// Copyright (C) 2012-2014 Leap Motion, Inc. All rights reserved.
#include "stdafx.h"
#include <gtest/gtest-all.cc>
#include "AutowiringEnclosure.h"

using namespace std;

int autotesting_main(int argc, const char* argv[])
{
  auto& listeners = testing::UnitTest::GetInstance()->listeners();
  listeners.Append(new AutowiringEnclosure);
  testing::InitGoogleTest(&argc, (char**)argv);
  return RUN_ALL_TESTS();
}
