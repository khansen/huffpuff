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
    if (node == 0)
        return;
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
    if (!node)
        return;
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
    if (nodecount == 0)
        return 0;
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
static void write_huffman_codes(FILE *out, huffman_node_t *root,
                                const unsigned char *charmap)
{
    huffman_node_list_t *current;
    huffman_node_list_t *tail;
    if (root == 0)
        return;
    current = (huffman_node_list_t*)malloc(sizeof(huffman_node_list_t));
    current->node = root;
    current->next = 0;
    tail = current;
    while (current) {
        huffman_node_list_t *tmp;
        huffman_node_t *node;
        node = current->node;
        /* label */
        if (node != root)
            fprintf(out, "@@node_%d_%d: ", node->code.code, node->code.length);
        if (node->symbol != -1) {
            /* a leaf node */
            fprintf(out, ".db $00, $%.2X\n", charmap[node->symbol]);
        } else {
            /* an interior node -- print pointers to children */
            huffman_node_list_t *succ;
            fprintf(out, ".db @@node_%d_%d-$, @@node_%d_%d-$+1\n",
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
    unsigned char *huff_data;
    int huff_size;
};

typedef struct string_list string_list_t;

/* The end-of-string token. */
#define STRING_SEPARATOR 0x0A

/**
 * Reads strings from a file and computes the frequencies of the characters.
 * @param in File to read from
 * @param freq Where to store computed frequencies
 * @return The resulting list of strings
 */
string_list_t *read_strings(FILE *in, int *freq)
{
    unsigned char *buf;
    string_list_t *head;
    string_list_t **nextp;
    int max_len;
    int i;

    /* Zap frequency counts. */
    for (i = 0; i < 256; i++)
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
                buf[i++] = c;
                freq[c]++;
        }

        if (i > 0) {
            /* Add string to list */
            string_list_t *lst = (string_list_t *)malloc(sizeof(string_list_t));
            lst->text = (unsigned char *)malloc(i+1);
            lst->huff_data = 0;
            lst->huff_size = 0;
            memcpy(lst->text, buf, i);
            lst->text[i] = 0;
            lst->next = NULL;
            *nextp = lst;
            nextp = &(lst->next);
        }
    }
    free(buf);
    return head;
}

/**
 * Encodes the given list of strings.
 * @param head Head of list of strings to encode
 * @param codes Mapping from character to Huffman node
 */
static void encode_strings(string_list_t *head, huffman_node_t * const *codes)
{
    string_list_t *string;
    unsigned char *buf;
    unsigned char enc=0;
    int maxlen=0;
    buf = 0;
    /* Do all strings. */
    for (string = head; string != NULL; string = string->next) {
        /* Do all characters in string. */
        const huffman_node_t *node;
        int i;
        int len;
        int bitnum;
        unsigned char *p;
        len=0;
        bitnum=7;
        p = string->text;
        while (*p) {
            node = codes[*(p++)];
            for (i = node->code.length-1; i >= 0; i--) {
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
        /* Store encoded buffer */
        string->huff_data = (unsigned char *)malloc(len);
        memcpy(string->huff_data, buf, len);
        string->huff_size = len;
    }
    free(buf);
}

/**
 * Writes a chunk of data as assembly .db statements.
 * @param out File to write to
 * @param label Data label
 * @param comment Comment that describes the data
 * @param buf Data
 * @param size Total number of bytes
 * @param cols Number of columns
 */
static void write_chunk(FILE *out, const char *label, const char *comment,
                        const unsigned char *buf, int size, int cols)
{
    int i, j, k, m;
    if (label && strlen(label))
        fprintf(out, "%s:\n", label);
    if (comment && strlen(comment))
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
 * @param head Head of list of strings to encode & write
 * @param label_prefix
 */
static void write_huffman_strings(FILE *out, const string_list_t *head,
                                  const char *label_prefix)
{
    const string_list_t *string;
    int string_id = 0;
    for (string = head; string != NULL; string = string->next) {
        char strlabel[256];
        char strcomment[80];

        sprintf(strlabel, "%sString%d", label_prefix, string_id++);

        strcpy(strcomment, "\"");
        if (strlen(string->text) < 40) {
            strcat(strcomment, string->text);
        } else {
            strncat(strcomment, string->text, 37);
            strcat(strcomment, "...");
        }
        strcat(strcomment, "\"");

        /* Write encoded data */
        write_chunk(out, strlabel, strcomment,
                    string->huff_data, string->huff_size, 16);
    }
}

/**
 * Destroys a string list.
 * @param lst The list to destroy
 */
void destroy_string_list(string_list_t *lst)
{
    string_list_t *tmp;
    for ( ; lst != 0; lst = tmp) {
        tmp = lst->next;
        free(lst->text);
        free(lst->huff_data);
        free(lst);
    }
}

static char program_version[] = "huffpuff 1.0.5";

/* Prints usage message and exits. */
static void usage()
{
    printf(
        "Usage: huffpuff [--character-map=FILE]\n"
        "                [--table-output=FILE] [--data-output=FILE]\n"
        "                [--table-label=LABEL] [--string-label-prefix=PREFIX]\n"
        "                [--generate-string-table]\n"
        "                [--help] [--usage] [--version]\n"
        "                FILE\n");
    exit(0);
}

/* Prints help message and exits. */
static void help()
{
    printf("Usage: huffpuff [OPTION...] FILE\n\n"
           "  --character-map=FILE            Transform input according to FILE\n"
           "  --table-output=FILE             Store Huffman decoder table in FILE\n"
           "  --data-output=FILE              Store Huffman-encoded data in FILE\n"
           "  --table-label=LABEL             Use given LABEL for Huffman decoder table\n"
           "  --string-label-prefix=PREFIX    Use given PREFIX as string label prefix\n"
           "  --generate-string-table         Generate string pointer table\n"
           "  --help                          Give this help list\n"
           "  --usage                         Give a short usage message\n"
           "  --version                       Print program version\n");
    exit(0);
}

/* Prints version and exits. */
static void version()
{
    printf("%s\n", program_version);
    exit(0);
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
    FILE *input;
    FILE *table_output;
    FILE *data_output;
    const char *input_filename = 0;
    const char *charmap_filename = 0;
    const char *table_output_filename = 0;
    const char *data_output_filename = 0;
    const char *table_label = 0;
    const char *string_label_prefix = "";
    int generate_string_table = 0;

    /* Process arguments. */
    {
        char *p;
        while ((p = *(++argv))) {
            if (!strncmp("--", p, 2)) {
                const char *opt = &p[2];
                if (!strncmp("character-map=", opt, 14)) {
                    charmap_filename = &opt[14];
                } else if (!strncmp("table-output=", opt, 13)) {
                    table_output_filename = &opt[13];
                } else if (!strncmp("data-output=", opt, 12)) {
                    data_output_filename = &opt[12];
                } else if (!strncmp("table-label=", opt, 12)) {
                    table_label = &opt[12];
                } else if (!strcmp("generate-string-table", opt)) {
                    generate_string_table = 1;
                } else if (!strncmp("string-label-prefix=", opt, 20)) {
                    string_label_prefix = &opt[20];
                } else if (!strcmp("help", opt)) {
                    help();
                } else if (!strcmp("usage", opt)) {
                    usage();
                } else if (!strcmp("version", opt)) {
                    version();
                } else {
                    fprintf(stderr, "unrecognized option `%s'\n", p);
                    return(-1);
                }
            } else {
                input_filename = p;
            }
        }
    }

    /* Set default character mapping f(c)=c */
    {
        int i;
        for (i=0; i<256; i++)
            charmap[i] = (unsigned char)i;
    }

    if (charmap_filename) {
        if (!charmap_parse(charmap_filename, charmap)) {
            fprintf(stderr, "error: failed to parse character map `%s'\n",
                    charmap_filename);
            return(-1);
        }
    }

    if (input_filename) {
        input = fopen(input_filename, "rt");
        if (!input) {
            fprintf(stderr, "error: failed to open `%s' for reading\n",
                    input_filename);
            return(-1);
        }
    } else {
        input = stdin;
    }

    if (!table_label)
        table_label = "huff_node_table";

    /* Read strings to encode. */
    strings = read_strings(input, frequencies);

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

    /* Huffman-encode strings. */
    encode_strings(strings, code_nodes);

    /* Prepare output */
    if (!table_output_filename) {
        table_output_filename = "huffpuff.tab";
    }
    table_output = fopen(table_output_filename, "wt");
    if (!table_output) {
        fprintf(stderr, "error: failed to open `%s' for writing\n",
                table_output_filename);
        return(-1);
    }

    if (!data_output_filename) {
        data_output_filename = "huffpuff.dat";
    }
    data_output = fopen(data_output_filename, "wt");
    if (!data_output) {
        fprintf(stderr, "error: failed to open `%s' for writing\n",
                data_output_filename);
        return(-1);
    }

    /* Print the Huffman codes in code length order. */
    if (table_label && strlen(table_label))
        fprintf(table_output, "%s:\n", table_label);
    write_huffman_codes(table_output, root, charmap);

    if (generate_string_table) {
        /* Print string pointer table */
        int i;
        string_list_t *lst;
        fprintf(data_output, "huff_string_table:\n");
        for (i = 0, lst = strings; lst != 0; lst = lst->next, ++i) {
            fprintf(data_output, ".dw @@String%d\n", i);
        }
        string_label_prefix = "@@";
    }

    /* Write the Huffman-encoded strings. */
    write_huffman_strings(data_output, strings, string_label_prefix);

    /* Free the Huffman tree. */
    huffman_delete_node(root);

    /* Free string list */
    destroy_string_list(strings);

    fclose(input);
    fclose(table_output);
    fclose(data_output);

    return 0;
}
