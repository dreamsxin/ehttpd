#include "../embedhttp.h"
#include <gtest/gtest.h>
#include <iostream>

TEST(parse_header, simple) {
  ehttp mock;
  string tmp = "t1=t2;t3=t4;t5=t6";
  map <string, string> result = map <string, string>();
  mock.parse_cookie(&result, tmp);
  //  EXPECT_EQ("t2",
  EXPECT_EQ("t2", result["t1"]);
  EXPECT_EQ("t4", result["t3"]);
  EXPECT_EQ("t6", result["t5"]);
}

TEST(parse_header, parse_error) {
  ehttp mock;
  string tmp = "t1=t2;t3=t4;t5";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);
  EXPECT_EQ(EHTTP_ERR_GENERIC, ret);
}

