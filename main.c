#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define return_defer(value)                                                                        \
    do {                                                                                           \
        result = (value);                                                                          \
        goto defer;                                                                                \
    } while (0)

#define DA_INIT_CAP 128

#define da_append(l, v)                                                                            \
    do {                                                                                           \
        if ((l)->count >= (l)->capacity) {                                                         \
            (l)->capacity = (l)->capacity == 0 ? DA_INIT_CAP : (l)->capacity * 2;                  \
            (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                    \
            assert((l)->data);                                                                     \
        }                                                                                          \
                                                                                                   \
        (l)->data[(l)->count++] = (v);                                                             \
    } while (0)

#define da_append_many(l, v, c)                                                                    \
    do {                                                                                           \
        if ((l)->count + (c) > (l)->capacity) {                                                    \
            if ((l)->capacity == 0) {                                                              \
                (l)->capacity = DA_INIT_CAP;                                                       \
            }                                                                                      \
                                                                                                   \
            while ((l)->count + (c) > (l)->capacity) {                                             \
                (l)->capacity *= 2;                                                                \
            }                                                                                      \
                                                                                                   \
            (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                    \
            assert((l)->data);                                                                     \
        }                                                                                          \
                                                                                                   \
        if ((v) != NULL) {                                                                         \
            memcpy((l)->data + (l)->count, (v), (c) * sizeof(*(l)->data));                         \
            (l)->count += (c);                                                                     \
        }                                                                                          \
    } while (0)

typedef struct {
    char *data;
    size_t count;
    size_t capacity;
} Buffer;

int is_directory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) < 0) {
        return 0;
    }

    return S_ISDIR(statbuf.st_mode);
}

int capture_process(char **args, Buffer *output) {
    int capture[2];
    if (pipe(capture) < 0) {
        fprintf(stderr, "Error: could not create pipe\n");
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: could not fork process\n");
        return 0;
    }

    if (pid == 0) {
        close(capture[0]);
        dup2(capture[1], STDOUT_FILENO);
        close(capture[1]);

        int silence = open("/dev/null", O_WRONLY);
        if (silence < 0) {
            fprintf(stderr, "Error: could not silence output\n");
            exit(1);
        }
        dup2(silence, STDERR_FILENO);
        close(silence);

        execvp(*args, args);

        fprintf(stderr, "Error: could not execute process\n");
        exit(1);
    }
    close(capture[1]);

    while (1) {
        da_append_many(output, NULL, 1024);

        long count = read(capture[0], output->data + output->count, 1024);
        if (count < 0) {
            fprintf(stderr, "Error: could not read process output\n");
            return 0;
        }

        if (count == 0) {
            break;
        }

        output->count += count;
    }
    close(capture[0]);

    if (output->count && output->data[output->count - 1] == '\n') {
        output->count--;
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "Error: could not wait for process\n");
        return 0;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int resolve_packages(char **argv, size_t argc, char *pflag, char *cflag, char *dir) {
    int result = 1;

    Buffer buffer = {0};
    const char *base = getenv("CPRPATH");

    for (size_t i = 0; i < argc; i++) {
        int found = 1;
        size_t head = buffer.count;

        char *pkg = argv[i];
        char *args[] = {"pkg-config", pflag, pkg, NULL};

        if (!capture_process(args, &buffer)) {
            buffer.count = head;

            if (base) {
                da_append_many(&buffer, cflag, strlen(cflag));
                da_append_many(&buffer, base, strlen(base));
                da_append(&buffer, '/');
                da_append_many(&buffer, pkg, strlen(pkg));
                da_append(&buffer, '/');
                da_append_many(&buffer, dir, strlen(dir));
                da_append(&buffer, '\0');

                if (!is_directory(buffer.data + head + strlen(cflag))) {
                    found = 0;
                }

                buffer.count--;
            }
        }

        if (found) {
            if (buffer.count && buffer.data[buffer.count - 1] != ' ') {
                da_append(&buffer, ' ');
            }
        } else {
            buffer.count = head;

            fprintf(stderr, "Error: package '%s' not found\n", pkg);
            if (!base) {
                fprintf(stderr, "Note: set up the 'CPRPATH' variable to access local packages\n");
            }

            return_defer(0);
        }
    }

    if (buffer.count) {
        fwrite(buffer.data, buffer.count, 1, stdout);
        putc('\n', stdout);
    }

defer:
    free(buffer.data);
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Error: command not found\n");
        fprintf(stderr, "Usage: cpr <flags|libs> [...PKGS]\n");
        exit(1);
    }

    const char *command = argv[1];
    if (!strcmp(command, "flags")) {
        if (!resolve_packages(argv + 2, argc - 2, "--cflags", "-I", "include")) {
            exit(1);
        }
    } else if (!strcmp(command, "libs")) {
        if (!resolve_packages(argv + 2, argc - 2, "--libs", "-L", "lib")) {
            exit(1);
        }
    } else {
        fprintf(stderr, "Error: invalid command '%s'\n", command);
        fprintf(stderr, "Usage: cpr <flags|libs> [...PKGS]\n");
        exit(1);
    }
}
