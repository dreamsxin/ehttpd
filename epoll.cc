#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "epoll.h"
#include "embedhttp.h"

#include "log.h"


void *epoll_thread(void *) {
  DrEpoll::get_mutable_instance().thread();
}

void DrEpoll::init() {
  g_epoll_fd = epoll_create(EPOLL_EVENTS);
  if(g_epoll_fd < 0) {
    return;
  }

  pthread_t threads[EPOLL_THREADS];
  /* create threads */
  for (int i = 0; i < EPOLL_THREADS; i++) {
    pthread_create(&threads[i], NULL, &epoll_thread, NULL);
  }

  //  mutex_pool = PTHREAD_MUTEX_INITIALIZER;
}

void DrEpoll::add(TransferPtr trans) {
  if (trans->dn->remaining <= 0) {
    trans->close();
    return;
  }

  struct epoll_event event;
  int fd = trans->up->ehttp->getFD();
  event.events = EPOLLIN | EPOLLONESHOT;
  event.data.fd = fd;

  transfers[trans->dn->key] = trans;
  event.data.ptr = (void *)trans.get();

  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
    return;
  }

  string &key = trans->up->key;
  size_t found = key.find("-");
  if (found != string::npos) {
    string date = key.substr(0, found);
    string path = Ehttp::get_save_path() + "/" + date;
    mkdir(path.c_str(), 0755);
  }
}

void saveToFile(UploadPtr up, const char *buffer, int r);

bool DrEpoll::process() {
  struct epoll_event events[EPOLL_TASKS];

  int nfds = epoll_wait(g_epoll_fd, events, EPOLL_TASKS, 50); /* timeout 50ms */
  if (nfds == 0) return true; /* no event , no work */
  if (nfds < 0) {
    printf("[ETEST] epoll wait error\n");
    return false; /* return but this is epoll wait error */
  }

  char buffer[1024];
  for (int i = 0 ; i < nfds ; i++) {

    Transfer *trans = (Transfer *)events[i].data.ptr;
    int fd = trans->up->ehttp->getFD();

    int r1, r2;
    log(1) << "upload fd:" << trans->up->ehttp->getFD() << " dn:" << trans->dn->ehttp->getFD() << endl;

    r1 = trans->up->ehttp->recv(buffer, sizeof(buffer));
    if (r1 < 0) {
      log(1) << "close r1 rem:" << trans->dn->remaining << " r1: " << r1 << endl;
      epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &(events[i]));
      trans->close();
      transfers.erase(trans->dn->key);
      continue;
    }
    log(1) << "download rem:" << trans->dn->remaining << " r1: " << r1 << endl;

    trans->up->ehttp->timestamp = trans->dn->ehttp->timestamp =  time(NULL);
    r2 = trans->dn->ehttp->send(buffer, r1);
    if (r2 < 0 || r1 != r2) {
      log(1) << "close r2 rem:" << trans->dn->remaining << " r2: " << r2 << endl;
      epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &(events[i]));
      trans->close();
      transfers.erase(trans->dn->key);
      continue;
    }
   
    trans->dn->remaining -= r1;
    if (trans->dn->remaining <= 0) {
      log(1) << "remaining <= 0 : " << trans->dn->remaining << endl;
      epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &(events[i]));
      trans->close();
      transfers.erase(trans->dn->key);
    } else {
      log(1) << "remaining > 0: " << trans->dn->remaining << endl;
      struct epoll_event event;
      event.events = EPOLLIN | EPOLLONESHOT;
      event.data.fd = fd;
      event.data.ptr = trans;
      epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &(event));
    }
  }
  return true;
}

void DrEpoll::thread() {
  while(1) {
    process();
  }
}

void DrEpoll::worker() {

}
