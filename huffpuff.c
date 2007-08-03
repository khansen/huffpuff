/**
 * huffpuff - text encoding using Huffman
 *
 * Example of use:
 *
 * huffpuff example.tbl < text-file > assembly-file
 *
 * where text-file is a file containing the strings you wish to encode, and
 * assembly-file is the resulting 6502 assembly file.
 *
 * The default input string separator is newline, but you can use line
 * continuation to split a single string into several lines in a text editor.
 *
 * The output is 6502 assembly compatible with my assembler, xorcyst, but this
 * program can easily be changed to output assembly in a different format.
 *
 * The output contains
 * - table of Huffman nodes used to decode the strings
 * - table of strings of encoded data
 *
 * This file should be linked with my 6502 Huffman decoder, huffman.asm,
 * available elsewhere. You can then
 * - initialize the Huffman decoder:
 *     - call huff_set_decode_table with address of huff_node_table in (A, Y)
 *     - call huff_set_string_table with address of huff_string_table in (A, Y)
 * - decode a string, byte by byte:
 *     - call huff_set_string with string # in A (index into string pointer table)
 *     - call huff_next_byte to decode the next byte, returned in A
 */

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "huffpuff.h"
#include "charmap.h"

/**
 * Builds a Huffman tree from an array of leafnodes with weights set.
 * @param nodes Array of Huffman leafnodes
 * @param nodecount Number of nodes in the array
 * @return Root of the resulting tree
 */
huffman_node_t *huffman_build_tree(huffman_node_t **nodes, int nodecount)
{
    huffman_node_t *n;
    huffman_node_t *n1;
    huffman_node_t *n2;
    int i, j, k;
    for (i=nodecount-1; i>0; i--) {
        /* Sort nodes based on frequency using simple bubblesort */
        for (j=i; j>0; j--) {
            for (k=0; k<j; k++) {
                if (nodes[k]->weight < nodes[k+1]->weight) {
                    n = nodes[k+1];
                    nodes[k+1] = nodes[k];
                    nodes[k] = n;
                }
            }
        }
        /* Combine nodes with two lowest frequencies */
        n1 = nodes[i];
        n2 = nodes[i-1];
        nodes[i-1] = huffman_create_node(-1, n1->weight+n2->weight, n1, n2);
    }
    return nodes[0];    /* Root of tree */
}

/**
 * Creates a Huffman node.
 * @param symbol The symbol that this node represents, or -1 if it's not a leaf node
 * @param weight The weight of this node
 * @param left The node's left child
 * @param right The node's right child
 * @return The new node
 */
huffman_node_t *huffman_create_node(int symbol, int weight, huffman_node_t *left, huffman_node_t *right)
{
    huffman_node_t *node = (huffman_node_t *)malloc(sizeof(huffman_node_t));
    node->symbol = symbol;
    node->weight = weight;
    node->left = left;
    node->right = right;
    return node;
}

/**
 * Deletes a Huffman node (tree) recursively.
 * @param node The node to delete
 */
void huffman_delete_node(huffman_node_t *node)
{
    if (node->symbol == -1) {
        huffman_delete_node(node->left);
        huffman_delete_node(node->right);
    }
    free(node);
}

/**
 * Generates codes for a Huffman node (tree) recursively.
 * @param node Node
 * @param length Current length (in bits)
 * @param code Current code
 * @param map Mapping from symbol to leaf node
 */
void huffman_generate_codes(huffman_node_t *node, int length, int code, huffman_node_t **map)
{
    node->code.length = length;
    node->code.code = code;
    if (node->symbol != -1) {
        map[node->symbol] = node;
    }
    else {
        length++;
        code <<= 1;
        huffman_generate_codes(node->left, length, code, map);
        code |= 1;
        huffman_generate_codes(node->right, length, code, map);
    }
}

struct huffman_node_list {
    struct huffman_node_list *next;
    huffman_node_t *node;
};

typedef struct huffman_node_list huffman_node_list_t;

/**
 * Writes codes for nodes in a Huffman tree recursively.
 * @param out File to write to
 * @param root Root node of Huffman tree
 */
