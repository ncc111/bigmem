#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <ctype.h>
#include <getopt.h>

void *allocated_mem = NULL;
size_t allocated_size = 0;

void cleanup(int sig) {
    (void)sig;
    if (allocated_mem) {
        munmap(allocated_mem, allocated_size);
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

    while (isspace((unsigned char)*endptr)) endptr++;

    if (*endptr == '\0') return size;

    if (strcmp(endptr, "KB") == 0 || strcmp(endptr, "kb") == 0) size *= 1024ULL;
    else if (strcmp(endptr, "MB") == 0 || strcmp(endptr, "mb") == 0) size *= 1024ULL * 1024ULL;
    else if (strcmp(endptr, "GB") == 0 || strcmp(endptr, "gb") == 0) size *= 1024ULL * 1024ULL * 1024ULL;
    else if (strcmp(endptr, "K") == 0) size *= 1000ULL;
    else if (strcmp(endptr, "M") == 0) size *= 1000ULL * 1000ULL;
    else if (strcmp(endptr, "G") == 0) size *= 1000ULL * 1000ULL * 1000ULL;
    else {
        fprintf(stderr, "Error: Unknown suffix '%s'. Use KB, MB, GB (or K, M, G).\n", endptr);
        exit(1);
    }

    return size;
}

void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <size>\n"
        "Options:\n"
        "  -H, --hugepage     Use explicit 2MiB huge pages (requires pre-allocation)\n"
        "  -T, --thp          Advise Transparent Huge Pages (MADV_HUGEPAGE)\n"
        "  -h, --help         Show this help\n"
        "Examples:\n"
        "  %s 128MB\n"
        "  %s -H 1GB\n"
        "  %s -T 500MB\n",
        prog, prog, prog, prog
    );
}

int main(int argc, char *argv[]) {
    int use_hugepage = 0;
    int use_thp = 0;

    struct option long_options[] = {
        {"hugepage", no_argument, 0, 'H'},
        {"thp",      no_argument, 0, 'T'},
        {"help",     no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "HT", long_options, NULL)) != -1) {
        switch (c) {
            case 'H': use_hugepage = 1; break;
            case 'T': use_thp = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (optind != argc - 1) {
        print_usage(argv[0]);
        return 1;
    }

    allocated_size = parse_size(argv[optind]);
    if (allocated_size == 0) {
        fprintf(stderr, "Error: Size must be > 0.\n");
        return 1;
    }

    printf("Allocating %llu bytes (%.2f MB)...\n",
           (unsigned long long)allocated_size,
           allocated_size / (1024.0 * 1024.0));

    int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    int madvise_flag = 0;

    if (use_hugepage) {
        mmap_flags |= MAP_HUGETLB | (21 << MAP_HUGE_SHIFT);  // 2^21 = 2MiB
        printf("Requesting explicit 2MiB huge pages...\n");
    } else if (use_thp) {
        madvise_flag = MADV_HUGEPAGE;
        printf("Advising Transparent Huge Pages (THP)...\n");
    }

    allocated_mem = mmap(NULL, allocated_size,
                         PROT_READ | PROT_WRITE,
                         mmap_flags, -1, 0);

    if (allocated_mem == MAP_FAILED) {
        if (use_hugepage) {
            perror("mmap with MAP_HUGETLB failed");
            printf("Hint: Pre-allocate huge pages:\n"
                   "  echo 512 > /proc/sys/vm/nr_hugepages   # for ~1GB\n"
                   "  Or mount hugetlbfs: mkdir /mnt/huge && mount -t hugetlbfs none /mnt/huge\n");
            return 1;
        } else {
            perror("mmap failed");
            return 1;
        }
    }

    // Apply THP advice if requested
    if (use_thp && madvise(allocated_mem, allocated_size, MADV_HUGEPAGE) == -1) {
        perror("madvise(MADV_HUGEPAGE) failed");
        // Not fatal — continue
    }

    // Touch memory to force allocation
    printf("Touching memory pages...\n");
    memset(allocated_mem, 0, allocated_size);

    // Verify huge page usage (optional)
    if (use_hugepage) {
        printf("Success: Allocated using 2MiB huge pages.\n");
    } else if (use_thp) {
        printf("Success: Allocated with THP advice. Check /proc/PID/smaps for HugePages.\n");
    } else {
        printf("Success: Allocated with normal pages.\n");
    }

    printf("Process PID: %d | Using ~%.2f MB\n", getpid(), allocated_size / (1024.0 * 1024.0));
    printf("Press Ctrl+C to exit and free memory.\n");

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    while (1) pause();

    return 0;
}
