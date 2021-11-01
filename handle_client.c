#define _DEFAULT_SOURCE // for DT_DIR

#include "handle_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>

#include "macro.h"

struct HTTPRequest *parse_request(int client_sfd);
void freeHTTPRequest(struct HTTPRequest *request);
void http_path_decode(char *path);
void freeHTTPResponse(struct HTTPResponse *response);
struct HTTPResponse *http_get(int client_sfd, struct HTTPRequest *request);
char *absolute_path(char *root_path, char *path);
char *get_timestamp(void);
char *generate_index_html(const char *path, size_t *file_size);
void sock_write(int fd, const void *buf, size_t count); // writes until finished
bool path_sanitize(char *path);

void *handle_client(void *hc)
{
    int client_sfd = ((struct hc_t *)hc)->client_sfd;
    struct sockaddr_in client_sa = ((struct hc_t *)hc)->client_sa;
    char *root_path = ((struct hc_t *)hc)->root_path;

    // get ip and port of client
    char client_ip[INET_ADDRSTRLEN];
    unsigned short client_port = ntohs(client_sa.sin_port);
    CHECK_ERRNO(inet_ntop(AF_INET, (struct sockaddr *)&(client_sa.sin_addr), client_ip, sizeof(client_ip)));

    // parse request
    struct HTTPRequest *request = parse_request(client_sfd);
    if (request == NULL)
    {
        CHECK_ERRNO(freeHTTPRequest(request));
        return NULL;
    }

    request->path = absolute_path(root_path, request->path);

    // send response
    struct HTTPResponse *response;
    switch (request->method)
    {
    case GET:
        response = http_get(client_sfd, request);
        break;

    default: /*http_bad(clinet_sfd)*/
        printf("into default\n");
    }

    //printf("User-Agent: %s\n", request->user_agent);
    char *timestamp = get_timestamp();
    char file_type[STRING_LEN] = "\0";

    if (!response->dir)
        sprintf(file_type, "file [%.1016s]", response->content_type);

    switch (response->status_code)
    {
    case 200:
        printf("[%s] | %s:%d | sending %s [%s] %ld bytes\n", timestamp, client_ip, client_port,
               response->dir ? "dir" : file_type, request->path, response->file_size);
        break;
    case 403:
        printf("[%s] | %s:%d | %s [%s] 403 Permission Denied\n", timestamp, client_ip, client_port,
               response->dir ? "dir" : "file", request->path);
        break;
    case 404:
        printf("[%s] | %s:%d | %s [%s] 404 File Not Found\n", timestamp, client_ip, client_port,
               response->dir ? "dir" : "file", request->path);
        break;

    default:
        printf("[%s] | %s:%d | status code not known %d\n", timestamp, client_ip, client_port,
               response->status_code);
    }

    CHECK_ERRNO(free(timestamp));
    freeHTTPRequest(request);
    freeHTTPResponse(response);

    return NULL;
}

struct HTTPRequest *parse_request(int client_sfd)
{
    struct HTTPRequest *request = calloc(1, sizeof(*request));
    char mbuf[BUFFER_LEN];
    char *buf = mbuf;
    size_t len;

    do
    {
        CHECK_ERRNO(len = read(client_sfd, buf, BUFFER_LEN - 1));
    } while (errno == EINTR);

    if (len == 0)
    {
        CHECK_ERRNO(free(request));
        return NULL;
    }

    buf[len] = '\0';

    // method
    if (!strncmp(buf, "GET", 3))
        request->method = GET;

    char piece[STRING_LEN];
    CHECK_ERRNO(sscanf(buf, "%*s %s", piece));

    request->path = malloc((strlen(piece) + 1) * sizeof(*request->path));
    strcpy(request->path, piece);

    buf = strchr(buf, '\n');
    while ((buf = strchr(buf, '\n')))
    {
        CHECK_ERRNO(sscanf(buf, "%s", piece));
        if (!strcmp(piece, "User-Agent:"))
        {
            CHECK_ERRNO(sscanf(buf, "%*s %[^\n]", piece));
            request->user_agent = malloc((strlen(piece) + 1) * sizeof(*request->user_agent));
            strcpy(request->user_agent, piece);
        }
        // aggiungere qui if (!strcmp(piece, "CAMPO:")) e poi leggerlo

        buf = buf ? buf + 1 : NULL;
    }

