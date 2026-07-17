#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

// Constants and macros
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

const uint32_t COLUMN_USERNAME_SIZE = 32;
const uint32_t COLUMN_EMAIL_SIZE = 255;

typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;
#define TABLE_MAX_PAGES 1000

// Node header constants
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// Leaf node constants
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;

const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

// New for part 9: split counts for dividing cells between nodes
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

// NNew for part 9: internal node constants
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET = INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE;

const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE = INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
const uint32_t INTERNAL_NODE_SPACE_FOR_CELLS = PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_MAX_CELLS = INTERNAL_NODE_SPACE_FOR_CELLS / INTERNAL_NODE_CELL_SIZE;

// Enums
enum NodeType { NODE_LEAF, NODE_INTERNAL };

// Serialization 
void serialize_row(Row* source, void* destination) {
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  strncpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
  strncpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

// Data structures
typedef struct {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages; // NNew for part 9: track total allocated pages
  void* pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
  Pager* pager;
  uint32_t root_page_num;
} Table;

typedef struct {
  Table* table;
  uint32_t cell_num;
  bool end_of_table;
} Cursor;

// Node accessor functions
uint8_t* node_type(void* node) { return node + NODE_TYPE_OFFSET; }

bool is_node_root(void* node) {
  uint8_t value = *(uint8_t*)(node + IS_ROOT_OFFSET);
  return (bool)value;
}

void set_node_root(void* node, bool is_root) {
  uint8_t value = is_root;
  *(uint8_t*)(node + IS_ROOT_OFFSET) = value;
}

uint32_t* leaf_node_num_cells(void* node) {
  return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_OFFSET;
}

void* leaf_node_value(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_VALUE_OFFSET;
}

void initialize_leaf_node(void* node) {
  *leaf_node_num_cells(node) = 0;
}

// New for part 9: internal node accessor functions
uint32_t* internal_node_num_keys(void* node) {
  return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t* internal_node_right_child(void* node) {
  return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

void* internal_node_cell(void* node, uint32_t cell_num) {
  return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t* internal_node_child(void* node, uint32_t child_num) {
  uint32_t num_keys = *internal_node_num_keys(node);
  if (child_num > num_keys) {
    printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
    exit(EXIT_FAILURE);
  } else if (child_num == num_keys) {
    return internal_node_right_child(node);
  } else {
    return internal_node_cell(node, child_num);
  }
}

uint32_t* internal_node_key(void* node, uint32_t key_num) {
  return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

// Pager and table functions
void* get_page(Pager* pager, uint32_t page_num) {
  if (page_num > TABLE_MAX_PAGES) {
    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    void* page = malloc(PAGE_SIZE);
    uint32_t num_pages = pager->file_length / PAGE_SIZE;

    if (pager->file_length % PAGE_SIZE) {
      num_pages += 1;
    }

    if (page_num <= num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;
  }

  return pager->pages[page_num];
}

// New for part 9: allocate a new page and return its number
uint32_t allocate_page(Pager* pager) {
  void* page = get_page(pager, pager->num_pages);
  return pager->num_pages++;
}

Pager* pager_open(const char* filename) {
  int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
  if (fd == -1) {
    printf("Unable to open file %s\n", filename);
    exit(EXIT_FAILURE);
  }

  off_t file_length = lseek(fd, 0, SEEK_END);

  Pager* pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = file_length;
  // New for part 9: initialize num_pages
  pager->num_pages = (file_length / PAGE_SIZE);
  if (file_length % PAGE_SIZE != 0) {
    pager->num_pages += 1;
  }

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = NULL;
  }

  return pager;
}

Table* db_open(const char* filename) {
  Pager* pager = pager_open(filename);

  Table* table = malloc(sizeof(Table));
  table->pager = pager;
  table->root_page_num = 0;

  if (pager->num_pages == 0) {
    void* root_node = get_page(pager, 0);
    *node_type(root_node) = NODE_LEAF;
    set_node_root(root_node, true);
    initialize_leaf_node(root_node);
    pager->num_pages += 1; // New for part 9: account for root page
  }

  return table;
}

void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
  if (pager->pages[page_num] == NULL) {
    printf("Tried to flush null page\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
  if (offset == -1) {
    printf("Error seeking: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written = write(pager->file_descriptor, pager->pages[page_num], size);
  if (bytes_written == -1) {
    printf("Error writing: %d\n", errno);
    exit(EXIT_FAILURE);
  }
}

void db_close(Table* table) {
  Pager* pager = table->pager;
  
  // New for part 9: flush all pages
  for (uint32_t i = 0; i < pager->num_pages; i++) {
    if (pager->pages[i] == NULL) continue;
    pager_flush(pager, i, PAGE_SIZE);
    free(pager->pages[i]);
    pager->pages[i] = NULL;
  }

  int result = close(pager->file_descriptor);
  if (result == -1) {
    printf("Error closing db file.\n");
    exit(EXIT_FAILURE);
  }
  free(pager);
  free(table);
}

// B-tree functions
uint32_t leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_cells;
  
  while (one_past_max_index != min_index) {
    uint32_t index = (min_index + one_past_max_index) / 2;
    uint32_t key_at_index = *leaf_node_key(node, index);
    
    if (key == key_at_index) {
      return index;
    }
    if (key < key_at_index) {
      one_past_max_index = index;
    } else {
      min_index = index + 1;
    }
  }
  return min_index;
}

// New for part 9: gets the maximum key in a node
uint32_t get_node_max_key(void* node) {
  if (*node_type(node) == NODE_LEAF) {
    return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
  return *internal_node_key(node, *internal_node_num_keys(node) - 1);
}

// Cursor functions
Cursor* table_start(Table* table) {
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->cell_num = 0;

  void* root_node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *leaf_node_num_cells(root_node);
  cursor->end_of_table = (num_cells == 0);

  return cursor;
}

Cursor* table_end(Table* table) {
  void* root_node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *leaf_node_num_cells(root_node);

  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->cell_num = num_cells;
  cursor->end_of_table = true;

  return cursor;
}

void cursor_advance(Cursor* cursor) {
  cursor->cell_num += 1;
  if (cursor->cell_num >= *leaf_node_num_cells(get_page(cursor->table->pager, cursor->table->root_page_num))) {
    cursor->end_of_table = true;
  }
}

void* cursor_value(Cursor* cursor) {
  return leaf_node_value(get_page(cursor->table->pager, cursor->table->root_page_num), cursor->cell_num);
}

// Repl and input buffer
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

bool read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
  
  if (bytes_read < 0) {
    return false;
  }
  
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
  return true;
}

void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

// Enums and statement
typedef enum { 
  META_COMMAND_SUCCESS, 
  META_COMMAND_UNRECOGNIZED_COMMAND,
  META_COMMAND_BTREE 
} MetaCommandResult;

typedef enum { PREPARE_SUCCESS, PREPARE_UNRECOGNIZED_STATEMENT, PREPARE_SYNTAX_ERROR, PREPARE_STRING_TOO_LONG, PREPARE_NEGATIVE_ID } PrepareResult;
typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

typedef struct {
  StatementType type;
  Row row_to_insert;
} Statement;

// Meta command 
void print_leaf_node(void* node) {
  uint32_t num_cells = *leaf_node_num_cells(node);
  printf("leaf (size %d)\n", num_cells);
  for (uint32_t i = 0; i < num_cells; i++) {
    uint32_t key = *leaf_node_key(node, i);
    printf("  - %d : %d\n", i, key);
  }
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    db_close(table);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
    printf("Tree:\n");
    print_leaf_node(get_page(table->pager, table->root_page_num));
    return META_COMMAND_SUCCESS;
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

// Compiler 
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
  statement->type = STATEMENT_INSERT;
  char* keyword = strtok(input_buffer->buffer, " ");
  char* id_string = strtok(NULL, " ");
  char* username = strtok(NULL, " ");
  char* email = strtok(NULL, " ");

  if (id_string == NULL || username == NULL || email == NULL) return PREPARE_SYNTAX_ERROR;
  int id = atoi(id_string);
  if (id < 0) return PREPARE_NEGATIVE_ID;
  if (strlen(username) > COLUMN_USERNAME_SIZE) return PREPARE_STRING_TOO_LONG;
  if (strlen(email) > COLUMN_EMAIL_SIZE) return PREPARE_STRING_TOO_LONG;

  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);
  return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) return prepare_insert(input_buffer, statement);
  if (strcmp(input_buffer->buffer, "select") == 0) {
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}

// Virtual machine
void print_row(Row* row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

// New for part 9: create a new root internal node when old root splits
void create_new_root(Table* table, uint32_t right_child_page_num) {
  void* root = get_page(table->pager, table->root_page_num);
  void* right_child = get_page(table->pager, right_child_page_num);
  uint32_t left_child_page_num = allocate_page(table->pager);
  void* left_child = get_page(table->pager, left_child_page_num);

  // Left child gets the old root's data
  memcpy(left_child, root, PAGE_SIZE);
  set_node_root(left_child, false);

  // Root becomes an internal node
  *node_type(root) = NODE_INTERNAL;
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
}

// New for part 9: split a full leaf node and insert the new key
void leaf_node_split_and_insert(Table* table, uint32_t page_num, uint32_t key, Row* value) {
  void* old_node = get_page(table->pager, page_num);
  uint32_t old_max = get_node_max_key(old_node);

  uint32_t new_page_num = allocate_page(table->pager);
  void* new_node = get_page(table->pager, new_page_num);
  initialize_leaf_node(new_node);
  *node_type(new_node) = NODE_LEAF;
  set_node_root(new_node, false);

  // If old node was root, create new root
  if (is_node_root(old_node)) {
    create_new_root(table, new_page_num);
  }

  // Copy right half of cells to new node
  for (uint32_t i = 0; i < LEAF_NODE_RIGHT_SPLIT_COUNT; i++) {
    void* destination = leaf_node_cell(new_node, i);
    void* source = leaf_node_cell(old_node, i + LEAF_NODE_LEFT_SPLIT_COUNT);
    memcpy(destination, source, LEAF_NODE_CELL_SIZE);
  }

  // Update cell counts
  *leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
  *leaf_node_num_cells(new_node) = LEAF_NODE_RIGHT_SPLIT_COUNT;

  // Determine which node to insert into
  uint32_t index_to_insert;
  void* node_to_insert;
  uint32_t page_to_insert;

  if (key <= old_max) {
    node_to_insert = old_node;
    page_to_insert = page_num;
  } else {
    node_to_insert = new_node;
    page_to_insert = new_page_num;
  }

  uint32_t num_cells = *leaf_node_num_cells(node_to_insert);
  index_to_insert = leaf_node_find(table, page_to_insert, key);

  // Shift cells and insert
  for (uint32_t i = num_cells; i > index_to_insert; i--) {
    void* destination = leaf_node_cell(node_to_insert, i);
    void* source = leaf_node_cell(node_to_insert, i - 1);
    memcpy(destination, source, LEAF_NODE_CELL_SIZE);
  }

  *leaf_node_key(node_to_insert, index_to_insert) = key;
  serialize_row(value, leaf_node_value(node_to_insert, index_to_insert));
  *leaf_node_num_cells(node_to_insert) = num_cells + 1;
}

void execute_insert(Statement* statement, Table* table) {
  void* node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  Row* row_to_insert = &(statement->row_to_insert);
  uint32_t key_to_insert = row_to_insert->id;
  
  uint32_t index_to_insert = leaf_node_find(table, table->root_page_num, key_to_insert);
  
  if (index_to_insert < num_cells) {
    uint32_t key_at_index = *leaf_node_key(node, index_to_insert);
    if (key_at_index == key_to_insert) {
      printf("Error: Duplicate key.\n");
      return;
    }
  }

  // New for part 9: split if full instead of erroring
  if (num_cells >= LEAF_NODE_MAX_CELLS) {
    leaf_node_split_and_insert(table, table->root_page_num, key_to_insert, row_to_insert);
    return;
  }

  // Shift cells
  for (uint32_t i = num_cells; i > index_to_insert; i--) {
    void* destination = leaf_node_cell(node, i);
    void* source = leaf_node_cell(node, i - 1);
    memcpy(destination, source, LEAF_NODE_CELL_SIZE);
  }

  *leaf_node_key(node, index_to_insert) = key_to_insert;
  serialize_row(row_to_insert, leaf_node_value(node, index_to_insert));
  *leaf_node_num_cells(node) = num_cells + 1;

  printf("Executed.\n");
}

void execute_select(Statement* statement, Table* table) {
  Cursor* cursor = table_start(table);
  Row row;
  while (!(cursor->end_of_table)) {
    deserialize_row(cursor_value(cursor), &row);
    print_row(&row);
    cursor_advance(cursor);
  }
  free(cursor);
  printf("Executed.\n");
}

void execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
    case STATEMENT_INSERT: execute_insert(statement, table); break;
    case STATEMENT_SELECT: execute_select(statement, table); break;
  }
}

// Main
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Must supply a database filename.\n");
    exit(EXIT_FAILURE);
  }

  char* filename = argv[1];
  Table* table = db_open(filename);
  InputBuffer* input_buffer = new_input_buffer();
  
  while (true) {
    print_prompt();
    
    if (!read_input(input_buffer)) {
      printf("\n");
      break;
    }

    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, table)) {
        case META_COMMAND_SUCCESS: continue;
        case META_COMMAND_UNRECOGNIZED_COMMAND:
          printf("Unrecognized command '%s'\n", input_buffer->buffer);
          continue;
        case META_COMMAND_BTREE: continue;
      }
    }

    Statement statement;
    switch (prepare_statement(input_buffer, &statement)) {
      case PREPARE_SUCCESS: break;
      case PREPARE_SYNTAX_ERROR: printf("Syntax error. Could not parse statement.\n"); continue;
      case PREPARE_NEGATIVE_ID: printf("ID must be positive.\n"); continue;
      case PREPARE_STRING_TOO_LONG: printf("String is too long.\n"); continue;
      case PREPARE_UNRECOGNIZED_STATEMENT: printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer); continue;
    }

    execute_statement(&statement, table);
  }
  
  close_input_buffer(input_buffer);
  db_close(table);
  exit(EXIT_SUCCESS);
}