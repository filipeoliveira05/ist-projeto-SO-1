#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

// Estrutura para armazenar argumentos das threads
typedef struct {
  const char *file_name;             // Nome do ficheiro .job
  pid_t *active_backups;             // Lista de processos de backup ativos
  int *active_count;                 // Contador de backups ativos
  int max_concurrent_backups;        // Número máximo de backups concorrentes
  pthread_mutex_t *rd_jobs_mutex;    // Mutex para sincronizar leitura de jobs
  pthread_mutex_t *kvs_backup_mutex; // Mutex para sincronizar backups
} thread_args_t;

// Lista ficheiros com extensão .job na diretoria e retorna o número de
// ficheiros
void list_job_files(const char *dir_path, char ***files, size_t *num_files) {
  // Abre a diretoria especificada
  DIR *dir = opendir(dir_path);
  if (!dir) {
    perror("Failed to open directory");
    exit(EXIT_FAILURE);
  }

  struct dirent *entry;
  *num_files = 0;

  // Conta o número de ficheiros com extensão .job
  while ((entry = readdir(dir)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len > 4 && strcmp(entry->d_name + len - 4, ".job") == 0) {
      (*num_files)++;
    }
  }

  // Aloca memória para armazenar os caminhos dos ficheiros
  *files = malloc(sizeof(char *) * (*num_files));
  if (*files == NULL) {
    perror("Failed to allocate memory for files");
    closedir(dir);
    exit(EXIT_FAILURE);
  }

  // Redefine o ponteiro da diretoria e preenche a lista de ficheiros
  rewinddir(dir);
  size_t index = 0;
  while ((entry = readdir(dir)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len > 4 && strcmp(entry->d_name + len - 4, ".job") == 0) {
      (*files)[index] = malloc(MAX_PATH_LENGTH * sizeof(char));
      if ((*files)[index] == NULL) {
        perror("Failed to allocate memory for file path");
        // Limpa a memória já alocada
        for (size_t i = 0; i < index; i++) {
          free((*files)[i]);
        }
        free(*files);
        closedir(dir);
        exit(EXIT_FAILURE);
      }
      // Constrói o caminho completo do ficheiro
      snprintf((*files)[index], MAX_PATH_LENGTH, "%s/%s", dir_path,
               entry->d_name);
      index++;
    }
  }

  closedir(dir);

  // Ordena os arquivos .job em ordem alfabética
  qsort(*files, *num_files, sizeof((*files)[0]),
        (int (*)(const void *, const void *))strcmp);
}