void huffman_write_codes(FILE *out, huffman_node_t *root)
{
    huffman_node_list_t *current;
    huffman_node_list_t *tail;
    current = (huffman_node_list_t*)malloc(sizeof(huffman_node_list_t));
    current->node = root;
    current->next = 0;
    tail = current;
    while (current) {
        huffman_node_list_t *tmp;
        huffman_node_t *node;
        node = current->node;
        // label
        fprintf(out, "node_%d_%d\t", node->code.code, node->code.length);
        if (node->symbol != -1) {
            // a leaf node
            fprintf(out, ".db $00, $%.2X\n", node->symbol);
        } else {
            // an interior node -- print pointers to children
            huffman_node_list_t *succ;
            fprintf(out, ".db node_%d_%d-$, node_%d_%d-$+1\n",
                    node->code.code << 1, node->code.length+1,
                    (node->code.code << 1) | 1, node->code.length+1);
            // add child nodes to list
            succ = (huffman_node_list_t*)malloc(sizeof(huffman_node_list_t));
            succ->node = node->left;
            succ->next = (huffman_node_list_t*)malloc(sizeof(huffman_node_list_t));
            succ->next->node = node->right;
            succ->next->next = 0;
            tail->next = succ;
            tail = succ->next;
        }
        tmp = current->next;
        free(current);
        current = tmp;
    }
}

/* A linked list of text strings. */
struct string_list {
    struct string_list *next;
    unsigned char *text;
};

typedef struct string_list string_list_t;

/* The end-of-string token. */
#define STRING_SEPARATOR 0x0A

/**
 * Writes a chunk of data as assembly .db statements.
 * @param out File to write to
 * @param label Data label
 * @param comment Comment that describes the data
 * @param buf Data
 * @param size Total number of bytes
 * @param cols Number of columns
 */
static void write_chunk(FILE *out, char *label, char *comment,
                        unsigned char *buf, int size, int cols)
{
    int i, j, k, m;
    if (label)
        fprintf(out, "%s:\n", label);
    if (comment)
        fprintf(out, "; %s\n", comment);
    k=0;
    for (i=0; i<size/cols; i++) {
        fprintf(out, ".db ");
        for (j=0; j<cols-1; j++) {
            fprintf(out, "$%.2X,", buf[k++]);
        }
        fprintf(out, "$%.2X\n", buf[k++]);
    }
    m = size % cols;
    if (m > 0) {
        fprintf(out, ".db ");
        for (j=0; j<m-1; j++) {
            fprintf(out, "$%.2X,", buf[k++]);
        }
        fprintf(out, "$%.2X\n", buf[k++]);
    }
}

/**
 * Encodes the strings and writes the encoded data to file.
 * @param out File to write to
 */
static void write_huffman_strings(FILE *out, string_list_t *head, huffman_node_t **codes)
{
    string_list_t *string;
    int i;
    char strname[256];
    char strcomment[80];
    unsigned char *buf;
    unsigned char enc=0;
    huffman_node_t *node;
    int len;
    int bitnum;
    int strnum=0;
    int maxlen=0;
    buf = 0;
#if 0
    /* Print pointer table */
    fprintf(out, "huff_string_table:\n");
    for (i=0, string=head; string!=NULL; string=string->next, i++) {
        fprintf(out, ".dw string_%d\n", i);
    }
    fprintf(out, "\n");
#endif
    /* Do all strings. */
    for (string=head; string!=NULL; string=string->next) {
        /* Do all characters in string. */
        unsigned char *p;
        len=0;
        bitnum=7;
        p = string->text;
        while (*p) {
            node = codes[*(p++)];
            for (i=node->code.length-1; i>=0; i--) {
                enc |= ((node->code.code >> i) & 1) << bitnum--;
                if (bitnum < 0) {
                    if (len == maxlen) {
                        maxlen += 128;
                        buf = (char *)realloc(buf, maxlen);
                    }
                    buf[len++] = enc;
                    bitnum = 7;
                    enc = 0;
                }
            }
        }
        if (bitnum != 7) {  // write last few bits
            if (len == maxlen) {
                maxlen += 128;
                buf = (char *)realloc(buf, maxlen);
            }
            buf[len++] = enc;
        }

        sprintf(strname, "string_%d", strnum++);

        strcpy(strcomment, "\"");
        if (strlen(string->text) < 40) {
            strcat(strcomment, string->text);
        } else {
            strncat(strcomment, string->text, 37);
            strcat(strcomment, "...");
        }
        strcat(strcomment, "\"");

        /* Write encoded data */
        write_chunk(out, strname, strcomment, buf, len, 16);
    }
    free(buf);
}

