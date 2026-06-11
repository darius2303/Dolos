#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define SIZE_64 64
#define MAX_TOKEN_LENGTH 63
#define INITIAL_HASH 5381
#define DJB2_SHIFT 5

static unsigned char to_lowercase(unsigned char character) {
    return (unsigned char)tolower(character);
}

int tokenize_and_stem(const unsigned char* data, int size,
    char stemmed_tokens[][SIZE_64], int max_tokens) {
    int count = 0;
    char* buf = malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    char* cursor = buf;
    while (*cursor && count < max_tokens) {
        while (*cursor && !isalnum((unsigned char)*cursor) && *cursor != '_') {
            cursor++;
        }

        if (!*cursor) {
            break;
        }

        char word[SIZE_64];
        int wlen = 0;
        while (*cursor && (isalnum((unsigned char)*cursor) || *cursor == '_') && wlen < MAX_TOKEN_LENGTH) {
            word[wlen++] = (char)to_lowercase((unsigned char)*cursor++);
        }
        word[wlen] = '\0';
        if (wlen == 0) {
            continue;
        }

        snprintf(stemmed_tokens[count], SIZE_64, "%s", word);
        count++;
    }

    free(buf);
    return count;
}

int build_shingles(char tokens[][SIZE_64], int token_count, unsigned long* shingles,
    int max_shingles, int shingle_size) {
    int count = 0;
    for (int i = 0; i <= token_count - shingle_size && count < max_shingles; i++) {
        unsigned long hash_value = INITIAL_HASH;
        for (int j = 0; j < shingle_size; j++) {
            for (const char* character = tokens[i + j]; *character; character++) {
                hash_value = ((hash_value << DJB2_SHIFT) + hash_value) + (unsigned char)*character;
            }
        }
        shingles[count++] = hash_value;
    }
    return count;
}

double jaccard(const unsigned long* first_set, int first_count,
    const unsigned long* second_set, int second_count) {
    int intersect = 0;
    char* used = calloc(second_count, sizeof(char));
    if (!used) return 0.0;

    for (int i = 0; i < first_count; i++) {
        for (int j = 0; j < second_count; j++) {
            if (!used[j] && first_set[i] == second_set[j]) {
                intersect++;
                used[j] = 1;
                break;
            }
        }
    }

    free(used);

    int union_count = first_count + second_count - intersect;
    if (union_count == 0) {
        return 0.0;
    }

    return (double)intersect / (double)union_count;
}

unsigned char* load_file_bytes(const char* path, int* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    unsigned char* buf = malloc(size);
    if (buf) {
        size_t read_bytes = fread(buf, 1, size, f);
        *out_size = (int)read_bytes;
    }
    fclose(f);
    return buf;
}

void execute_comparison_check(const char* f1_path, const char* f2_path, double expected_percentage) {
    int s1 = 0, s2 = 0;
    unsigned char* b1 = load_file_bytes(f1_path, &s1);
    unsigned char* b2 = load_file_bytes(f2_path, &s2);

    if (!b1 || !b2) {
        fprintf(stderr, "[FAIL] Target tracking down broken: Could not locate configuration files.\n");
        if (b1) free(b1);
        if (b2) free(b2);
        exit(1);
    }

    char (*t1)[64] = malloc(65536 * 64);
    char (*t2)[64] = malloc(65536 * 64);
    unsigned long* sh1 = malloc(65536 * sizeof(unsigned long));
    unsigned long* sh2 = malloc(65536 * sizeof(unsigned long));

    int tc1 = tokenize_and_stem(b1, s1, t1, 65536);
    int tc2 = tokenize_and_stem(b2, s2, t2, 65536);

    int sc1 = build_shingles(t1, tc1, sh1, 65536, 3);
    int sc2 = build_shingles(t2, tc2, sh2, 65536, 3);

    double coefficient = jaccard(sh1, sc1, sh2, sc2);
    double plag_percentage = coefficient * 100.0;

    printf("[TEST] Running comparison: %s <-> %s\n", f1_path, f2_path);
    printf("[TEST] Math Results -> Detected Similarity Metrics: %.1f%%\n", plag_percentage);

    double variance_epsilon = 1.5;

    if (fabs(plag_percentage - expected_percentage) <= variance_epsilon) {
        printf("[PASS] Similarity values align within accepted delta parameters!\n\n");
    }
    else {
        fprintf(stderr, "[FAIL] Matrix mismatch! Expected near %.1f%%, got %.1f%%\n",
            expected_percentage, plag_percentage);
        free(b1); free(b2); free(t1); free(t2); free(sh1); free(sh2);
        exit(1);
    }

    free(b1); free(b2); free(t1); free(t2); free(sh1); free(sh2);
}

int main(void) {
    printf("=== RUNNING PLAGIARISM BACKEND REGRESSION SUITE ===\n\n");

    execute_comparison_check("demo/stack_original.c", "demo/stack_plag.c", 59.9);
    execute_comparison_check("demo/ll_stack_orig.py", "demo/ll_stack_plag.py", 34.2);

    printf("=== ALL ALGORITHM VERIFICATION TESTS PASSED ===\n");
    return 0;
}