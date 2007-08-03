#ifndef HUFFMAN_H
#define HUFFMAN_H

/* A Huffman code */
struct huffman_code {
    int code;
    int length;
};

/* A Huffman node */
struct huffman_node {
    int symbol;
    int weight;
    struct huffman_node *left;
    struct huffman_node *right;
    struct huffman_code code;
};

typedef struct huffman_node huffman_node_t;

huffman_node_t *huffman_build_tree(huffman_node_t **, int);
huffman_node_t *huffman_create_node(int, int, huffman_node_t *, huffman_node_t *);
void huffman_delete_node(huffman_node_t *);
void huffman_generate_codes(huffman_node_t *, int, int, huffman_node_t **);
void huffman_print_codes(huffman_node_t *, int);

#endif // HUFFMAN_H
