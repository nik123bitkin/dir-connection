/*
 Написать программу синхронизации двух каталогов, например, Dir1 и Dir2.
 Пользователь задаёт имена Dir1 и Dir2. В результате работы программы файлы, имеющиеся в Dir1, но отсутствующие в Dir2,
 должны скопироваться в Dir2 вместе с правами доступа.
 Процедуры копирования  должны запускаться в отдельном процессе для каждого копируемого файла.
 Головной процесс создает  неименованные каналы для связи с дочерними  процессами и передает в канал путь, имя файл;
 считывает из канала и выводит на консоль результаты выполнения копирования.
 Дочерние  процессы записывают в канал  свой  pid, полный путь к копируемому файлу  и число скопированных байт.
 Число одновременно работающих процессов не должно превышать N (вводится пользователем).
 Скопировать несколько файлов из каталога /etc в свой домашний каталог.
 Проверить работу программы для каталога /etc и домашнего каталога.
 */

#include <stdbool.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <wait.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <memory.h>

#define VALID_ARGS 3
#define BLOCK_SIZE 1048576

#define MISSING_ARG "Missing argument\n"
#define DIR_ERR "Directory not found/ not a directory\n"
#define PROC_ERR "Number of max processes is not an integer\n"
#define PROC_COUNT_ERR "Number of max processes must be greater than 1"

#define STAT_ERR "Unable to get file info"
#define MEM_ERR "Memory allocation error"
#define OPEN_DIR_ERR "Unable to open dir"
#define PID_CREATE_ERR "Unable to create process\n"
#define SRC_OPEN_ERR "Unable to open source file"
#define SRC_CLOSE_ERR "Unable to close source file"
#define DEST_OPEN_ERR "Unable to open dest file"
#define DEST_CLOSE_ERR "Unable to close dest file"
#define READ_ERR "Read error"
#define WRITE_ERR "Write error"
#define PIPE_ERR "Unable to create pipe"


#define EXIT_ERR(M)     do{ throwError(M); return 1;} while(0)
#define string            char*
#define const_string      const char*


typedef struct lnode {
    string filename;
    struct lnode *next;
} lnode;

typedef struct copy_result {
    long copied;
    pid_t pid;
    char* path;
} copy_result;
long max_processes;

void throwError(const_string msg);

bool validateDir(const_string path);

void compareDirs(const_string source, const_string dest);

lnode *getUniqueList(const_string source, const_string dest);

lnode *putNode(const_string filename);

int fill_list(const_string dirpath, lnode *files, lnode *compare_list);

int fill_set(int column, int mode, fd_set *set, int pipes[2][max_processes][2]);

bool in_list(string filepath, lnode *files);

void free_list(lnode *files);

long fcopy(const_string source_path, const_string dest_fld_path);

string getFullPath(const_string path, const_string name);

pid_t tryFork();

string PROG_NAME;
int proc_counter = 1;


int main(int argc, string argv[], string envp[]) {
    PROG_NAME = basename(argv[0]);

    if (argc < VALID_ARGS)
        EXIT_ERR(MISSING_ARG);

    if (!validateDir(argv[1]) || !(validateDir(argv[2])))
        EXIT_ERR(DIR_ERR);

    if (!(max_processes = strtol(argv[3], NULL, 10)))
        EXIT_ERR(PROC_ERR);

    if (max_processes < 2) {
        EXIT_ERR(PROC_COUNT_ERR);
    }

    compareDirs(argv[1], argv[2]);

    return 0;
}

inline void throwError(const_string msg) {
    fprintf(stderr, "%d: %s %s\n", getpid(), PROG_NAME,  msg);
}

inline bool validateDir(const_string path) {
    struct stat statbuf;
    return stat(path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode);
}

string getFullPath(const_string path, const_string name) {
    size_t pathLength = sizeof(char) * (strlen(path) + strlen(name) + 2);
    string fullPath = malloc(pathLength);
    if (fullPath) {
        strcpy(fullPath, path);
        strcpy(fullPath + strlen(path), "/");
        strcpy(fullPath + strlen(path) + 1, name);
    }
    return fullPath;
}

pid_t tryFork() {
    if (proc_counter == max_processes) {
        wait(NULL);
        proc_counter--;
    }
    bool success = false;
    pid_t pid = -1;
    while (!success) {
        pid = fork();
        if (pid == -1) {
            throwError(PID_CREATE_ERR);
            continue;
        }
        success = !success;
    }
    proc_counter++;
    return pid;
}

