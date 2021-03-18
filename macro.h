#pragma once

#define S(x) #x
#define S_(x) S(x)
#define S__LINE__ S_(__LINE__)
#ifdef DEBUG
#define CHECK_ERRNO(INSTR)                                              \
    do                                                                  \
    {                                                                   \
        errno = 0;                                                      \
        INSTR;                                                          \
        if (errno != 0)                                                 \
            perror("Error in " __FILE__ ":" S__LINE__ " [" #INSTR "]"); \
    } while (0)
#else
#define CHECK_ERRNO(INSTR) INSTR
#endif

#define UNUSED(param) (void)param

#define LISTEN_BACKLOG 5
#define STRING_LEN 1024
#define BUFFER_LEN 4096
