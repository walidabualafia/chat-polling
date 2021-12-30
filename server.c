/*
 * server.c - a chat server (and monitor) that uses pipes and sockets
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#define MAX_CLIENTS 10

// constants for pipe FDs
#define WFD 1
#define RFD 0


/**
 * nonblock - a function that makes a file descriptor non-blocking
 * @param fd file descriptor
 */
void nonblock(int fd) {
  int flags;

  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    perror("fcntl (get):");
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | FNDELAY) == -1) {
    perror("fcntl (set):");
    exit(1);
  }

}

/* get_in_addr - gets the sockaddr, regardless of sockaddr family
 * @param sa - sockaddr
 */
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 * monitor - provides a local chat window
 * @param srfd - server read file descriptor
 * @param swfd - server write file descriptor
 */
void monitor(int srfd, int swfd) {
  /* Variables */
  int rbytes, wbytes;     // read/write byte count 
  int num_events;         // count of revents for poll
  char buf[100];          // buffer string to hold the message
  struct pollfd pfds[2];  // array of poll fds
  
  // set the read fds to nonblocking
  nonblock(srfd);
  nonblock(STDIN_FILENO);

  do {
    pfds[0].fd = STDIN_FILENO;  // STDIN
    pfds[0].events = POLLIN;
    pfds[1].fd = srfd;          // srfd
    pfds[1].events = POLLIN;

    // call poll with no timeout
    if ((num_events = poll(pfds, 2, -100)) == -1) {
      perror("monitor: poll");
      exit(1);
    }

    // when poll returns, handle reads/writes
    if (num_events != 0) {
      int pollin_stdin = pfds[0].revents & POLLIN;
      int pollin_srfd = pfds[1].revents & POLLIN;

      if (pollin_stdin) {
        /* Read from STDIN */
        if ((rbytes = read(STDIN_FILENO, buf, sizeof buf)) == -1) {
          perror("server: read from stdin");
          exit(1);
        }
        if (rbytes == 0) {      // EOF
          break;
        }
        /* Write to SWFD */
        if ((wbytes = write(swfd, buf, rbytes)) == -1) {
          perror("server: write to swfd");
          exit(1);
        }
        if (wbytes == 0) {      // EOF
          break;
        }
      } else if (pollin_srfd) {
        /* Read from SRFD */
        if ((rbytes = read(srfd, buf, sizeof buf)) == -1) {
          perror("server: read from srfd");
          exit(1);
        }
        if (rbytes == 0) {      // EOF
          break;
        }
        /* Write to STDOUT */
        if ((wbytes = write(STDOUT_FILENO, buf, rbytes)) == -1) {
          perror("server: write to stdout");
          exit(1);
        }
        if (wbytes == 0) {      // EOF
          break;
        }
      }
    }
  } while(1);

  /* Cleanup Sequence */
  close(srfd);
  close(swfd);
  exit(0);
}



/*
 * server - relays chat messages
 * @param mrfd - monitor read file descriptor
 * @param mwfd - monitor write file descriptor
 * @param portno - TCP/IP port number to listen on
 */
