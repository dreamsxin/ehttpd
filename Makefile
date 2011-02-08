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

dr_mysql.o : dr_mysql.cc *.h
	$(CXX) $(CXXFLAGS) -c -o dr_mysql.o dr_mysql.cc -I/usr/local/include/cppconn
	$(CXX) $(CXXFLAGS) -c -o mysql_test.o mysql_test.cc -I/usr/local/include/cppconn

mysql_test : dr_mysql.o mysql_test.o
	$(CXX) $(CXXFLAGS) -o $(BIN)/mysql_test -lmysqlcppconn $^

$(DRSERVER) : thread.o embedhttp.o connection.o dr_mysql.o
	$(CXX) $(CXXFLAGS) -o $(BIN)/$(DRSERVER) -lpthread -lmysqlcppconn $^

