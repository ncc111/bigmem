#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <getopt.h>

void *mem_base = NULL;
size_t vsz_size = 0;
size_t rss_size = 0;

void cleanup(int sig) {
    (void)sig;
    if (mem_base) {
        munmap(mem_base, vsz_size);
    }
    printf("\nMemory freed. Exiting.\n");
    exit(0);
}

unsigned long long parse_size(const char *str) {
    char *endptr;
    unsigned long long num = strtoull(str, &endptr, 10);
    if (endptr == str || num == 0) {
        fprintf(stderr, "Error: Invalid number in '%s'\n", str);
        exit(1);
    }
    while (*endptr && strchr(" \t", *endptr)) endptr++;

    if (*endptr == '\0') return num;

    if (strcmp(endptr, "KB") == 0 || strcmp(endptr, "kb") == 0) num *= 1024ULL;
    else if (strcmp(endptr, "MB") == 0 || strcmp(endptr, "mb") == 0) num *= 1024ULL * 1024ULL;
    else if (strcmp(endptr, "GB") == 0 || strcmp(endptr, "gb") == 0) num *= 1024ULL * 1024ULL * 1024ULL;
    else if (strcmp(endptr, "K") == 0) num *= 1000ULL;
    else if (strcmp(endptr, "M") == 0) num *= 1000ULL * 1000ULL;
    else if (strcmp(endptr, "G") == 0) num *= 1000ULL * 1000ULL * 1000ULL;
    else {
        fprintf(stderr, "Error: Unknown suffix '%s'. Use KB, MB, GB, K, M, G.\n", endptr);
        exit(1);
    }
    return num;
}

void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --vsz <size>       Set Virtual Size (VSZ) [required]\n"
        "  --rss <size>       Set Resident Size (RSS) [default: same as VSZ]\n"
        "  -H, --hugepage     Use explicit 2MiB huge pages\n"
        "  -T, --thp          Advise Transparent Huge Pages\n"
        "  -h, --help         Show this help\n"
        "Examples:\n"
        "  %s --vsz 2GB --rss 512MB\n"
        "  %s --vsz 1GB --rss 1GB -H\n"
        "  %s --vsz 500MB --rss 100MB -T\n",
        prog, prog, prog, prog
    );
}

int main(int argc, char *argv[]) {
    int use_hugepage = 0;
    int use_thp = 0;
    const char *vsz_str = NULL, *rss_str = NULL;

    struct option long_options[] = {
        {"vsz",       required_argument, 0, 1000},
        {"rss",       required_argument, 0, 1001},
        {"hugepage",  no_argument,       0, 'H'},
        {"thp",       no_argument,       0, 'T'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "HT", long_options, NULL)) != -1) {
        switch (opt) {
            case 1000: vsz_str = optarg; break;
            case 1001: rss_str = optarg; break;
            case 'H': use_hugepage = 1; break;
            case 'T': use_thp = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (!vsz_str) {
        fprintf(stderr, "Error: --vsz is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    vsz_size = parse_size(vsz_str);
    rss_size = rss_str ? parse_size(rss_str) : vsz_size;

    if (rss_size > vsz_size) {
        fprintf(stderr, "Error: RSS (%zu) cannot be larger than VSZ (%zu).\n", rss_size, vsz_size);
        return 1;
    }

    if (vsz_size == 0 || rss_size == 0) {
        fprintf(stderr, "Error: Sizes must be > 0.\n");
        return 1;
    }

    printf("VSZ: %.2f MB | RSS: %.2f MB\n",
           vsz_size / (1024.0 * 1024.0),
           rss_size / (1024.0 * 1024.0));

    int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (use_hugepage) {
        mmap_flags |= MAP_HUGETLB | (21 << MAP_HUGE_SHIFT);  // 2MiB
        printf("Using explicit 2MiB huge pages...\n");
    }

    mem_base = mmap(NULL, vsz_size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
    if (mem_base == MAP_FAILED) {
        perror("mmap failed");
        if (use_hugepage) {
            printf("Hint: Pre-allocate huge pages:\n"
                   "  sudo sysctl vm.nr_hugepages=%ld\n", (vsz_size + (1ULL<<21) - 1) >> 21);
        }
        return 1;
    }

    // Apply THP advice if requested
    if (use_thp && madvise(mem_base, vsz_size, MADV_HUGEPAGE) == -1) {
        perror("madvise(MADV_HUGEPAGE) failed");
    }

    // Commit RSS by touching memory
    printf("Committing %.2f MB of physical memory (RSS)...\n", rss_size / (1024.0 * 1024.0));
    memset(mem_base, 0, rss_size);

    // Optional: Prevent swapping of RSS
    if (mlock(mem_base, rss_size) == -1) {
        perror("mlock failed (continuing without lock)");
    }

    printf("Done! Process PID: %d\n", getpid());
    printf("  VSZ: %.2f MB | RSS: ~%.2f MB\n",
           vsz_size / (1024.0 * 1024.0),
           rss_size / (1024.0 * 1024.0));
    printf("Use 'ps', 'top', or 'htop' to verify.\n");
    printf("Press Ctrl+C to free memory.\n");

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    while (1) pause();

    return 0;
}
