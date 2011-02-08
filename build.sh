#!/bin/sh
#g++  -o sample1 -Wall -g ./sample1.cpp ./connection.cpp ./embedhttp.cpp
#g++  -o helloworld -Wall -g ./helloworld.cpp ./connection.cpp ./embedhttp.cpp
g++  -o bin/thread -Wall -g -lpthread ./thread.cc ./embedhttp.cpp ./connection.cpp
