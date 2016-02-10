#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>


struct UserData {
    int fd;
};

int make_a_connection(const struct sockaddr_in* paddr, int poll) {
    int c_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (c_fd == -1) {
        perror("create client socket failed");
        return -1;
    }

    int r = connect(c_fd, (struct sockaddr*)paddr, sizeof(struct sockaddr));
    int connected = 1;
    if (r < 0) {
        if (errno == EINPROGRESS)
            connected = 0;
        else {
            perror("connect failed");
            return -1;
        }
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    struct UserData *ud = malloc(sizeof(struct UserData));
    memset(ud, 0, sizeof(struct UserData));
    ud->fd = c_fd;
    event.data.ptr = ud;
    if (!connected)
        event.events |= EPOLLOUT;

    r = epoll_ctl(poll, EPOLL_CTL_ADD, c_fd, &event);
    if (r == -1) {
        perror("epoll add fd failed");
        return -1;
    }
    return c_fd;
}


int read_all(struct UserData * ud) {
    // return boolean to indicate whether we should close the connection
    static char buf[1024];
    int done = 0;
    while (1) {
        ssize_t count = read(ud->fd, buf, sizeof(buf));
        if (count == -1) {
            if (errno != EAGAIN) {
// Connection reset by peer
                perror("read error");
                done = 1;
            }
           break;
        } else if (count == 0) {
            done = 1;
            break;
        }
        //printf(">> read %ld bytes\n", count);
    }
    return done;
}


void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] host port path\n"
        "   -n requests\n"
        "   -t timelimit\n"
        "   -c concurrency\n"
        "   -p POST-file\n"
        "   -T content-type\n"
        , prog);
    exit(1);
}


int running = 1;

void stop_loop(int sig) {
    running = 0;
}

struct Args {
    // arguments
    char *hostname;
    short port;
    char *path;

    // options
    int requests;
    int timelimit;
    int concurrency;
    char *content_type;
    char *post_file;
};

void parse_args(struct Args *args, int ac, char **av) {
    int opt;
    while ((opt = getopt(ac, av, "n:t:c:")) != -1) {
        switch (opt) {
        case 'n':
            args->requests = atoi(optarg);
            if (args->requests < 1) {
                fprintf(stderr, "-n must > 0\n");
                usage(av[0]);
            }
            args->timelimit = -1;
            break;
        case 't':
            args->timelimit = atoi(optarg);
            if (args->timelimit < 1) {
                fprintf(stderr, "-t must > 0\n");
                usage(av[0]);
            }
            args->timelimit *= 1000000;
            args->requests = -1;
            break;
        case 'c':
            args->concurrency = atoi(optarg);
            if (args->concurrency < 1) {
                fprintf(stderr, "-c must > 0\n");
                usage(av[0]);
            }
            break;
        default:
            usage(av[0]);
        }
    }
    if (optind >= ac)
        usage(av[0]);
    args->hostname = av[optind];
    args->port = atoi(av[optind+1]);
    args->path = av[optind+2];
}

void main(int ac, char **av) {
    struct Args args = {NULL, -1, NULL, 1, -1, 1, NULL, NULL};
    parse_args(&args, ac, av);

    struct hostent *he = gethostbyname(args.hostname);
    if (he == NULL) {
        perror("gethostbyname failed");
        exit(1);
    }
    int i;
    char ip[16];
    struct in_addr **addr_list = (struct in_addr **) he->h_addr_list;
    for (i = 0; addr_list[i] != NULL; i++) {
        strcpy(ip, inet_ntoa(*addr_list[i]));
        break;
    }
    char hostport[64];
    if (args.port == 80)
        strcpy(hostport, args.hostname);
    else
        sprintf(hostport, "%s:%d", args.hostname, args.port);
    char request[1024];
    sprintf(request,
        "GET %s HTTP/1.1\r\nUser-Agent: curl/7.35.0\r\nHost: %s\r\nAccept: */*\r\n\r\n",
        args.path, hostport);
    printf("Concurrency level: %d\n", args.concurrency);
    printf("Request: %s", request);
    printf("===========\n");

    int poll = epoll_create(1);
    if (poll == -1) {
        perror("create epoll failed");
        exit(1);
    }

    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(args.port);
    inet_pton(AF_INET, ip, &s_addr.sin_addr);

    int users = 0;
    for (i = 0; i < args.concurrency; ++i) {
        if (make_a_connection(&s_addr, poll) != -1) {
            users ++;
        }
    }
    printf("%d users created\n", users);

    int nwrites = 0;
    int size = 64;
    struct epoll_event events[size];

    signal(SIGINT, stop_loop);

    struct timeval tvstart, tvnow;
    gettimeofday(&tvstart, NULL);
    long starttime = tvstart.tv_sec * 1000000 + tvstart.tv_usec, now;

    while (users > 0 && running &&
           (args.requests == -1 || nwrites < args.requests)) {
        int n = epoll_wait(poll, events, size, args.timelimit);

        if (args.timelimit != -1) {
            gettimeofday(&tvnow, NULL);
            now = tvnow.tv_sec * 1000000 + tvnow.tv_usec;
            if ((now - starttime) >= args.timelimit)
                break;
        }

        if (n == -1) {
            perror("epoll wait failed");
            close(poll);
            break;
        }
        for (i = 0; i < n; ++i) {
            struct epoll_event * ev = &events[i];
            if (ev->events & EPOLLIN) {
                struct UserData * ud = ev->data.ptr;
                if (read_all(ud)) {
                    close(ud->fd);
                    if (make_a_connection(&s_addr, poll) == -1)
                        users --;
                } else {
                    write(ud->fd, request, strlen(request));
                    nwrites ++;
                    if (nwrites % 10000 == 0) {
                        printf("%d requests\n", nwrites);
                    }
                }
            } else if (ev->events & EPOLLOUT) {
                struct UserData * ud = ev->data.ptr;
                write(ud->fd, request, strlen(request));
                nwrites ++;
            } else {
                perror("epoll wait error. continue");
            }
        }
    }
    gettimeofday(&tvnow, NULL);
    now = tvnow.tv_sec * 1000000 + tvnow.tv_usec;
    double took = (double)(now - starttime) / 1000000;
    printf("Took %f seconds to send %d requests\n", took, nwrites);
}

// TODO
// parse http response
// simple statistics