lnode *getUniqueList(const_string source, const_string dest) {
    lnode *src_files = putNode(NULL), *dest_files = putNode(NULL);
    if (!src_files || !dest_files) {
        throwError(MEM_ERR);
        return NULL;
    }

    if (fill_list(dest, dest_files, NULL) == -1) {
        return NULL;
    }

    if (fill_list(source, src_files, dest_files) == -1) {
        return NULL;
    }

    free_list(dest_files);
    return src_files->next ? src_files : NULL;
}

bool in_list(string filepath, lnode *files) {
    lnode *this = files;
    while (this->next) {
        this = this->next;
        if (!strcmp(basename(filepath), basename(this->filename))) {
            return true;
        }
    }
    return false;
}

int fill_list(const_string dirpath, lnode *files, lnode *compare_list) {
    DIR *dir;
    if (dir = opendir(dirpath), !dir) {
        fprintf(stderr, "%s: %d %s %s\n", PROG_NAME, getpid(), OPEN_DIR_ERR, dirpath);
        return 1;
    }

    lnode *temp_files = files;

    struct dirent *current_file;
    while ((current_file = readdir(dir))) {
        if (strcmp(current_file->d_name, ".") && strcmp(current_file->d_name, "..") && current_file->d_type != DT_UNKNOWN) {
            if (current_file->d_type == DT_DIR) {
                string name = getFullPath(dirpath, current_file->d_name);
                fill_list(name, temp_files, compare_list);
                while(temp_files->next)
                    temp_files = temp_files->next;
                free(name);
            } else if (current_file->d_type == DT_REG) {
                string name = getFullPath(dirpath, current_file->d_name);
                if (!name) {
                    closedir(dir);
                    EXIT_ERR(MEM_ERR);
                }
                struct stat statbuf;
                if (stat(name, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
                    if (compare_list) {
                        if (!in_list(name, compare_list)) {
                            temp_files->next = putNode(name);
                            temp_files = temp_files->next;
                        }
                    } else {
                        temp_files->next = putNode(name);
                        temp_files = temp_files->next;
                    }
                }
                free(name);
            }
        }
    }

    closedir(dir);
    return 0;
}

lnode *putNode(const_string filename) {
    lnode *this = (lnode *) malloc(sizeof(lnode));
    if (!this) {
        throwError(MEM_ERR);
        return NULL;
    }
    if (filename) {
        this->filename = malloc(strlen(filename) + 1);
        strcpy(this->filename, filename);
    }
    this->next = NULL;
    return this;
}

void free_list(lnode *files) {
    lnode *this;
    while ((this = files) != NULL) {
        files = files->next;
        free(this);
    }
}

long fcopy(const_string source_path, const_string dest_fld_path) {

    struct stat statbuf;

    if (lstat(source_path, &statbuf) == -1) {
        fprintf(stderr, "%d: %s %s %s\n", getpid(), PROG_NAME, STAT_ERR, source_path);
        return -1;
    }

    int source_fd;
    if ((source_fd = open(source_path, O_RDONLY)) == -1) {
        fprintf(stderr, "%d: %s %s %s\n", getpid(), PROG_NAME, SRC_OPEN_ERR, source_path);
        return -1;
    }

    string name = basename(source_path);

    string dest_path = getFullPath(dest_fld_path, name);

    if (!dest_path) {
        throwError(MEM_ERR);
        return -1;
    }

    int dest_fd;
    if ((dest_fd = creat(dest_path, statbuf.st_mode)) == -1) {
        fprintf(stderr, "%d: %s %s %s\n", getpid(), PROG_NAME, DEST_OPEN_ERR, dest_path);
        if (close(source_fd) == -1) {
            fprintf(stderr, "%d: %s %s %s\n", getpid(), PROG_NAME, SRC_CLOSE_ERR, source_path);
        }
        return -1;
    }

    long total = 0, received, written, is_error = 0;
    char buf[BLOCK_SIZE];

    while (!is_error && (received = read(source_fd, buf, BLOCK_SIZE))) {
        char *buf_pos_pointer = buf;
        if (received != -1) {
            do {
                written = write(dest_fd, buf_pos_pointer, received);
                if (written) {
                    if (written != -1) {
                        received -= written;
                        buf_pos_pointer += written;
                        total += written;
                    } else if (errno != EINTR) {
                        throwError(READ_ERR);
                        is_error = true;
                    }
                }
            } while (!is_error && received);
        } else if (errno != EINTR) {
            throwError(WRITE_ERR);
            is_error = true;
        }
    }

    if (close(source_fd) == -1) {
        fprintf(stderr, "%s: %s %s %d\n", PROG_NAME, SRC_CLOSE_ERR, source_path, getpid());
    }

    if (close(dest_fd) == -1) {
        fprintf(stderr, "%s: %s %s %d\n", PROG_NAME, DEST_CLOSE_ERR, dest_path, getpid());
    }

    return total;
}

int fill_set(int column, int mode, fd_set *set, int pipes[2][max_processes][2]) {
    int max_fd_value = -1;
    FD_ZERO(set);
    for (int i = 0; i < max_processes; i++) {
        if (pipes[column][i][mode] != -1) {
            if (pipes[column][i][mode] > max_fd_value) {
                max_fd_value = pipes[column][i][0];
            }
            FD_SET(pipes[column][i][mode], set);
        }
    }
    return max_fd_value;
}

void compareDirs(const_string source, const_string dest) {
    lnode *files = getUniqueList(source, dest);
    if (files == NULL) {
        return;
    }

    int pipes[2][max_processes][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < max_processes; j++)
            if (pipe(pipes[i][j]) == -1) {
                throwError(PIPE_ERR);
                goto clear_resources;
            }

    lnode *temp = files->next;
    pid_t pid;

    int curr_pipe = 0, pipe_number, max_fd;
    fd_set rfds;

    while (temp) {
        if (curr_pipe < max_processes - 1) {
            pipe_number = curr_pipe;
            curr_pipe++;
        } else {
            max_fd = fill_set(1, 0, &rfds, pipes);
            int ret;
            if ((ret = select(max_fd + 1, &rfds, NULL, NULL, NULL)) == -1) {
                throwError("Unexpected select error");
                break;
            }
            copy_result *result = malloc(sizeof(copy_result));
            for (int i = 0; i < max_processes; i++) {
                if (FD_ISSET(pipes[1][i][0], &rfds)) {
                    long received = read(pipes[1][i][0], result, sizeof(copy_result));
                    if (received == -1) {
                        throwError(READ_ERR);
                        goto clear_resources;
                    }
                    printf("%d %ld %s\n", result->pid, result->copied, result->path);
                    pipe_number = i;
                    break;
                }
            }
        }

        long written = write(pipes[0][pipe_number][1], temp, sizeof(lnode));
        if (written == -1) {
            throwError(WRITE_ERR);
            goto clear_resources;
        }

        pid = tryFork(pipes);
        if (!pid) {
            lnode *file_info = malloc(sizeof(lnode));
            long received = read(pipes[0][pipe_number][0], file_info, sizeof(lnode));
            if (received == -1) {
                throwError(READ_ERR);
                exit(1);
            }
            //printf("Process %d get %s\n", getpid(), file_info->filename);
            copy_result *result = malloc(sizeof(copy_result));
            result->path = malloc(sizeof(char*));
            result->path = file_info->filename;

            result->copied = fcopy(file_info->filename, dest);
            result->pid = getpid();

            long _written = write(pipes[1][pipe_number][1], result, sizeof(copy_result));
            if (_written == -1) {
                throwError(WRITE_ERR);
                exit(1);
            }
            //result = fcopy(temp->filename, dest);
            exit(0);

        } else if (pid > 0) {
            temp = temp->next;
        }
    }

    clear_resources:

    while (wait(NULL) != -1);

    if(files) {
        if (curr_pipe < max_processes - 1) {
            max_fd = fill_set(1, 0, &rfds, pipes);
            int ret;
            if ((ret = select(max_fd + 1, &rfds, NULL, NULL, NULL)) == -1) {
                throwError("Unexpected select error");
                goto clear_resources;
            }
            copy_result *result = malloc(sizeof(copy_result));
            for (int i = 0; i < max_processes; i++) {
                if (FD_ISSET(pipes[1][i][0], &rfds)) {
                    long received = read(pipes[1][i][0], result, sizeof(copy_result));
                    if (received == -1) {
                        throwError(READ_ERR);
                        goto clear_resources;
                    }
                    printf("%d %ld %s\n", result->pid, result->copied, result->path);
                }
            }
        }
    }

    free_list(files);
}