void server(int mrfd, int mwfd, char *portno) {
  /* Variables */
  int sfd;                                                      // socket fd

  int rbytes, wbytes;                                           // read/write bytes

  int clientfd;                                                 // incoming connection fd
  struct sockaddr_storage clientaddr;                           // incoming connection addr
  socklen_t addrlen;                                            // incoming connection addrlen

  char buf[100];                                                // string buffer holding exchanged messages
  
  char clientIP[INET6_ADDRSTRLEN];                              // incoming connection's IP addr

  int num_events;                                               // number of events returned by poll
  int existing_connections = 0;                                 // count of fds in pfds
  struct pollfd *pfds = malloc(sizeof *pfds * MAX_CLIENTS);     // set of poll fds

  int yes = 1;                                                  // used in setsockopt
  struct addrinfo hints, *ai, *p;                               // placeholders for getaddrinfo

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  // get the addr info
  if ((getaddrinfo(NULL, portno, &hints, &ai)) == -1) {
    perror("getaddrinfo");
    exit(1);
  }

  for (p = ai; p != NULL; p = p->ai_next) {
    // create a socket
    if ((sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");  
      exit(1);
    }

    // set the sockaddr to be reusable
    if ((setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
      perror("setsockopt");
      exit(1);
    }

    // bind the socket to TCP port
    if ((bind(sfd, p->ai_addr, p->ai_addrlen)) == -1) {
      perror("bind");
      close(sfd);
      exit(1);
    }

    break;
  }

  // failed to get addrinfo
  if (p == NULL) {
    exit(1);
  }

  // listen for incoming connections on the socket
  if ((listen(sfd, MAX_CLIENTS)) == -1) {
    perror("listen");
    exit(1);
  }

  // set the socket fd to be nonblocking
  nonblock(sfd);
  nonblock(mrfd);

  freeaddrinfo(ai);

  // add active fds to the poll fds
  pfds[0].fd = sfd;     // socket
  pfds[0].events = POLLIN;
  pfds[1].fd = mrfd;    // monitor
  pfds[1].events = POLLIN;
  existing_connections = 2;

  // main loop
  do {
    // call poll with 0.1s timeout
    if ((num_events = poll(pfds, existing_connections, 100)) == -1) {
      perror("poll");
      exit(1);
    }

    // if poll times out, continue
    if (num_events == 0) {
      continue;
    }

    int i;
    // when poll returns
    if (num_events != 0) {
      // Bitwise && to check if there is something to read on the listener socket
      int pollin_sfd = pfds[0].revents & POLLIN;
      // Biwise && to check if there is something to read on the monitor pipe read end
      int pollin_mrfd = pfds[1].revents & POLLIN;

      // if sockfd is ready
      if (pollin_sfd) {
        // attempt to accept connection
        if ((clientfd = accept(sfd, (struct sockaddr *)&clientaddr, &addrlen)) == -1) {
          perror("accept");
          exit(1);
        } else {
          // if connection accepted, add the new client to pfds
          nonblock(clientfd);
          pfds[existing_connections].fd = clientfd;
          pfds[existing_connections].events = POLLIN;
          existing_connections++;

          printf("new connection from %s...\n", 
              inet_ntop(clientaddr.ss_family,
                get_in_addr((struct sockaddr *)&clientaddr), clientIP, INET6_ADDRSTRLEN));

        }

      }

      // broadcast messages from monitor to all clients
      if (pollin_mrfd) {
        // read the message from the monitor read fd
        if ((rbytes = read(mrfd, buf, sizeof buf)) == -1) {
          perror("read from monitor");
          exit(1);
        }
        if (rbytes == 0) {
          break;
        }
        // loop over all connected clients
        for (i = 2; i < existing_connections; i++) {
          // write message to client fd
          if ((wbytes = write(pfds[i].fd, buf, rbytes)) == -1) {
            perror("write to client");
            exit(1);
          } 
        }
      }
   
      // loop over all connected clients
      for (i = 2; i < existing_connections; i++) {
        // for each client with a message
        if (pfds[i].revents & POLLIN) {
          // read message
          if ((rbytes = read(pfds[i].fd, buf, sizeof buf)) == -1) {
            perror("read from client");
            exit(1);
          }

          // if get disconnect 
          if (rbytes == 0) {
            // print disconnect message with client's IP addr
            printf("%s has disconnected...\n", 
                inet_ntop(clientaddr.ss_family,
                  get_in_addr((struct sockaddr *)&clientaddr), clientIP, INET6_ADDRSTRLEN));

            // close the client's fd
            close(pfds[i].fd);

            pfds[i] = pfds[existing_connections-1];
            pfds[i] = pfds[existing_connections-1];
            existing_connections--;
          }
          // if not disconnect,
          // write message to monitor
          if ((wbytes = write(mwfd, buf, rbytes)) == -1) {
            perror("write to monitor");
            exit(1);
          }
          // write message to every other (not myself) clients
          // loop over all clients
          int j;
          for (j = 2; j < existing_connections; j++) {
            // if client is not myself
            if (j != i) {
              // write message to client
              if ((wbytes = write(pfds[j].fd, buf, rbytes)) == -1) {
                perror("write message from client to clients");
                exit(1);
              }
            }
          }
        }
      } 
    }
  } while (1);

}



int main(int argc, char **argv) {
  /* Variables */
  int opt;                      // command line optional arg

  char *defport = "5055";       // default port

  int ser_mon[2];               // server -> monitor pipe
  int mon_ser[2];               // monitor -> server pipe

  pid_t cpid;                   // child process id


  /* Command Line Arguments can be set with 2 flags:
   *    - p [port #]
   *    - h [host] 
   */
  while ((opt = getopt(argc, argv, "h:p:")) != -1) {
    switch (opt) {
    case 'p':
      // set the port to be the argument specified by the user
      defport = optarg;
    default:
      printf("usage: ./server [-p port]\n");
    break;
    }
  }

  // create the server -> monitor pipe
  if ((pipe(ser_mon)) == -1) {
    perror("pipe server->monitor");
    exit(1);
  }

  // create the monitor -> server pipe
  if ((pipe(mon_ser)) == -1) {
    perror("pipe monitor->server");
    exit(1);
  }

  // fork the process
  if ((cpid = fork()) == -1) {
    perror("fork");
    exit(1);
  }
  /* Child Process */
  if (cpid == 0) {
    close(mon_ser[1]);
    close(ser_mon[0]);
    // call monitor
    monitor(mon_ser[0], ser_mon[1]);
    // when the child returns, close remaining fds
    close(mon_ser[0]);
    close(ser_mon[1]);
    exit(0);
  }
  /* Parent Process */
  else {
    close(mon_ser[0]);
    close(ser_mon[1]);
    // call server
    server(ser_mon[0], mon_ser[1], defport);
    // when the parent returns, close remaning fds
    close(mon_ser[1]);
    close(ser_mon[0]);
    wait(NULL); /* Reap Child */
  }
}
