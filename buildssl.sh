#!/bin/sh

g++  -o helloworldssl -Wall -g -I/usr/include/openssl -I/usr/kerberos/include ./helloworldssl.cpp ./connection.cpp ./embedhttp.cpp -L/usr/lib/ -lssl

