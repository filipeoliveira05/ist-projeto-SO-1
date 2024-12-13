#ifndef KVS_OPERATIONS_H
#define KVS_OPERATIONS_H

#include <stddef.h>

/// Redireciona a saída padrão (stdout) para um descritor de ficheiro
/// especificado.
/// @param new_fd O descritor de ficheiro para onde a saída padrão será
/// redirecionada.
/// @param saved_fd Ponteiro para um inteiro onde será guardado o descritor de
/// ficheiro original do stdout.
/// @return 0 se o redirecionamento foi bem-sucedido, -1 caso contrário.
int redirect_output(int new_fd, int *saved_fd);

/// Restaura a saída padrão (stdout) para o seu descritor de ficheiro original
/// após um redirecionamento.
/// @param saved_fd O descritor de ficheiro original do stdout que será
/// restaurado.
/// @return void.
void restore_output(int saved_fd);

/// Inicializa o estado do KVS.
/// @return 0 se o estado do KVS foi inicializado com sucesso, 1 caso contrário.
int kvs_init();

/// Destrói o estado do KVS.
/// @return 0 se o estado do KVS foi terminado com sucesso, 1 caso contrário.
int kvs_terminate();

/// Escreve um par chave-valor no KVS. Se a chave já existir, ela será
/// atualizada.
/// @param num_pairs Número de pares a serem escritos.
/// @param keys Array de strings representando as chaves.
/// @param values Array de strings representando os valores.
/// @return 0 se os pares foram escritos com sucesso, 1 caso contrário.
int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE],
              char values[][MAX_STRING_SIZE]);

/// Lê valores do KVS.
/// @param num_pairs Número de pares a serem lidos.
/// @param keys Array de strings representando as chaves.
/// @param fd Descritor de ficheiro para onde o output (sucesso) será escrito.
/// @return 0 se a leitura das chaves foi bem-sucedida, 1 caso contrário.
int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE]);

/// Remove pares chave-valor do KVS.
/// @param num_pairs Número de pares a serem removidos.
/// @param keys Array de strings representando as chaves.
/// @return 0 se os pares foram removidos com sucesso, 1 caso contrário.
int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE]);

/// Mostra o estado atual do KVS.
/// @param fd Descritor de ficheiro para onde o output será escrito.
void kvs_show();

/// Cria um backup do estado do KVS e armazena-o num ficheiro especificado.
/// @param backup_file O caminho para o ficheiro onde o backup será armazenado.
/// @return 0 se o backup foi bem-sucedido, -1 caso contrário.
int kvs_backup(const char *backup_file, pthread_mutex_t *kvs_mutex);

/// Espera por um período de tempo especificado.
/// @param delay_ms Atraso em milissegundos.
void kvs_wait(unsigned int delay_ms);

/// Ordena um array de pares chave-valor em ordem lexicográfica com base nas
/// chaves.
/// @param keys Um array de strings representando as chaves a serem ordenadas.
/// @param values Um array de strings representando os valores correspondentes
/// às chaves.
/// @param num_pairs O número de pares chave-valor a serem ordenados.
void sort_key_value_pairs(char keys[][MAX_STRING_SIZE],
                          char values[][MAX_STRING_SIZE], size_t num_pairs);

#endif // KVS_OPERATIONS_H