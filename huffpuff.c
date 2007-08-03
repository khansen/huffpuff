#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "huffpuff.h"
#include "charmap.h"

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
 */
static void huffman_generate_codes(huffman_node_t *node, int length, int code)
{
    node->code.length = length;
    node->code.code = code;
    if (node->symbol == -1) {
        length++;
        code <<= 1;
        huffman_generate_codes(node->left, length, code);
        code |= 1;
        huffman_generate_codes(node->right, length, code);
    }
}

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
    huffman_node_t *root;
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
        nodes[i-1] = huffman_create_node(/*symbol=*/-1, n1->weight+n2->weight, n1, n2);
    }
    root = nodes[0];
    /* Generate Huffman codes from tree. */
    huffman_generate_codes(root, /*length=*/0, /*code=*/0);
    return root;
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
static void write_huffman_codes(FILE *out, huffman_node_t *root)
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
        /* label */
        fprintf(out, "node_%d_%d\t", node->code.code, node->code.length);
        if (node->symbol != -1) {
            /* a leaf node */
            fprintf(out, ".db $00, $%.2X\n", node->symbol);
        } else {
            /* an interior node -- print pointers to children */
            huffman_node_list_t *succ;
            fprintf(out, ".db node_%d_%d-$, node_%d_%d-$+1\n",
                    node->code.code << 1, node->code.length+1,
                    (node->code.code << 1) | 1, node->code.length+1);
            /* add child nodes to list */
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

string_list_t *read_strings(FILE *in, unsigned char *charmap, int *freq)
{
    unsigned char *buf;
    string_list_t *head;
    string_list_t **nextp;
    int max_len;
    int i;

    /* Zap frequency counts. */
    for (i=0; i<256; i++)
        freq[i] = 0;

    /* Read strings and count character frequencies as we go. */
    head = NULL;
    nextp = &head;
    max_len = 64;
    buf = (unsigned char *)malloc(max_len);
    while (!feof(in)) {
        /* Read one string (all chars until STRING_SEPARATOR) into temp buffer */
        int c;
        i = 0;
        while (((c = fgetc(in)) != -1) && (c != STRING_SEPARATOR)) {
                if (c == '\\') {
                    /* Check for line escape */
                    int d;
                    d = fgetc(in);
                    if (d == STRING_SEPARATOR) {
                        continue;
                    } else {
                        ungetc(d, in);
                    }
                }
                if (i == max_len) {
                    /* Allocate larger buffer */
                    max_len += 64;
                    buf = (unsigned char *)realloc(buf, max_len);
                }
                c = charmap[c];
                buf[i++] = c;
                freq[c]++;
        }
        if (i > 0) {
            /* Add string to list */
            *nextp = (string_list_t *)malloc(sizeof(string_list_t));
            (*nextp)->text = (unsigned char *)malloc(i+1);
            memcpy((*nextp)->text, buf, i);
            (*nextp)->text[i] = 0;
            (*nextp)->next = NULL;
            nextp = &((*nextp)->next);
        }
    }
    free(buf);
    return head;
}

void destroy_string_list(string_list_t *lst)
{
    string_list_t *tmp;
    for ( ; lst != 0; lst = tmp) {
        tmp = lst->next;
        free(lst->text);
        free(lst);
    }
}

/**
 * Program entrypoint.
 */
int main(int argc, char **argv)
{
    unsigned char charmap[256];
    int frequencies[256];
    huffman_node_t *leaf_nodes[256];
    huffman_node_t *code_nodes[256];
    huffman_node_t *root;
    int symbol_count;
    string_list_t *strings;

    /* Check argument count. */
    if (argc > 2) {
        fprintf(stderr, "usage: huffpuff [OPTION...]\n");
        return -1;
    }

    /* Set default character mapping f(c)=c */
    {
        int i;
        for (i=0; i<256; i++)
            charmap[i] = (unsigned char)i;
    }

    if (argc == 2) {
        /* Read character map. */
        if (charmap_parse(argv[1], charmap) == 0) {
            fprintf(stderr, "error: could not open character map `%s' for reading\n", argv[1]);
            return -1;
        }
    }

    /* Read strings to encode. */
    strings = read_strings(stdin, charmap, frequencies);

    /* Create Huffman leaf nodes. */
    symbol_count = 0;
    {
        int i;
        for (i=0; i<256; i++) {
            if (frequencies[i] > 0) {
                huffman_node_t *node;
                node = huffman_create_node(
                    /*symbol=*/i, /*weight=*/frequencies[i],
                    /*left=*/NULL, /*right=*/NULL);
                leaf_nodes[symbol_count++] = node;
                code_nodes[i] = node;
            } else {
                code_nodes[i] = 0;
            }
        }
    }

    /* Build the Huffman tree. */
    root = huffman_build_tree(leaf_nodes, symbol_count);

    /* Print the Huffman codes in code length order. */
    fprintf(stdout, "huff_node_table:\n");
    write_huffman_codes(stdout, root);
    fprintf(stdout, "\n");

    /* Huffman-encode strings. */
    write_huffman_strings(stdout, strings, code_nodes);

    /* Free the Huffman tree. */
    huffman_delete_node(root);

    /* Free string list */
    destroy_string_list(strings);

    return 0;
}
