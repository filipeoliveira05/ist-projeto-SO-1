#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>

#include "constants.h"
#include "parser.h"
#include "operations.h"

#define MAX_PATH_LENGTH 4096

// Lista ficheiros com extensão .job na diretoria e retorna o número de ficheiros
void list_job_files(const char *dir_path, char ***files, size_t *num_files)
{
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        perror("Failed to open directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    *num_files = 0;

    // Conta o número de ficheiros .job
    while ((entry = readdir(dir)) != NULL)
    {
        if (strstr(entry->d_name, ".job"))
        {
            (*num_files)++;
        }
    }

    // Aloca memória para armazenar os caminhos dos arquivos
    *files = (char **)malloc(sizeof(char *) * (*num_files));
    if (*files == NULL)
    {
        perror("Failed to allocate memory for files");
        exit(EXIT_FAILURE);
    }

    // Redefine o diretório e adiciona os ficheiros .job à lista
    rewinddir(dir);
    size_t index = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strstr(entry->d_name, ".job"))
        {
            (*files)[index] = (char *)malloc(MAX_PATH_LENGTH * sizeof(char));
            snprintf((*files)[index], MAX_PATH_LENGTH, "%s/%s", dir_path, entry->d_name);
            index++;
        }
    }

    closedir(dir);

    // Ordena os ficheiros .job pela ordem alfabética
    qsort(*files, *num_files, sizeof((*files)[0]), (int (*)(const void *, const void *))strcmp);
}

// Processa comandos de um ficheiro .job e gera um ficheiro .out
void process_job_file(const char *input_file, pid_t *active_backups, int active_count, int max_concurrent_backups)
{
    int fd_input = open(input_file, O_RDONLY);
    if (fd_input == -1)
    {
        perror("Failed to open input file");
        return;
    }

    // Gerar o nome do ficheiro de saída, removendo ".job" e adicionando ".out"
    char output_file[MAX_PATH_LENGTH];
    snprintf(output_file, MAX_PATH_LENGTH, "%.*s.out", (int)(strlen(input_file) - 4), input_file);

    int fd_output = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_output == -1)
    {
        perror("Failed to open output file");
        close(fd_input);
        return;
    }

    // Processar comandos no ficheiro .job
    while (1)
    {
        char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
        char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
        unsigned int delay;
        size_t num_pairs;

        enum Command cmd = get_next(fd_input);

        switch (cmd)
        {
        case CMD_WRITE:
            num_pairs = parse_write(fd_input, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
            if (num_pairs == 0)
            {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }

            int saved_stdout_write;
            if (redirect_output(fd_output, &saved_stdout_write) == -1)
            {
                perror("Failed to redirect stdout");
                break;
            }

            sort_key_value_pairs(keys, values, num_pairs);
            if (kvs_write(num_pairs, keys, values))
            {
                fprintf(stderr, "Failed to write pair\n");
            }

            restore_output(saved_stdout_write);
            break;

        case CMD_READ:
            num_pairs = parse_read_delete(fd_input, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
            if (num_pairs == 0)
            {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }

            int saved_stdout_read;
            if (redirect_output(fd_output, &saved_stdout_read) == -1)
            {
                perror("Failed to redirect stdout");
                break;
            }

            sort_key_value_pairs(keys, values, num_pairs);
            if (kvs_read(num_pairs, keys))
            {
                fprintf(stderr, "Failed to read pair\n");
            }

            restore_output(saved_stdout_read);
            break;

        case CMD_DELETE:
            num_pairs = parse_read_delete(fd_input, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
            if (num_pairs == 0)
            {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }

            int saved_stdout_delete;
            if (redirect_output(fd_output, &saved_stdout_delete) == -1)
            {
                perror("Failed to redirect stdout");
                break;
            }

            if (kvs_delete(num_pairs, keys))
            {
                fprintf(stderr, "Failed to delete pair\n");
            }

            restore_output(saved_stdout_delete);
            break;

        case CMD_SHOW:
        {
            int saved_stdout_show;
            if (redirect_output(fd_output, &saved_stdout_show) == -1)
            {
                perror("Failed to redirect stdout");
                break;
            }

            kvs_show();

            restore_output(saved_stdout_show);
            break;
        }

        case CMD_WAIT:
            if (parse_wait(fd_input, &delay, NULL) == -1)
            {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }
            if (delay > 0)
            {
                kvs_wait(delay);
            }
            break;

        case CMD_BACKUP:
        {
            char backup_file[MAX_PATH_LENGTH];
            snprintf(backup_file, MAX_PATH_LENGTH, "%.*s-%d.bck",
                     (int)(strlen(input_file) - 4), input_file, active_count);

            // Wait if the active backup count reaches the limit
            while (active_count >= max_concurrent_backups)
            {
                int status;
                pid_t pid = wait(&status); // Wait for any child process to finish
                if (pid > 0)
                {
                    // Remove the finished process from active backups
                    for (int i = 0; i < active_count; i++)
                    {
                        if (active_backups[i] == pid)
                        {
                            active_backups[i] = active_backups[--active_count];
                            break;
                        }
                    }
                }
            }

            pid_t pid = fork();
            if (pid == 0)
            {
                // Child process
                if (kvs_backup(backup_file) == -1)
                {
                    fprintf(stderr, "Backup failed for file: %s\n", backup_file);
                }
                exit(0); // Exit the child process
            }
            else if (pid > 0)
            {
                // Parent process
                if (active_count < max_concurrent_backups)
                {
                    active_backups[active_count++] = pid; // Add to active backups
                }
            }
            else
            {
                perror("Failed to fork");
            }
            break;
        }
        case CMD_INVALID:
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            break;

        case CMD_HELP:
            printf(
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

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <directory_path> <max_concurrent_backups>\n", argv[0]);
        return 1;
    }
    int max_concurrent_backups = atoi(argv[2]);
    pid_t active_backups[max_concurrent_backups];
    int active_count = 1;
    if (max_concurrent_backups <= 0)
    {
        fprintf(stderr, "Invalid max_concurrent_backups value\n");
        return 1;
    }
    const char *dir_path = argv[1];

    if (kvs_init())
    {
        fprintf(stderr, "Failed to initialize KVS\n");
        return 1;
    }

    // Listar os ficheiros .job na diretoria
    char **job_files = NULL;
    size_t num_files = 0;
    list_job_files(dir_path, &job_files, &num_files);

    // Processar cada ficheiro .job
    for (size_t i = 0; i < num_files; i++)
    {
        process_job_file(job_files[i], active_backups, active_count, max_concurrent_backups);
        free(job_files[i]); // Liberar memória do caminho do arquivo após o processamento
    }
    free(job_files); // Liberar a memória alocada para os arquivos

    kvs_terminate();
    return 0;
}