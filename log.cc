#include "./log.h"

ofstream null_stream("/dev/null");

ostream & log(int debuglevel) {
  if (debuglevel >= DEBUGLEVEL) {
    time_t rawtime;
    time ( &rawtime );
    string timestamp = ctime(&rawtime);
    timestamp = timestamp.substr(0, timestamp.length()-1);
    switch(debuglevel) {
    case 0: cout << "[D] " << timestamp << " "; break;
    case 1: cout << "[I] " << timestamp << " "; break;
    case 2: cout << "[E] " << timestamp << " "; break;
    }
    return cout;
  } else {
    return null_stream;
  }
}
