#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "epoll.h"
#include "embedhttp.h"
#include "download.h"

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

void DrEpoll::add(download_ptr dn) {
  if (dn->remaining <= 0) {
    dn->close();
    return;
  }

  struct epoll_event event;
  int fd = dn->fd_upload->getFD();
  event.events = EPOLLIN | EPOLLONESHOT;
  event.data.fd = fd;

  downloads[dn->key] = dn;
  event.data.ptr = (void *)dn.get();

  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
    // return false;
  }
}

bool DrEpoll::process() {
  struct epoll_event events[EPOLL_TASKS];

  int nfds = epoll_wait(g_epoll_fd, events, EPOLL_TASKS, 50); /* timeout 50ms */
  if (nfds == 0) return true; /* no event , no work */
  if (nfds < 0) {
    printf("[ETEST] epoll wait error\n");
    return false; /* return but this is epoll wait error */
  }

  char buffer[8196];
  for (int i = 0 ; i < nfds ; i++) {

    download *dn = (download *)events[i].data.ptr;
    int fd = dn->fd_upload->getFD();

    int r1, r2;

    // log(1) << "upload fd:" << dn->fd_upload->getFD() << " dn:" << dn->fd_download->getFD() << endl;

    r1 = dn->fd_upload->pRecv((void*) (dn->fd_upload->getFD()), buffer, sizeof(buffer));
    if (r1 < 0) {
      log(0) << "close r1 rem:" << dn->remaining << " r1: " << r1 << endl;
      epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &(events[i]));
      dn->close();
      downloads.erase(dn->key);
      continue;
    }
    r2 = dn->fd_download->pSend((void *) (dn->fd_download->getFD()), buffer, r1);
    if (r2 < 0 || r1 != r2) {
      log(0) << "close r2 rem:" << dn->remaining << " r2: " << r2 << endl;
      epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &(events[i]));
      dn->close();
      downloads.erase(dn->key);
      continue;
    }

    // log(0) << "download r:" << dn->remaining << " r2: " << r2 << endl;

    dn->remaining -= r2;
    if (dn->remaining <= 0) {
      epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, &(events[i]));
      dn->close();
      downloads.erase(dn->key);
    } else {
      struct epoll_event event;
      event.events = EPOLLIN | EPOLLONESHOT;
      event.data.fd = fd;
      event.data.ptr = dn;

      epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &(event));
    }
  }
  return true;
}

void DrEpoll::thread() {
  while(process());
}

void DrEpoll::worker() {

}
