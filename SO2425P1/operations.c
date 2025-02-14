#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "constants.h"
#include "kvs.h"

static struct HashTable *kvs_table = NULL;
static pthread_rwlock_t kvs_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Calcula uma `timespec` a partir de um atraso em milissegundos.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

// Função para redirecionar saída padrão
int redirect_output(int new_fd, int *saved_fd) {
  // Garantir que todos os dados pendentes sejam escritos antes do
  // redirecionamento
  fflush(stdout);

  // Guardar o descritor original
  *saved_fd = dup(STDOUT_FILENO);
  if (*saved_fd == -1)
    return -1;

  // Redirecionar saída padrão
  if (dup2(new_fd, STDOUT_FILENO) == -1) {
    close(*saved_fd);
    return -1;
  }

  return 0;
}

// Função para restaurar saída padrão
void restore_output(int saved_fd) {
  // Garantir que todos os dados pendentes sejam escritos antes de restaurar
  fflush(stdout);

  // Restaurar descritor original
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
}

// Inicializa a estrutura de armazenamento chave-valor (KVS)
int kvs_init() {
  // Verificar se já está inicializado
  if (kvs_table != NULL) {
    fprintf(stderr, "KVS state has already been initialized\n");
    return 1;
  }

  // Criar a tabela de dispersão
  kvs_table = create_hash_table();
  if (kvs_table == NULL)
    return 1;

  // Inicializar a rwlock
  if (pthread_rwlock_init(&kvs_rwlock, NULL) != 0) {
    fprintf(stderr, "Failed to initialize rwlock\n");
    free_table(kvs_table);
    kvs_table = NULL;
    return 1;
  }

  return 0;
}

// Termina e limpa os recursos associados ao KVS
int kvs_terminate() {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  // Libertar a tabela
  free_table(kvs_table);
  kvs_table = NULL;

  // Destruir a rwlock
  if (pthread_rwlock_destroy(&kvs_rwlock) != 0) {
    fprintf(stderr, "Failed to destroy rwlock\n");
    return 1;
  }

  return 0;
}

// Escreve pares chave-valor na tabela
int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE],
              char values[][MAX_STRING_SIZE]) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  // Adquirir um bloqueio de escrita
  if (pthread_rwlock_wrlock(&kvs_rwlock) != 0) {
    perror("Failed to acquire write lock");
    return -1;
  }

  // Escrita do par na tabela
  for (size_t i = 0; i < num_pairs; i++) {
    if (write_pair(kvs_table, keys[i], values[i]) != 0) {
      fprintf(stderr, "Failed to write keypair (%s,%s)\n", keys[i], values[i]);
    }
  }

  // Libertar o bloqueio de escrita
  if (pthread_rwlock_unlock(&kvs_rwlock) != 0) {
    perror("Failed to release write lock");
    return -1;
  }

  return 0;
}

// Lê pares chave-valor da tabela
int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE]) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  // Adquirir um bloqueio de leitura
  if (pthread_rwlock_rdlock(&kvs_rwlock) != 0) {
    perror("Failed to acquire read lock");
    return -1;
  }

  // Leitura de par/pares da tabela
  printf("[");
  for (size_t i = 0; i < num_pairs; i++) {
    char *result = read_pair(kvs_table, keys[i]);
    if (result == NULL) {
      printf("(%s,KVSERROR)", keys[i]);
    } else {
      printf("(%s,%s)", keys[i], result);
    }
    free(result);
  }
  printf("]\n");

  // Libertar o bloqueio de leitura
  if (pthread_rwlock_unlock(&kvs_rwlock) != 0) {
    perror("Failed to release read lock");
    return -1;
  }

  return 0;
}

// Remove pares chave-valor da tabela
int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE]) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  // Adquirir um bloqueio de escrita
  if (pthread_rwlock_wrlock(&kvs_rwlock) != 0) {
    perror("Failed to acquire write lock");
    return -1;
  }

  // Remoção do par/pares da tabela, se possível
  int aux = 0;
  for (size_t i = 0; i < num_pairs; i++) {
    if (delete_pair(kvs_table, keys[i]) != 0) {
      if (!aux) {
        printf("[");
        aux = 1;
      }
      printf("(%s,KVSMISSING)", keys[i]);
    }
  }
  if (aux) {
    printf("]\n");
  }

  // Libertar o bloqueio de escrita
  if (pthread_rwlock_unlock(&kvs_rwlock) != 0) {
    perror("Failed to release write lock");
    return -1;
  }

  return 0;
}

// Mostra todos os pares chave-valor armazenados na tabela
void kvs_show() {
  // Adquirir um bloqueio de leitura
  if (pthread_rwlock_rdlock(&kvs_rwlock) != 0) {
    perror("Failed to acquire read lock");
    return;
  }

  // Iteração pela tabela de dispersão
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL) {
      printf("(%s, %s)\n", keyNode->key, keyNode->value);
      keyNode = keyNode->next;
    }
  }

  // Liberta o bloqueio de leitura
  if (pthread_rwlock_unlock(&kvs_rwlock) != 0) {
    perror("Failed to release read lock");
  }
}

// Cria um backup da tabela de dispersão num ficheiro
int kvs_backup(const char *backup_file, pthread_mutex_t *kvs_mutex) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  // Tenta adquirir o mutex para proteger a região crítica
  if (pthread_mutex_lock(kvs_mutex) != 0) {
    perror("Failed to lock mutex");
    return -1;
  }

  // Abre o ficheiro de backup para escrita, criando-o ou truncando se já
  // existir
  int fd = open(backup_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    perror("Failed to open backup file");
    pthread_mutex_unlock(kvs_mutex);
    return -1;
  }

  // Redireciona a saída padrão para o ficheiro de backup
  int saved_stdout;
  if (redirect_output(fd, &saved_stdout) == -1) {
    perror("Failed to redirect output");
    close(fd);
    pthread_mutex_unlock(kvs_mutex);
    return -1;
  }

  // Mostra a tabela de dispersão
  kvs_show();

  // Restaura a saída padrão para o estado original
  restore_output(saved_stdout);
  close(fd);

  // Liberta o mutex após a região crítica
  if (pthread_mutex_unlock(kvs_mutex) != 0) {
    perror("Failed to unlock mutex");
    return -1;
  }

  return 0;
}

// Espera um período de tempo especificado em milissegundos
void kvs_wait(unsigned int delay_ms) {
  // Converte o atraso em milissegundos para a estrutura timespec
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

// Ordena pares chave-valor pelo primeiro elemento (chave)
void sort_key_value_pairs(char keys[][MAX_STRING_SIZE],
                          char values[][MAX_STRING_SIZE], size_t num_pairs) {
  for (size_t i = 0; i < num_pairs - 1; i++) {
    for (size_t j = 0; j < num_pairs - i - 1; j++) {
      // Comparar as chaves alfabeticamente
      if (strcmp(keys[j], keys[j + 1]) > 0) {
        // Trocar chaves
        char temp_key[MAX_STRING_SIZE];
        strcpy(temp_key, keys[j]);
        strcpy(keys[j], keys[j + 1]);
        strcpy(keys[j + 1], temp_key);

        // Trocar valores correspondentes
        char temp_value[MAX_STRING_SIZE];
        strcpy(temp_value, values[j]);
        strcpy(values[j], values[j + 1]);
        strcpy(values[j + 1], temp_value);
      }
    }
  }
}
