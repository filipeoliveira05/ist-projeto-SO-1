#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "kvs.h"
#include "constants.h"

static struct HashTable *kvs_table = NULL;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms)
{
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

// Função para redirecionar saída padrão
int redirect_output(int new_fd, int *saved_fd)
{
  fflush(stdout); // Garante que todos os dados pendentes sejam escritos antes de redirecionar
  *saved_fd = dup(STDOUT_FILENO);
  if (*saved_fd == -1)
    return -1;
  if (dup2(new_fd, STDOUT_FILENO) == -1)
  {
    close(*saved_fd);
    return -1;
  }
  return 0;
}

// Função para restaurar saída padrão
void restore_output(int saved_fd)
{
  fflush(stdout); // Garante que todos os dados pendentes sejam escritos antes de restaurar
  dup2(saved_fd, STDOUT_FILENO);
  close(saved_fd);
}

int kvs_init()
{
  if (kvs_table != NULL)
  {
    fprintf(stderr, "KVS state has already been initialized\n");
    return 1;
  }

  kvs_table = create_hash_table();
  return kvs_table == NULL;
}

int kvs_terminate()
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  free_table(kvs_table);
  return 0;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE])
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  for (size_t i = 0; i < num_pairs; i++)
  {
    if (write_pair(kvs_table, keys[i], values[i]) != 0)
    {
      fprintf(stderr, "Failed to write keypair (%s,%s)\n", keys[i], values[i]);
    }
  }

  return 0;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE])
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  printf("[");
  for (size_t i = 0; i < num_pairs; i++)
  {
    char *result = read_pair(kvs_table, keys[i]);
    if (result == NULL)
    {
      printf("(%s,KVSERROR)", keys[i]);
    }
    else
    {
      printf("(%s,%s)", keys[i], result);
    }
    free(result);
  }
  printf("]\n");
  return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE])
{
  if (kvs_table == NULL)
  {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }
  int aux = 0;

  for (size_t i = 0; i < num_pairs; i++)
  {
    if (delete_pair(kvs_table, keys[i]) != 0)
    {
      if (!aux)
      {
        printf("[");
        aux = 1;
      }
      printf("(%s,KVSMISSING)", keys[i]);
    }
  }
  if (aux)
  {
    printf("]\n");
  }

  return 0;
}

void kvs_show()
{
  for (int i = 0; i < TABLE_SIZE; i++)
  {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL)
    {
      printf("(%s, %s)\n", keyNode->key, keyNode->value);
      keyNode = keyNode->next; // Move to the next node
    }
  }
}

int kvs_backup(const char *backup_file)
{
  int fd = open(backup_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1)
  {
    perror("Failed to open backup file");
    return -1;
  }

  int saved_stdout;
  if (redirect_output(fd, &saved_stdout) == -1)
  {
    perror("Failed to redirect output");
    close(fd);
    return -1;
  }

  kvs_show(); // Mostra o conteúdo da tabela hash no arquivo de backup

  restore_output(saved_stdout);
  close(fd);

  return 0;
}

void kvs_wait(unsigned int delay_ms)
{
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}

// Função para ordenar pares chave-valor pelo primeiro elemento (chave)
void sort_key_value_pairs(char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE], size_t num_pairs)
{
  for (size_t i = 0; i < num_pairs - 1; i++)
  {
    for (size_t j = 0; j < num_pairs - i - 1; j++)
    {
      // Comparar as chaves alfabeticamente
      if (strcmp(keys[j], keys[j + 1]) > 0)
      {
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