    return request;
}

void freeHTTPRequest(struct HTTPRequest *request)
{
    CHECK_ERRNO(free(request->path));
    CHECK_ERRNO(free(request->user_agent));
    CHECK_ERRNO(free(request));

    request = NULL;
}

char *absolute_path(char *root_path, char *path) // use as: path = absolute_path(root_path, path)
{
    char *ret = malloc(sizeof(*ret) * (strlen(root_path) + strlen(path) + 1 + 2)); // '\0' + 2'/'

    http_path_decode(path); // replace "%20" with "\ " and possibly others

    strcpy(ret, root_path);
    if (ret[strlen(ret) - 1] == '/')
        ret[strlen(ret) - 1] = '\0';

    strcat(ret, path);
    CHECK_ERRNO(free(path));

    return ret;
}

void http_path_decode(char *path)
{
    char sub = '\0';

    while (*path)
    {
        if (*path == '%')
        {
            if (*(path + 1) == '2')
            {
                switch (*(path + 2))
                {
                case '0':
                    sub = ' ';
                    break;
                }
            }
        }

        if (sub != '\0')
        {
            if (sub == ' ')
            {
                *path = ' ';

                // manual strcpy because of overlapping buffers
                char *ptr;
                for (ptr = path + 1; *(ptr + 2) != '\0'; ++ptr)
                    *ptr = *(ptr + 2);
                *ptr = '\0';
            }
        }

        sub = '\0';
        ++path;
    }
}

struct HTTPResponse *http_get(int client_sfd, struct HTTPRequest *request)
{
    struct HTTPResponse *response = calloc(1, sizeof(*response));
    struct stat file_info;
    FILE *client_sfd_s;
    char *payload = NULL;

    int stat_errno;
    errno = 0;
    CHECK_ERRNO(stat(request->path, &file_info));
    stat_errno = errno;
    CHECK_ERRNO(client_sfd_s = fdopen(dup(client_sfd), "w"));

    int access_errno;
    errno = 0; // in release mode errno non è settato a 0 prima della chiamata
    CHECK_ERRNO(access(request->path, R_OK));
    access_errno = errno;

    if (stat_errno == ENOENT)
    {
        char msg[STRING_LEN];
        sprintf(msg, "<h1>404 File Not Found</h1>\r\n"
                     "<b>\"%s\"</b> was not found\r\n\r\n",
                request->path);

        fprintf(client_sfd_s, "HTTP/1.0 404 Not Found\r\n");
        fprintf(client_sfd_s, "Content-Length: %zu\r\n", strlen(msg));
        fprintf(client_sfd_s, "Content-Type: text/html\r\n\r\n");
        fprintf(client_sfd_s, "%s", msg);

        response->status_code = 404;
    }
    else if (access_errno == EACCES)
    {

        char msg[STRING_LEN];
        sprintf(msg, "<h1>403 Permission denied</h1>\r\n"
                     "<b>\"%s\"</b> couldn't be retrieved\r\n\r\n",
                request->path);

        fprintf(client_sfd_s, "HTTP/1.0 403 Permission denied\r\n");
        fprintf(client_sfd_s, "Content-Length: %zu\r\n", strlen(msg));
        fprintf(client_sfd_s, "Content-Type: text/html\r\n\r\n");
        fprintf(client_sfd_s, "%s", msg);

        response->status_code = 403;
    }
    else
    {
        char content_type[STRING_LEN] = {'\0'};
        size_t payload_size;

        if (S_ISDIR(file_info.st_mode)) // directory
        {
            response->dir = true;

            payload = generate_index_html(request->path, &payload_size);
            if (payload == NULL)
            {
                //fprintf(client_sfd_s, "HTTP/1.1 200 OK\r\n");
                //fprintf(client_sfd_s, "Content-Type: text/html\r\n\r\n");
                //fprintf(client_sfd_s, "<h1>Can't generate directory index</h1>\r\n");
                //fprintf(client_sfd_s, "<b>\"%s\"</b> fail\r\n\r\n", request->path);
                fprintf(stderr, "generate_index_html ha ritornato payload = NULL\n\n");
            }
            else
            {
                strcpy(content_type, "text/html");
            }
        }
        else
        {
            FILE *payload_s, *pipe_file_cmd;
            CHECK_ERRNO(payload_s = fopen(request->path, "rb"));
            CHECK_ERRNO(payload = malloc(file_info.st_size * sizeof(*payload)));
            CHECK_ERRNO(fread(payload, sizeof(char), file_info.st_size, payload_s));
            CHECK_ERRNO(fclose(payload_s));

            char cmd[STRING_LEN];
            sprintf(cmd, "file %s --mime-type | cut -d \' \' -f 2", request->path);
            CHECK_ERRNO(pipe_file_cmd = popen(cmd, "r"));
            fscanf(pipe_file_cmd, "%s", content_type);
            CHECK_ERRNO(pclose(pipe_file_cmd));

            payload_size = file_info.st_size;
            response->dir = false;
        }

        response->status_code = 200;
        response->file_size = payload_size;
        strcpy(response->content_type, content_type);

        fprintf(client_sfd_s, "HTTP/1.0 200 OK\r\n");
        //fprintf(client_sfd_s, "Server: httpserver6969\r\n");
        fprintf(client_sfd_s, "Content-Length: %zu\r\n", payload_size);
        fprintf(client_sfd_s, "Content-Type: %s\r\n\r\n", content_type);
        fwrite(payload, sizeof(char), payload_size, client_sfd_s);
    }

