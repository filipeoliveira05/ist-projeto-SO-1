#ifndef KVS_OPERATIONS_H
#define KVS_OPERATIONS_H

#include <stddef.h>

/// Redirects the standard output (stdout) to a specified file descriptor.
/// @param new_fd The file descriptor to redirect stdout to.
/// @param saved_fd Pointer to an integer where the original stdout file descriptor will be saved.
/// @return 0 if the redirection was successful, -1 otherwise.
int redirect_output(int new_fd, int *saved_fd);

/// Restores the standard output (stdout) to its original file descriptor after a redirection.
/// @param saved_fd The file descriptor of the original stdout to restore.
/// @return void.
void restore_output(int saved_fd);

/// Initializes the KVS state.
/// @return 0 if the KVS state was initialized successfully, 1 otherwise.
int kvs_init();

/// Destroys the KVS state.
/// @return 0 if the KVS state was terminated successfully, 1 otherwise.
int kvs_terminate();

/// Writes a key value pair to the KVS. If key already exists it is updated.
/// @param num_pairs Number of pairs being written.
/// @param keys Array of keys' strings.
/// @param values Array of values' strings.
/// @return 0 if the pairs were written successfully, 1 otherwise.
int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE]);

/// Reads values from the KVS.
/// @param num_pairs Number of pairs to read.
/// @param keys Array of keys' strings.
/// @param fd File descriptor to write the (successful) output.
/// @return 0 if the key reading, 1 otherwise.
int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE]);

/// Deletes key value pairs from the KVS.
/// @param num_pairs Number of pairs to read.
/// @param keys Array of keys' strings.
/// @return 0 if the pairs were deleted successfully, 1 otherwise.
int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE]);

/// Writes the state of the KVS.
/// @param fd File descriptor to write the output.
void kvs_show();

/// Creates a backup of the KVS state and stores it in the specified backup file.
/// @param backup_file The path to the file where the backup will be stored.
/// @return 0 if the backup was successful, -1 otherwise.
int kvs_backup(const char *backup_file, pthread_mutex_t *kvs_mutex);

/// Waits for a given amount of time.
/// @param delay_us Delay in milliseconds.
void kvs_wait(unsigned int delay_ms);

/// Sorts an array of key-value pairs in lexicographical order based on the keys.
/// @param keys An array of strings representing the keys to be sorted.
/// @param values An array of strings representing the corresponding values of the keys.
/// @param num_pairs The number of key-value pairs to be sorted.
void sort_key_value_pairs(char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE], size_t num_pairs);

#endif // KVS_OPERATIONS_H