// Processa comandos de um ficheiro .job e gera um ficheiro .out
void process_job_file(const char *input_file, pid_t *active_backups,
                      int active_count, int max_concurrent_backups,
                      pthread_mutex_t *rd_jobs_mutex,
                      pthread_mutex_t *kvs_backup_mutex) {
  // Abre o ficheiro .job para leitura
  int fd_input = 0;
  if (input_file != NULL) {
    fd_input = open(input_file, O_RDONLY);
    if (fd_input == -1) {
      perror("Failed to open input file");
      return;
    }
  } else {
    printf("Error: input_file is NULL\n");
  }

  // Gera o nome do ficheiro de saída substituindo ".job" por ".out"
  char output_file[MAX_PATH_LENGTH];
  snprintf(output_file, MAX_PATH_LENGTH, "%.*s.out",
           (int)(strlen(input_file) - 4), input_file);

  // Abre o ficheiro .out para escrita
  int fd_output = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_output == -1) {
    perror("Failed to open output file");
    close(fd_input);
    return;
  }

  // Processar comandos no ficheiro .job
  while (1) {
    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    // Sincroniza a leitura do ficheiro usando o mutex
    pthread_mutex_lock(rd_jobs_mutex);
    enum Command cmd = get_next(fd_input);
    pthread_mutex_unlock(rd_jobs_mutex);

    switch (cmd) {
    case CMD_WRITE:
      // Processa comando de escrita
      num_pairs =
          parse_write(fd_input, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      // Redireciona saída para o ficheiro de saída
      int saved_stdout_write;
      if (redirect_output(fd_output, &saved_stdout_write) == -1) {
        perror("Failed to redirect stdout");
        break;
      }

      sort_key_value_pairs(keys, values, num_pairs);
      if (kvs_write(num_pairs, keys, values)) {
        fprintf(stderr, "Failed to write pair\n");
      }

      // Restaura a saída original
      restore_output(saved_stdout_write);

      break;

    case CMD_READ:
      // Processa comando de leitura
      num_pairs =
          parse_read_delete(fd_input, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      // Redireciona saída para o ficheiro de saída
      int saved_stdout_read;
      if (redirect_output(fd_output, &saved_stdout_read) == -1) {
        perror("Failed to redirect stdout");
        break;
      }

      sort_key_value_pairs(keys, values, num_pairs);
      if (kvs_read(num_pairs, keys)) {
        fprintf(stderr, "Failed to read pair\n");
      }

      // Restaura a saída original
      restore_output(saved_stdout_read);

      break;

    case CMD_DELETE:
      // Processa comando de delete
      num_pairs =
          parse_read_delete(fd_input, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      // Redireciona saída para o ficheiro de saída
      int saved_stdout_delete;
      if (redirect_output(fd_output, &saved_stdout_delete) == -1) {
        perror("Failed to redirect stdout");
        break;
      }

      if (kvs_delete(num_pairs, keys)) {
        fprintf(stderr, "Failed to delete pair\n");
      }

      // Restaura a saída original
      restore_output(saved_stdout_delete);

      break;

    case CMD_SHOW: {

      // Redireciona saída para o ficheiro de saída
      int saved_stdout_show;
      if (redirect_output(fd_output, &saved_stdout_show) == -1) {
        perror("Failed to redirect stdout");
        break;
      }

      kvs_show();

      // Restaura a saída original
      restore_output(saved_stdout_show);

      break;
    }

    case CMD_WAIT:
      // Processa comando de wait
      if (parse_wait(fd_input, &delay, NULL) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay > 0) {
        kvs_wait(delay);
      }

      break;

    case CMD_BACKUP: {
      // Processa comando de backup
      char backup_file[MAX_PATH_LENGTH];
      // Cria o nome do arquivo de backup baseado no nome do ficheiro de entrada
      // e no contador de backups ativos
      snprintf(backup_file, MAX_PATH_LENGTH, "%.*s-%d.bck",
               (int)(strlen(input_file) - 4), input_file, active_count);

      // Espera se o número de backups ativos atingir o limite máximo
      while (active_count >= max_concurrent_backups) {
        int status;
        pid_t pid = wait(&status); // Espera por qualquer processo filho
                                   // terminar
        if (pid > 0) {
          // Remove o processo terminado da lista de backups ativos
          for (int i = 0; i < active_count; i++) {
            if (active_backups[i] == pid) {
              active_backups[i] = active_backups[--active_count];
              break;
            }
          }
        }
      }

      // Cria um processo filho
      pid_t pid = fork();
      if (pid == 0) {
        // Código executado pelo processo filho
        // Tenta realizar o backup e exibe uma mensagem de erro caso falhe
        if (kvs_backup(backup_file, kvs_backup_mutex) == -1) {
          fprintf(stderr, "Backup failed for file: %s\n", backup_file);
        }

        // O processo filho termina após o backup
        exit(0);

      } else if (pid > 0) {
        // Código executado pelo processo pai
        // Se o número de backups ativos for menor que o limite, adiciona o novo
        // processo à lista de backups ativos
        if (active_count < max_concurrent_backups) {
          active_backups[active_count++] = pid;
        }
      } else {
        perror("Failed to fork");
      }

      break;
    }

    case CMD_INVALID:

      fprintf(stderr, "Invalid command. See HELP for usage\n");

      break;

    case CMD_HELP:
      printf("Available commands:\n"
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
      // Fecha os descritores de ficheiros de entrada e saída
      close(fd_input);
      close(fd_output);

      return;
    }
  }
}
void *thread_function(void *arg) {
  // Obtém os argumentos da thread
  thread_args_t *args = (thread_args_t *)arg;

  // Processa o ficheiro de trabalho, passando os parâmetros necessários
  process_job_file(args->file_name, args->active_backups, *(args->active_count),
                   args->max_concurrent_backups, args->rd_jobs_mutex,
                   args->kvs_backup_mutex);

  return NULL;
}

int main(int argc, char *argv[]) {

  // Verifica se o número de argumentos passados na linha de comando é correto
  if (argc != 4) {
    fprintf(
        stderr,
        "Usage: %s <directory_path> <max_concurrent_backups> <max_threads>\n",
        argv[0]);
    return 1;
  }

  // Converte os parâmetros de linha de comando para inteiros
  int max_concurrent_backups = atoi(argv[2]);
  int MAXTHREADS = atoi(argv[3]);

  // Declaração de variáveis para backups ativos e controle de threads
  pid_t active_backups[max_concurrent_backups];
  int active_count = 1;
  int active_threads = 0;

  // Inicializa os mutexes para controle de acesso a recursos compartilhados
  pthread_mutex_t rd_jobs_mutex;
  pthread_mutex_init(&rd_jobs_mutex, NULL);
  pthread_mutex_t kvs_backup_mutex;
  pthread_mutex_init(&kvs_backup_mutex, NULL);

  // Verifica se o número máximo de backups concorrentes é válido
  if (max_concurrent_backups <= 0) {
    fprintf(stderr, "Invalid max_concurrent_backups value\n");
    return 1;
  }

  // Caminho da diretoria dos arquivos .job
  const char *dir_path = argv[1];

  // Inicializa o sistema KVS
  if (kvs_init()) {
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  // Lista os ficheiros .job presentes na diretoria especificada
  char **job_files = NULL;
  size_t num_files = 0;
  list_job_files(dir_path, &job_files, &num_files);

  // Se não houver ficheiros .job, termina o program
  if (num_files == 0) {
    fprintf(stderr, "No .job files found in the specified directory.\n");
    kvs_terminate();
    return 0;
  }

  // Declara arrays para armazenar as threads e o status de cada uma
  pthread_t threads[num_files];

  // 0 = não finalizado, 1 = finalizado
  // Inicializa todos os status das threads como não finalizado
  int thread_status[num_files];
  for (size_t k = 0; k < num_files; k++) {
    thread_status[k] = 0;
  }

  // Processar cada ficheiro .job
  for (size_t i = 0; i < num_files; i++) {
    // Aloca memória para os argumentos da thread
    thread_args_t *args = malloc(sizeof(thread_args_t));
    if (args == NULL) {
      fprintf(stderr, "Failed to allocate memory for thread arguments\n");
      exit(EXIT_FAILURE);
    }

    // Inicializa os argumentos da thread com os parâmetros necessários
    args->file_name = job_files[i];
    args->active_backups = active_backups; // Pass the shared backup array
    args->active_count = &active_count;    // Pass the pointer to active_count
    args->max_concurrent_backups = max_concurrent_backups;
    args->rd_jobs_mutex = &rd_jobs_mutex;
    args->kvs_backup_mutex = &kvs_backup_mutex;

    // Verifica se o número de threads ativas é menor que o limite
    if (active_threads < MAXTHREADS) {
      // Cria a thread para processar o arquivo .job
      if (pthread_create(&threads[i], NULL, thread_function, args) != 0) {
        fprintf(stderr, "Failed to create thread for file %s\n",
                args->file_name);
        exit(EXIT_FAILURE);

      } else {
        // Atraso para permitir que a thread seja criada corretamente
        sleep(1);

        active_threads++;
      }
    }

    // Aguarda a finalização de qualquer thread quando o número máximo de
    // threads ativas for atingido
    while (active_threads == MAXTHREADS) {
      for (size_t j = 0; j < num_files; j++) {
        // Se a thread ainda não foi finalizada
        if (thread_status[j] == 0) {
          int status = pthread_join(threads[j], NULL); // Non-blocking check
          if (status == 0) {
            // Marca a thread como finalizada
            thread_status[j] = 1;
            active_threads--;
            free(args);
            break;
          }
        }
      }
    }

    // Liberta a memória do ficheiro .job processado
    free(job_files[i]);
  }

  // Liberta a memória alocada para a lista de ficheiros .job
  free(job_files);

  // Finaliza o sistema KVS
  kvs_terminate();

  return 0;
}