#pragma once
#include <time.h>
#include <iostream>
#include <fstream>
using namespace std;

#ifndef DEBUGLEVEL
#define DEBUGLEVEL 0
#endif

ostream & log(int debuglevel);