/**
 * Program entrypoint.
 */
int main(int argc, char **argv)
{
    int i;
    int c;
    int d;
    unsigned char map[256];
    int frequencies[256];
    unsigned char *string;
    string_list_t *head;
    string_list_t **nextp;
    string_list_t *l;
    string_list_t *t;
    int max_len;
    huffman_node_t *leaf_nodes[256];
    huffman_node_t *code_nodes[256];
    huffman_node_t *root;
    int symbol_count;

    /* Check argument count. */
    if (argc > 2) {
        fprintf(stderr, "usage: huffman [TABLE-FILE] [< IN-FILE] [> OUT-FILE]\n");
        return -1;
    }

    /* Set default mapping f(c)=c */
    for (i=0; i<256; i++) {
        map[i] = (unsigned char)i;
    }
    if (argc == 2) {
        /* Read character map. */
        if (charmap_parse(argv[1], map) == 0) {
                fprintf(stderr, "error: could not open character map `%s' for reading\n", argv[1]);
                return -1;
        }
    }

    /* Zap frequency counts. */
    for (i=0; i<256; i++) {
        frequencies[i] = 0;
    }

    /* Read strings and count character frequencies as we go. */
    head = NULL;
    nextp = &head;
    max_len = 32;
    string = (unsigned char *)malloc(max_len);
    while (!feof(stdin)) {
        /* Read one string (all chars until STRING_SEPARATOR) into temp buffer */
        i=0;
        while (((c = fgetc(stdin)) != -1) && (c != STRING_SEPARATOR)) {
                if (c == '\\') {
                    /* Check for line escape */
                    d = fgetc(stdin);
                    if (d == STRING_SEPARATOR) {
                        continue;
                    }
                    else {
                        ungetc(d, stdin);
                    }
                }
                c = map[c];
                string[i++] = c;
                frequencies[c]++;
                if (i == max_len) {
                    /* Allocate larger buffer */
                    max_len *= 2;
                    string = (unsigned char *)realloc(string, max_len);
                }
        }
        if (i > 0) {
            /* Add string to list */
            *nextp = (string_list_t *)malloc(sizeof(string_list_t));
            (*nextp)->text = (unsigned char *)malloc(i);
            memcpy((*nextp)->text, string, i);
            (*nextp)->next = NULL;
            nextp = &((*nextp)->next);
        }
    }
    free(string);

    /* Create Huffman leaf nodes. */
    symbol_count = 0;
    for (i=0; i<256; i++) {
        if (frequencies[i] > 0) {
            leaf_nodes[symbol_count++] = huffman_create_node(
                /*symbol=*/i, /*weight=*/frequencies[i],
                /*left=*/NULL, /*right=*/NULL);
        }
    }

    /* Build the Huffman tree. */
    root = huffman_build_tree(leaf_nodes, symbol_count);

    /* Generate Huffman codes from tree. */
    huffman_generate_codes(root, /*length=*/0, /*code=*/0, code_nodes);

    /* Print ASM header */
    fprintf(stdout, ".codeseg\n\n");
    fprintf(stdout, ".public huff_node_table, huff_string_table\n\n");

    /* Print the Huffman codes in code length order. */
    fprintf(stdout, "huff_node_table:\n");
    huffman_write_codes(stdout, root);
    fprintf(stdout, "\n");

    /* Huffman-encode strings. */
    write_huffman_strings(stdout, head, code_nodes);

    /* Print ASM footer */
    fprintf(stdout, "\n.end\n");

    /* Free the Huffman tree. */
    huffman_delete_node(root);

    /* Free string list */
    for (l=head; l!=NULL; l = t) {
        t = l->next;
        free(l->text);
        free(l);
    }

    return 0;
}
