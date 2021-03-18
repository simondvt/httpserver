#pragma once

#include <netinet/in.h>
#include <stdbool.h>

struct hc_t
{
    int client_sfd;
    struct sockaddr_in client_sa;
    char *root_path;
};

enum Method
{
    GET
};

struct HTTPRequest
{
    char *path;
    char *user_agent;
    enum Method method;
};

struct HTTPResponse
{
    int status_code;
    size_t file_size;
    bool dir;
};

void* handle_client(void *hc);
