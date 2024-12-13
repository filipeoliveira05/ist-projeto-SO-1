#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#include "constants.h"
#include "parser.h"
#include "operations.h"

#define MAX_PATH_LENGTH 4096
typedef struct
{
    const char *file_name;
    pid_t *active_backups;
    int *active_count;
    int max_concurrent_backups;
    pthread_mutex_t *rd_jobs_mutex;
    pthread_mutex_t *kvs_backup_mutex;

} thread_args_t;

// Lista ficheiros com extensão .job na diretoria e retorna o número de ficheiros
void list_job_files(const char *dir_path, char ***files, size_t *num_files) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("Failed to open directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    *num_files = 0;

    // Conta o número de arquivos .job
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".job")) {
            (*num_files)++;
        }
    }

    // Aloca memória para armazenar os caminhos dos arquivos
    *files = malloc(sizeof(char *) * (*num_files));
    if (*files == NULL) {
        perror("Failed to allocate memory for files");
        exit(EXIT_FAILURE);
    }

    // Redefine o diretório e adiciona os arquivos à lista
    rewinddir(dir);
    size_t index = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".job")) {
            (*files)[index] = malloc(MAX_PATH_LENGTH * sizeof(char));
            if ((*files)[index] == NULL) {
                perror("Failed to allocate memory for file path");
                exit(EXIT_FAILURE);
            }
            snprintf((*files)[index], MAX_PATH_LENGTH, "%s/%s", dir_path, entry->d_name);
            index++;
        }
    }

    closedir(dir);

    // Ordena os arquivos .job em ordem alfabética
    qsort(*files, *num_files, sizeof((*files)[0]), (int (*)(const void *, const void *))strcmp);
}


// Processa comandos de um ficheiro .job e gera um ficheiro .out
void process_job_file(const char *input_file, pid_t *active_backups, int active_count, int max_concurrent_backups, pthread_mutex_t *rd_jobs_mutex, pthread_mutex_t *kvs_backup_mutex)
{
    // printf("CHEGA AQUI COM INPUT FILE %c \n", *input_file);

    int fd_input = 0;
    if (input_file != NULL)
    {
        // printf("THIS IS THE input FILE: %s\n", input_file);
        fd_input = open(input_file, O_RDONLY);
        if (fd_input == -1)
        {
            perror("Failed to open input file");
            return;
        }
    }
    else
    {
        printf("Error: input_file is NULL\n");
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
        pthread_mutex_lock(rd_jobs_mutex);

        enum Command cmd = get_next(fd_input);
        pthread_mutex_unlock(rd_jobs_mutex);

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
            // pthread_mutex_unlock(rd_jobs_mutex);
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
            // pthread_mutex_unlock(rd_jobs_mutex);

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

                if (kvs_backup(backup_file, kvs_backup_mutex) == -1)
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
                "  BACKUP\n" // Not implemented
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
void *thread_function(void *arg)
{
    thread_args_t *args = (thread_args_t *)arg;
    printf("THIS IS THE NAME OF THE FILE %s\n", args->file_name);
    // printf("THIS IS THE input FILE: %s\n", args->file_name);

    process_job_file(args->file_name, args->active_backups, *(args->active_count), args->max_concurrent_backups, args->rd_jobs_mutex, args->kvs_backup_mutex);

    // free(args);
    return NULL;
}

int main(int argc, char *argv[])
{

    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <directory_path> <max_concurrent_backups> <max_threads>\n", argv[0]);
        return 1;
    }
    int max_concurrent_backups = atoi(argv[2]);
    int MAXTHREADS = atoi(argv[3]);
    pid_t active_backups[max_concurrent_backups];
    int active_count = 1;
    int active_threads = 0;
    pthread_mutex_t rd_jobs_mutex;
    pthread_mutex_init(&rd_jobs_mutex, NULL);
    pthread_mutex_t kvs_backup_mutex;
    pthread_mutex_init(&kvs_backup_mutex, NULL);

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
    pthread_t threads[num_files];
    int thread_status[num_files]; // 0 = not finished, 1 = finished
    for (size_t k = 0; k < num_files; k++)
    {
        thread_status[k] = 0;
    }

    // Processar cada ficheiro .job
    for (size_t i = 0; i < num_files; i++)
    {
        // Allocate memory for thread arguments
        thread_args_t *args = malloc(sizeof(thread_args_t));
        if (args == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for thread arguments\n");
            exit(EXIT_FAILURE);
        }

        // Initialize thread arguments
        args->file_name = job_files[i];
        args->active_backups = active_backups; // Pass the shared backup array
        args->active_count = &active_count;    // Pass the pointer to active_count
        args->max_concurrent_backups = max_concurrent_backups;
        args->rd_jobs_mutex = &rd_jobs_mutex;
        args->kvs_backup_mutex = &kvs_backup_mutex;

        if (active_threads < MAXTHREADS)
        {
            // Create the thread
            if (pthread_create(&threads[i], NULL, thread_function, args) != 0)
            {
                fprintf(stderr, "Failed to create thread for file %s\n", args->file_name);
                // free(args);
                exit(EXIT_FAILURE);
            }
            else
            {
                sleep(1);

                printf("Thread %d successfully created for file %s\n", (int)i + 1, args->file_name);
                active_threads++;
                printf("ACTIVE THREADS %d\n", active_threads);
                printf("----------------------------------------\n");
            }
        }

        // Join any thread that finishes
        while (active_threads == MAXTHREADS)
        {
            for (size_t j = 0; j < num_files; j++)
            {
                if (thread_status[j] == 0)
                {                                                // If thread is not finished
                    int status = pthread_join(threads[j], NULL); // Non-blocking check
                    if (status == 0)
                    {
                        thread_status[j] = 1; // Mark thread as finished
                        active_threads--;
                        printf("THREAD %zu HAS FINISHED\n", j + 1);
                        printf("ACTIVE THREADS %d\n", active_threads);
                        break;
                    }
                }
            }
        }
    }
    free(job_files); // Liberar a memória alocada para os arquivos

    kvs_terminate();
    return 0;
}