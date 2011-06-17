#pragma once
#include <pthread.h>
#include <sys/epoll.h>
#include <map>
#include <boost/serialization/singleton.hpp>

#include "connection.h"
#define EPOLL_EVENTS 10000
#define EPOLL_THREADS 1
#define EPOLL_TASKS 200

using namespace std;

class DrEpoll : public boost::serialization::singleton<DrEpoll> {
public:
  DrEpoll() {};

  void init();
  void add(TransferPtr trans);
  bool process();
  void thread();
  void worker();

private:
  int g_epoll_fd;                /* epoll fd */
  map<string, TransferPtr> transfers;
  pthread_mutex_t mutex_pool;
};
