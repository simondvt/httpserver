#define _DEFAULT_SOURCE // for NI_MAXHOST, NI_NUMERICHOST, IFF_UP
#define _GNU_SOURCE     // for secure_getenv

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <pthread.h>
#include <getopt.h>

#include "macro.h"
#include "handle_client.h"

#define DEFAULT_ROOT getenv("HOME")
#define DEFAULT_PORT 6969

void sig_int_handler(int sig);
void print_interface_ip(in_port_t);

int main(int argc, char **argv)
{
    char *root_dir = DEFAULT_ROOT;
    in_port_t port = DEFAULT_PORT;

    opterr = 0; // don't let getopt print error messages
    char c_opt;
    while ((c_opt = getopt(argc, argv, "r:p:h::")) != -1)
    {
        switch (c_opt)
        {
        case 'h':
            printf("Usage: %s [-r <root-path>] [-p <port>]\n", argv[0]);
            exit(EXIT_SUCCESS);
            break;
        case 'r':
            root_dir = optarg;
            break;

        case 'p':
            port = atoi(optarg);
            break;

        case '?':
            fprintf(stderr, "Command unknown\nUsage: %s [-r <root-path>] [-p <port>]\n", argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

    int check_root_dir;
    CHECK_ERRNO(check_root_dir = stat(root_dir, &(struct stat){0}));
    if (check_root_dir == -1)
    {
        fprintf(stderr, "Cannot set root dir to %s\n", root_dir);
        exit(EXIT_FAILURE);
    }

    CHECK_ERRNO(signal(SIGINT, sig_int_handler));

    struct sockaddr_in server_sa;
    memset(&server_sa, 0, sizeof(server_sa));
    server_sa.sin_family = AF_INET;
    server_sa.sin_addr.s_addr = htonl(INADDR_ANY);
    server_sa.sin_port = htons(port);

    int listen_sfd;
    CHECK_ERRNO(listen_sfd = socket(AF_INET, SOCK_STREAM, 0));
    CHECK_ERRNO(setsockopt(listen_sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)));

    int bind_errno = errno = 0;
    CHECK_ERRNO(bind(listen_sfd, (struct sockaddr *)&server_sa, sizeof(server_sa)));
    bind_errno = errno;
    if (bind_errno == EACCES)
    {
        fprintf(stderr, "Cannot use port %d, you must be superuser.\n", port);
        exit(EXIT_FAILURE);
    }
    // FIXME: non riesco a distinguire tra root e sudo
    //drop_root_privileges(); // if user run program with sudo to bind a well known port

    CHECK_ERRNO(listen(listen_sfd, LISTEN_BACKLOG));

    printf("Serving [%s] on:\n", root_dir);
    print_interface_ip(port);

    pthread_t thread;
    //pthread_attr_t thread_attr;
    //pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

    for (;;)
    {
        struct sockaddr_in client_sa;
        socklen_t client_sa_len = sizeof(client_sa);
        int client_sfd;
        CHECK_ERRNO(client_sfd = accept(listen_sfd, (struct sockaddr *)&client_sa, &client_sa_len));

        pthread_create(&thread, NULL, handle_client, &(struct hc_t){client_sfd, client_sa, root_dir}); // FIXME: se creo il thread come detached, funziona solo se la root dir è "/"
        pthread_detach(thread);
    }
}

void sig_int_handler(int sig)
{
    UNUSED(sig);
    printf("\nExiting\n");
    exit(EXIT_SUCCESS);
}

void print_interface_ip(in_port_t port) //https://man7.org/linux/man-pages/man3/getifaddrs.3.html
{
    struct ifaddrs *ifaddr, *ifa;
    char *host = malloc(NI_MAXHOST * sizeof(*host));

    CHECK_ERRNO(getifaddrs(&ifaddr));

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;

        if ((ifa->ifa_flags & IFF_UP) && (ifa->ifa_addr->sa_family == AF_INET))
        {
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0)
                printf("%-10s\t%s:%d\n", ifa->ifa_name, host, port);
        }
    }

    printf("\n");

    free(host);
    freeifaddrs(ifaddr);
}

