#pragma once

#include <netinet/in.h>
#include <stdbool.h>

#include "macro.h"

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
    char content_type[STRING_LEN];
};

void *handle_client(void *hc);
