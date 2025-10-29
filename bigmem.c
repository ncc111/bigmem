#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <ctype.h>
#Date: 2025-10-29
// Global pointer to free on exit
void *allocated_mem = NULL;

void cleanup(int sig) {
    (void)sig;
    if (allocated_mem) {
        munmap(allocated_mem, 0); // Size will be tracked internally
    }
    printf("\nMemory freed. Exiting.\n");
    exit(0);
}

unsigned long long parse_size(const char *str) {
    char *endptr;
    unsigned long long size = strtoull(str, &endptr, 10);

    if (endptr == str) {
        fprintf(stderr, "Error: No number found in argument.\n");
        exit(1);
    }

    // Skip whitespace
    while (isspace(*endptr)) endptr++;

    if (*endptr == '\0') {
        return size; // Default to bytes
    }

    // Handle suffixes
    if (strcmp(endptr, "KB") == 0 || strcmp(endptr, "kb") == 0) {
        size *= 1024ULL;
    } else if (strcmp(endptr, "MB") == 0 || strcmp(endptr, "mb") == 0) {
        size *= 1024ULL * 1024ULL;
    } else if (strcmp(endptr, "GB") == 0 || strcmp(endptr, "gb") == 0) {
        size *= 1024ULL * 1024ULL * 1024ULL;
    } else if (strcmp(endptr, "K") == 0) {
        size *= 1000ULL;
    } else if (strcmp(endptr, "M") == 0) {
        size *= 1000ULL * 1000ULL;
    } else if (strcmp(endptr, "G") == 0) {
        size *= 1000ULL * 1000ULL * 1000ULL;
    } else {
        fprintf(stderr, "Error: Unknown suffix '%s'. Use KB, MB, GB (or K, M, G for decimal).\n", endptr);
        exit(1);
    }

    return size;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <size>\n", argv[0]);
        fprintf(stderr, "Example: %s 128MB\n", argv[0]);
        fprintf(stderr, "         %s 1GB\n", argv[0]);
        fprintf(stderr, "         %s 500KB\n", argv[0]);
        return 1;
    }

    unsigned long long size = parse_size(argv[1]);

    if (size == 0) {
        fprintf(stderr, "Error: Size must be greater than 0.\n");
        return 1;
    }

    printf("Allocating %llu bytes (~ %.2f MB)...\n",
           size, size / (1024.0 * 1024.0));

    // Use mmap for large allocations (more reliable than malloc)
    allocated_mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (allocated_mem == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    // Touch the memory to ensure it's actually allocated (page faults)
    printf("Touching memory to force allocation...\n");
    memset(allocated_mem, 0, size);

    printf("Successfully allocated and using %.2f MB of memory.\n", size / (1024.0 * 1024.0));
    printf("Process PID: %d\n", getpid());
    printf("Press Ctrl+C to free memory and exit.\n");

    // Set up signal handler for clean exit
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    // Keep process alive
    while (1) {
        pause();
    }

    return 0;
}
