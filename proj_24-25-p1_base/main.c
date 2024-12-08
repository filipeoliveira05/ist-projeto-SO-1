#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>

#include "constants.h"
#include "parser.h"
#include "operations.h"

#define MAX_FILES 100
#define MAX_PATH_LENGTH 4096

// Lista ficheiros com extensão .job na diretoria e retorna o número de ficheiros
void list_job_files(const char *dir_path, char files[][MAX_PATH_LENGTH], size_t *num_files) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("Failed to open directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    *num_files = 0;

    // Adiciona ficheiros .job à lista
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".job")) {
            snprintf(files[*num_files], MAX_PATH_LENGTH, "%s/%s", dir_path, entry->d_name);
            (*num_files)++;
        }
    }

    closedir(dir);

    // Ordena os ficheiros .job pela ordem alfabética
    qsort(files, *num_files, sizeof(files[0]), (int(*)(const void *, const void *))strcmp);
}

// Processa comandos de um ficheiro .job e gera um ficheiro .out
void process_job_file(const char *input_file) {
    int fd_input = open(input_file, O_RDONLY);
    if (fd_input == -1) {
        perror("Failed to open input file");
        return;
    }

    // Gerar o nome do ficheiro de saída, remover a extensão ".job" e adicionar ".out"
    char output_file[MAX_PATH_LENGTH];
    snprintf(output_file, MAX_PATH_LENGTH, "%.*s.out", (int)(strlen(input_file) - 4), input_file);  // ".job" tem 4 caracteres

    int fd_output = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_output == -1) {
        perror("Failed to open output file");
        close(fd_input);
        return;
    }

    // Fechar stdout e redirecionar a saída para o arquivo de saída
    close(STDOUT_FILENO);
    if (dup(fd_output) == -1) {  // Redireciona a saída para o novo arquivo
        perror("Failed to redirect stdout to output file");
        close(fd_input);
        close(fd_output);
        return;
    }

    // Processar comandos no ficheiro .job
    while (1) {
        char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
        char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
        unsigned int delay;
        size_t num_pairs;

        enum Command cmd = get_next(fd_input);

        switch (cmd) {
            case CMD_WRITE:
                num_pairs = parse_write(fd_input, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
                if (num_pairs == 0) {
                    dprintf(STDOUT_FILENO, "Invalid command. See HELP for usage\n");
                    continue;
                }
                if (kvs_write(num_pairs, keys, values)) {
                    dprintf(STDOUT_FILENO, "Failed to write pair\n");
                }
                break;

            case CMD_READ:
                num_pairs = parse_read_delete(fd_input, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
                if (num_pairs == 0) {
                    dprintf(STDOUT_FILENO, "Invalid command. See HELP for usage\n");
                    continue;
                }
                if (kvs_read(num_pairs, keys)) {
                    dprintf(STDOUT_FILENO, "Failed to read pair\n");
                }
                break;

            case CMD_DELETE:
                num_pairs = parse_read_delete(fd_input, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
                if (num_pairs == 0) {
                    dprintf(STDOUT_FILENO, "Invalid command. See HELP for usage\n");
                    continue;
                }
                if (kvs_delete(num_pairs, keys)) {
                    dprintf(STDOUT_FILENO, "Failed to delete pair\n");
                }
                break;

            case CMD_SHOW:
                kvs_show(); // Aqui você pode redirecionar a saída de kvs_show() para o arquivo
                break;

            case CMD_WAIT:
                if (parse_wait(fd_input, &delay, NULL) == -1) {
                    dprintf(STDOUT_FILENO, "Invalid command. See HELP for usage\n");
                    continue;
                }
                if (delay > 0) {
                    kvs_wait(delay);
                }
                break;

            case CMD_BACKUP:
                if (kvs_backup()) {
                    dprintf(STDOUT_FILENO, "Failed to perform backup.\n");
                }
                break;

            case CMD_INVALID:
                dprintf(STDOUT_FILENO, "Invalid command. See HELP for usage\n");
                break;

            case CMD_HELP:
                dprintf(STDOUT_FILENO,
                        "Available commands:\n"
                        "  WRITE [(key,value)(key2,value2),...]\n"
                        "  READ [key,key2,...]\n"
                        "  DELETE [key,key2,...]\n"
                        "  SHOW\n"
                        "  WAIT <delay_ms>\n"
                        "  BACKUP\n"
                        "  HELP\n");
                break;

            case CMD_EMPTY:
                break;

            case EOC:
                close(fd_input);
                close(fd_output);
                return;
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory_path> <max_concurrent_backups>\n", argv[0]);
        return 1;
    }

    const char *dir_path = argv[1];

    if (kvs_init()) {
        fprintf(stderr, "Failed to initialize KVS\n");
        return 1;
    }

    // Listar os ficheiros .job na diretoria
    char job_files[MAX_FILES][MAX_PATH_LENGTH];
    size_t num_files = 0;
    list_job_files(dir_path, job_files, &num_files);

    // Processar cada ficheiro .job
    for (size_t i = 0; i < num_files; i++) {
        process_job_file(job_files[i]);
    }

    kvs_terminate();
    return 0;
}