# Flags passed to the C++ compiler.
CXXFLAGS += -g -Wall -Wextra

# All tests produced by this Makefile.  Remember to add new tests you
# created to the list.
DRSERVER = drserver

BIN = bin

# House-keeping build targets.

all : $(DRSERVER)

clean :
	rm -f $(BIN)/$(DRSERVER) gtest.a gtest_main.a *.o

thread.o : thread.cc *.h
	$(CXX) $(CXXFLAGS) -c -o thread.o thread.cc

embedhttp.o : embedhttp.cc *.h
	$(CXX) $(CXXFLAGS) -c -o embedhttp.o embedhttp.cc

connection.o : connection.cc *.h
	$(CXX) $(CXXFLAGS) -c -o connection.o connection.cc

$(DRSERVER) : thread.o embedhttp.o connection.o
	$(CXX) $(CXXFLAGS) -o $(BIN)/$(DRSERVER) -lpthread $^

