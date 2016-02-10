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
        "Usage: %s [-n requests] [-t seconds] [-c concurrency] host port path\n",
        prog);
    exit(1);
}


void main(int ac, char **av) {
    int opt;
    int arg_requests = -1;
    int arg_seconds = -1;
    int arg_concurrency = 1;
    while ((opt = getopt(ac, av, "n:t:c:")) != -1) {
        switch (opt) {
        case 'n':
            arg_requests = atoi(optarg);
            break;
        case 't':
            arg_seconds = atoi(optarg);
            break;
        case 'c':
            arg_concurrency = atoi(optarg);
            break;
        default:
            usage(av[0]);
        }
    }
    if (optind >= ac)
        usage(av[0]);
    char *hostname = av[optind];
    short port = atoi(av[optind+1]);
    char *path = av[optind+2];

    // Main

    struct hostent *he = gethostbyname(hostname);
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
    static char request[255];
    sprintf(request,
        "GET %s HTTP/1.1\r\nUser-Agent: curl/7.35.0\r\nHost: %s\r\nAccept: */*\r\n\r\n",
        path, hostname);
    printf("Concurrency: %d\n", arg_concurrency);
    printf("Server: %s:%d\n", ip, port);
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
    s_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &s_addr.sin_addr);

    int users = 0;
    for (i = 0; i < arg_concurrency; ++i) {
        if (make_a_connection(&s_addr, poll) != -1) {
            users ++;
        }
    }
    printf("%d users created\n", users);

    int nwrites = 0;
    int size = 64;
    struct epoll_event events[size];
    while (users > 0) {
        int n = epoll_wait(poll, events, size, -1);

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
    printf("done\n");
}
