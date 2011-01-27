#include "../embedhttp.h"
#include <gtest/gtest.h>
#include <iostream>

TEST(parse_header, correct_pair) {
  ehttp mock;
  string tmp = "t1=t2;t3=t4;t5=t6;";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t2", result["t1"]);
  EXPECT_EQ("t4", result["t3"]);
  EXPECT_EQ("t6", result["t5"]);
}

TEST(parse_header, wrong_pair_last) {
  ehttp mock;
  string tmp = "t1=t2;t3=t4;t5";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t2", result["t1"]);
  EXPECT_EQ("t4", result["t3"]);
}

TEST(parse_header, wrong_pair_middle) {
  ehttp mock;
  string tmp = "t1=t2;t3;t5=t6";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t2", result["t1"]);
  EXPECT_EQ("t6", result["t5"]);
}

TEST(parse_header, wrong_pair_first) {
  ehttp mock;
  string tmp = "t1;t5=t6;t7=t8";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t6", result["t5"]);
  EXPECT_EQ("t8", result["t7"]);
}

TEST(parse_header, empty_pair) {
  ehttp mock;
  string tmp = ";;t7=t8;";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t8", result["t7"]);
}


TEST(parse_header, include_space) {
  ehttp mock;
  string tmp = "t5= t6;t7 =t8; t9 = t10 ;;t11=t12";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t6", result["t5"]);
  EXPECT_EQ("t8", result["t7"]);
  EXPECT_EQ("t10", result["t9"]);
  EXPECT_EQ("t12", result["t11"]);
}

TEST(parse_header, multiple_equal) {
  ehttp mock;
  string tmp = "t5=t6;t7=t8=t9";
  map <string, string> result = map <string, string>();
  int ret = mock.parse_cookie(&result, tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t6", result["t5"]);
  EXPECT_EQ("", result["t7"]);
  EXPECT_EQ("", result["t8"]);
  EXPECT_EQ("", result["t9"]);
}