    CHECK_ERRNO(fclose(client_sfd_s));
    if (payload != NULL)
        CHECK_ERRNO(free(payload));

    return response;
}

void freeHTTPResponse(struct HTTPResponse *response)
{
    CHECK_ERRNO(free(response));
}

char *get_timestamp(void)
{
    time_t now;
    struct tm *ts;
    char *buf;
    CHECK_ERRNO(buf = malloc(sizeof(*buf) * 20));

    time(&now);
    ts = localtime(&now);
    strftime(buf, 20, "%F %T", ts);

    return buf;
}

void sock_write(int fd, const void *buf, size_t count)
{
    ssize_t bytes_sent = 0;

    do
    {
        CHECK_ERRNO(bytes_sent += write(fd, buf + bytes_sent, count - bytes_sent));
        if (errno == EINTR)
            bytes_sent = 0;

    } while ((size_t)bytes_sent < count);
}

bool path_sanitize(char *path) // TODO: farla bene
{
    return strstr(path, "..") != NULL;
}

char *generate_index_html(const char *path, size_t *file_size)
{
    DIR *directory_path;
    struct dirent *dir;

    CHECK_ERRNO(directory_path = opendir(path));

    if (!directory_path)
        return NULL;

    char file[STRING_LEN];
    size_t buffer_len = BUFFER_LEN;
    char *buffer;
    CHECK_ERRNO(buffer = malloc(buffer_len * sizeof(*buffer)));
    char *buf = buffer;
    int bytes_buf;
    bytes_buf = sprintf(buf, "<html>\n<head><title>%s</title></head>\n<body>\n"
                             "<center><h1>%s</h1></center><br/>\n",
                        path, path);
    buf += bytes_buf;

    CHECK_ERRNO(dir = readdir(directory_path));
    while (dir != NULL)
    {
        strcpy(file, dir->d_name);

        if (buf - buffer > (long int)buffer_len - STRING_LEN) // FIXME: STRING_LEN arbitrario, se una path di un file è più lunga ho problemi
        {
            buffer_len *= 2;
            size_t offset = buf - buffer;
            CHECK_ERRNO(buffer = realloc(buffer, buffer_len));
            buf = buffer + offset;
        }

        if (dir->d_type == DT_DIR) // if is a directory
            strcat(file, "/");

        bytes_buf = sprintf(buf, "<a href=\"%s\">%s</a><br/>\n", file, file);
        buf += bytes_buf;

        CHECK_ERRNO(dir = readdir(directory_path));
    }

    sprintf(buf, "</body></html>");

    CHECK_ERRNO(closedir(directory_path));

    *file_size = strlen(buffer);
    return buffer;
}
