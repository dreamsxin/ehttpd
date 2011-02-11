#include "../embedhttp.h"
#include <gtest/gtest.h>
#include <iostream>

TEST(unescape, practice) {
  ehttp mock;
  string tmp = "command=getfolderlist&incrkey=39&jsondata={ \
   \"list\" : [ \
      { \
         \"name\" : \"C%3a\\\\\", \
         \"path\" : \"C%3a\\\\\", \
         \"type\" : \"DRIVE_HDD\" \
      }, \
      { \
         \"name\" : \"D%3a\\\\\", \
         \"path\" : \"D%3a\\\\\", \
         \"type\" : \"DRIVE_HDD\" \
      }, \
      { \
         \"name\" : \"E%3a\\\\\", \
         \"path\" : \"E%3a\\\\\", \
         \"type\" : \"DRIVE_CDROM\" \
      }, \
      { \
         \"name\" : \"F%3a\\\\\", \
         \"path\" : \"F%3a\\\\\", \
         \"type\" : \"DRIVE_CDROM\" \
      }, \
      { \
         \"name\" : \"G%3a\\\\\", \
         \"path\" : \"G%3a\\\\\", \
         \"type\" : \"DRIVE_REMOTE\" \
      }, \
      { \
         \"name\" : \"H%3a\\\\\", \
         \"path\" : \"H%3a\\\\\", \
         \"type\" : \"DRIVE_REMOTE\" \
      } \
   ], \
   \"parent\" : \"\" \
}";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("@ff$gg", tmp);
}

TEST(unescape, simple) {
  ehttp mock;
  string tmp = "%40ff%24gg";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("@ff$gg", tmp);
}

TEST(unescape, simple2) {
  ehttp mock;
  string tmp = "%26ff%40";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("&ff@", tmp);
}

TEST(unescape, simple3) {
  ehttp mock;
  string tmp = "ggdfg";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("ggdfg", tmp);
}

TEST(unescape, simple4) {
  ehttp mock;
  string tmp = "%25a%25s%25d%25f%25a%25s%25d%25b";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("%a%s%d%f%a%s%d%b", tmp);
}

TEST(unescape, simple5) {
  ehttp mock;
  string tmp = "%25a%25s%2ad%25f%25a%2fs%25d%25b";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("%a%s*d%f%a/s%d%b", tmp);
}

TEST(unescape, error) {
  ehttp mock;
  string tmp = "%25a%25s%2ad%25f%25a%2fs%25d%25b%2";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_GENERIC);
}

TEST(unescape, error2) {
  ehttp mock;
  string tmp = "%25a%25s%2ad%25f%25a%2fs%25d%25b%";
  int ret = mock.unescape(&tmp);
  EXPECT_EQ(ret, EHTTP_ERR_GENERIC);
}

TEST(parse_header, correct_pair) {
  ehttp mock;
  string tmp = "t1=t2;t3=t4;t5=t6;";
  int ret = mock.parse_cookie(tmp);
  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t2", mock.ptheCookie["t1"]);
  EXPECT_EQ("t4", mock.ptheCookie["t3"]);
  EXPECT_EQ("t6", mock.ptheCookie["t5"]);
}

TEST(parse_header, wrong_pair_last) {
  ehttp mock;
  string tmp = "t1=t2;t3=t4;t5";
  int ret = mock.parse_cookie(tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t2", mock.ptheCookie["t1"]);
  EXPECT_EQ("t4", mock.ptheCookie["t3"]);
}

TEST(parse_header, wrong_pair_middle) {
  ehttp mock;
  string tmp = "t1=t2;t3;t5=t6";
  int ret = mock.parse_cookie(tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t2", mock.ptheCookie["t1"]);
  EXPECT_EQ("t6", mock.ptheCookie["t5"]);
}

TEST(parse_header, wrong_pair_first) {
  ehttp mock;
  string tmp = "t1;t5=t6;t7=t8";
  int ret = mock.parse_cookie(tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t6", mock.ptheCookie["t5"]);
  EXPECT_EQ("t8", mock.ptheCookie["t7"]);
}

TEST(parse_header, empty_pair) {
  ehttp mock;
  string tmp = ";;t7=t8;";
  int ret = mock.parse_cookie(tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t8", mock.ptheCookie["t7"]);
}


TEST(parse_header, include_space) {
  ehttp mock;
  string tmp = "t5= t6;t7 =t8; t9 = t10 ;;t11=t12";
  int ret = mock.parse_cookie(tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t6", mock.ptheCookie["t5"]);
  EXPECT_EQ("t8", mock.ptheCookie["t7"]);
  EXPECT_EQ("t10", mock.ptheCookie["t9"]);
  EXPECT_EQ("t12", mock.ptheCookie["t11"]);
}

TEST(parse_header, multiple_equal) {
  ehttp mock;
  string tmp = "t5=t6;t7=t8=t9";
  int ret = mock.parse_cookie(tmp);

  EXPECT_EQ(ret, EHTTP_ERR_OK);
  EXPECT_EQ("t6", mock.ptheCookie["t5"]);
  EXPECT_EQ("", mock.ptheCookie["t7"]);
  EXPECT_EQ("", mock.ptheCookie["t8"]);
  EXPECT_EQ("", mock.ptheCookie["t9"]);
}
