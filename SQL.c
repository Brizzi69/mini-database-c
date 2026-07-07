#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// created a struct to hold the state of our input buffer.
typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

// Memory allocation: malloc to allocate memory on the heap.
InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;

  return input_buffer;
}

void print_prompt() { printf("db > "); }


// It dynamically allocates memory for the buffer if it's NULL.
void read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  // Ignore trailing newline character that getline includes
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

// Memory Deallocation: because we used malloc we must use free.
// If we don't, we create a memory leak, which will crash the DB later.
void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer); // Free the buffer allocated by getline
    free(input_buffer);         // Free the struct allocated by new_input_buffer
}

int main(int argc, char* argv[]) {
  InputBuffer* input_buffer = new_input_buffer();
  
  // The infinite loop of the REPL
  while (true) {
    print_prompt();
    read_input(input_buffer);

    // Check if the user wants to exit
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
      close_input_buffer(input_buffer);
      exit(EXIT_SUCCESS);
    } else {
      printf("Unrecognized command '%s'.\n", input_buffer->buffer);
    }
  }
}