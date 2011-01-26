#include <pthread.h>
#include <stdio.h>
#include <sys/timeb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <deque>

using namespace std;

#define MAX_THREAD 100
#define PORT 3355

int listenfd;

typedef struct {
  pthread_t tid;
  deque<int> *conn_pool;
} Thread;

void nonblock(int sockfd) {
  int opts;
  opts = fcntl(sockfd, F_GETFL);
  if(opts < 0) {
    perror("fcntl(F_GETFL)\n");
    exit(1);
  }
  opts = (opts | O_NONBLOCK);
  if(fcntl(sockfd, F_SETFL, opts) < 0) {
    perror("fcntl(F_SETFL)\n");
    exit(1);
  }
}

pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;

void *main_thread(void *arg) {
  Thread *thread = (Thread *)arg;

  while (1) {
    // fetch a new job.
    int socket;
    for(;;) {
      pthread_mutex_lock(&new_connection_mutex);
      if (!thread->conn_pool->empty()) {
        socket = thread->conn_pool->front();
        thread->conn_pool->pop_front();
      }
      pthread_mutex_unlock(&new_connection_mutex);
      sleep(1);
    }

    // job

  }
}

int main() {
  char c;
  struct sockaddr_in srv, cli;
  int clifd;
  int tid;
  int i;
  int choosen;
  signal(SIGPIPE, SIG_IGN);

  deque<int> conn_pool;
  Thread threads[MAX_THREAD];

  /* create threads */
  for(i = 0; i < MAX_THREAD; i++) {
    threads[i].conn_pool = &conn_pool;
    pthread_create(&threads[i].tid, NULL, &main_thread, (void *)&threads[i]);
  }

  if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("sockfd\n");
    exit(1);
  }

  int yes;
  setsockopt(listenfd, SOL_SOCKET,SO_REUSEADDR, (void *)&yes, sizeof(int));

  bzero(&srv, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_addr.s_addr = htonl(INADDR_ANY);
  srv.sin_port = htons(PORT);
  if( bind(listenfd, (struct sockaddr *) &srv, sizeof(srv)) < 0) {
    perror("bind\n");
    exit(1);
  }

  listen(listenfd, 1024);

  struct sockaddr_in client_addr
  int sin_size = sizeof(struct sockaddr_in);

  for( ; ; ) {
    if((clifd = accept(listenfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
      perror("accept\n");
      exit(1);
    }
    nonblock(clifd);

    pthread_mutex_lock(&new_connection_mutex);
    conn_pool.push_back(clifd);
    pthread_mutex_unlock(&new_connection_mutex);
  }

  return 0;
}

