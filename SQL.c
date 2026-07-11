#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h> // Added for uint32_t

// Fixed sizes for columns to make memory layout predictable.
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define TABLE_MAX_ROWS 1400

// Data structures
// 1. ROW: Represents a single record. Fixed size makes it easy to calculate memory offsets.
typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1]; // Added + 1 for '\0'
  char email[COLUMN_EMAIL_SIZE + 1];       // Added + 1 for '\0'
} Row;

// 2. TABLE: Holds a array of rows and keeps track of how many rows we have.
typedef struct {
  uint32_t num_rows;
  Row rows[TABLE_MAX_ROWS];
} Table;

// 3. CURSOR: Represents a location in the table. Used to iterate through rows.
typedef struct {
  Table* table;
  uint32_t row_num;
  bool end_of_table;
} Cursor;

// cursor functions
// These functions abstract away the logic of moving through the table.
Cursor* table_start(Table* table) {
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->row_num = 0;
  cursor->end_of_table = (table->num_rows == 0);
  return cursor;
}

Cursor* table_end(Table* table) {
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->row_num = table->num_rows;
  cursor->end_of_table = true;
  return cursor;
}

void cursor_advance(Cursor* cursor) {
  cursor->row_num += 1;
  if (cursor->row_num >= cursor->table->num_rows) {
    cursor->end_of_table = true;
  }
}

Row* cursor_value(Cursor* cursor) {
  return &(cursor->table->rows[cursor->row_num]);
}

// Repl and Input Buffer: unchanged from Part 1
typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;
  return input_buffer;
}

void print_prompt() { printf("db > "); }

void read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

// Enums unchanged: from Part 2
typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum { 
  PREPARE_SUCCESS, 
  PREPARE_UNRECOGNIZED_STATEMENT,
  PREPARE_SYNTAX_ERROR,
  PREPARE_STRING_TOO_LONG,
  PREPARE_NEGATIVE_ID
} PrepareResult;

typedef enum { 
  STATEMENT_INSERT, 
  STATEMENT_SELECT 
} StatementType;

// Statement: updated to hold a row
typedef struct {
  StatementType type;
  Row row_to_insert; // new line to hold the row data for insert statements
} Statement;

// META command: unchanged
MetaCommandResult do_meta_command(InputBuffer* input_buffer) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    exit(EXIT_SUCCESS);
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

// Compiler: updated to parse insert arguments
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_INSERT;

  // 1. strtok splits the input string by spaces. 
  // Modifies the original string by inserting null terminators where the spaces were.
  char* keyword = strtok(input_buffer->buffer, " ");
  char* id_string = strtok(NULL, " ");
  char* username = strtok(NULL, " ");
  char* email = strtok(NULL, " ");

  // 2. Makes sure that the user actually provided all 3 arguments
  if (id_string == NULL || username == NULL || email == NULL) {
    return PREPARE_SYNTAX_ERROR;
  }

  // 3. Converts the string ID to an integer
  int id = atoi(id_string);
  if (id < 0) {
    return PREPARE_NEGATIVE_ID;
  }

  // 4. Security check: Checks string lengths BEFORE copying
  if (strlen(username) > COLUMN_USERNAME_SIZE) {
    return PREPARE_STRING_TOO_LONG;
  }
  if (strlen(email) > COLUMN_EMAIL_SIZE) {
    return PREPARE_STRING_TOO_LONG;
  }

  // 5. Copy the strings into the row struct
  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);

  return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement);
  }
  if (strcmp(input_buffer->buffer, "select") == 0) {
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }

  return PREPARE_UNRECOGNIZED_STATEMENT;
}

// Virtual machine: updated to actually insert and print
// Helper to print row
void print_row(Row* row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void execute_insert(Statement* statement, Table* table) {
  // Checks if table is full
  if (table->num_rows >= TABLE_MAX_ROWS) {
    printf("Error: Table full.\n");
    return;
  }

  // Get the cursor to the end of the table
  Row* row_to_insert = &(table->rows[table->num_rows]);
  // Copy the data from the statement into the table's row
  *row_to_insert = statement->row_to_insert;
  
  table->num_rows += 1;
  printf("Executed.\n");
}

void execute_select(Statement* statement, Table* table) {
  // Start a cursor at the beginning of the table
  Cursor* cursor = table_start(table);

  // Loop through all rows until we hit the end
  while (!(cursor->end_of_table)) {
    Row* row = cursor_value(cursor);
    print_row(row);
    cursor_advance(cursor);
  }

  free(cursor);
  printf("Executed.\n");
}

void execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
    case (STATEMENT_INSERT):
      execute_insert(statement, table);
      break;
    case (STATEMENT_SELECT):
      execute_select(statement, table);
      break;
  }
}

// Main: updated to initialize the table
int main(int argc, char* argv[]) {
  Table* table = malloc(sizeof(Table));
  table->num_rows = 0;
  
  InputBuffer* input_buffer = new_input_buffer();
  
  while (true) {
    print_prompt();
    read_input(input_buffer);

    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer)) {
        case (META_COMMAND_SUCCESS):
          continue;
        case (META_COMMAND_UNRECOGNIZED_COMMAND):
          printf("Unrecognized command '%s'\n", input_buffer->buffer);
          continue;
      }
    }

    Statement statement;
    switch (prepare_statement(input_buffer, &statement)) {
    case (PREPARE_SUCCESS):
      break;
    case (PREPARE_SYNTAX_ERROR):
      printf("Syntax error. Could not parse statement.\n");
      continue;
    case (PREPARE_NEGATIVE_ID):
      printf("ID must be positive.\n");
      continue;
    case (PREPARE_STRING_TOO_LONG):
      printf("String too long.\n");
      continue;
    case (PREPARE_UNRECOGNIZED_STATEMENT):
      printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
      continue;
      
    }
      execute_statement(&statement, table);
    }
}