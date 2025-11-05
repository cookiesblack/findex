/* File Integrity Checker - With Filter Options */
#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>

/* ANSI color codes - only used when output is to terminal */
static int USE_COLOR = 0;
#define C_RESET   (USE_COLOR ? "\033[0m"  : "")
#define C_GREEN   (USE_COLOR ? "\033[32m" : "")
#define C_RED     (USE_COLOR ? "\033[31m" : "")
#define C_YELLOW  (USE_COLOR ? "\033[33m" : "")
#define C_CYAN    (USE_COLOR ? "\033[36m" : "")

/* Filter flags */
#define FILTER_NEW      (1 << 0)
#define FILTER_MODIFIED (1 << 1)
#define FILTER_DELETED  (1 << 2)
#define FILTER_ALL      (FILTER_NEW | FILTER_MODIFIED | FILTER_DELETED)

/* ========================== CRC32 Implementation ========================== */
static uint32_t crc_table[256];
static int crc_init_done = 0;

static void crc32_init(void) {
    uint32_t poly = 0xEDB88320U;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (poly ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_init_done = 1;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *buf, size_t len) {
    if (!crc_init_done) crc32_init();
    crc ^= 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFFU] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t crc32_file_fast(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    
    unsigned char buf[256 * 1024];
    uint32_t crc = 0;
    ssize_t n;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        crc = crc32_update(crc, buf, (size_t)n);
    }
    close(fd);
    return crc;
}

/* ========================== Media File Detection ========================== */
static const char *media_exts[] = {
    "jpg", "jpeg", "png", "gif", "svg", "webp", "ico",
    "mp4", "mp3", "wav", "ogg", "pdf", "webm", NULL
};

static int is_media_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    ext++;
    
    for (const char **m = media_exts; *m; ++m) {
        if (strcasecmp(ext, *m) == 0) return 1;
    }
    return 0;
}

/* Check if path is media file (works with full paths) */
static int is_media_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    
    char ext[16];
    strncpy(ext, dot + 1, 15);
    ext[15] = '\0';
    
    for (char *p = ext; *p; ++p)
        *p = tolower((unsigned char)*p);
    
    for (const char **m = media_exts; *m; ++m) {
        if (strcmp(ext, *m) == 0) return 1;
    }
    return 0;
}

/* ========================== Entry List (Thread-Safe) ========================== */
typedef struct {
    char *path;
    off_t size;
    time_t mtime;
    uint32_t crc32;
} Entry;

typedef struct {
    Entry *items;
    size_t len;
    size_t cap;
    pthread_mutex_t lock;
} EntryList;

static void el_init(EntryList *el) {
    el->items = NULL;
    el->len = 0;
    el->cap = 0;
    pthread_mutex_init(&el->lock, NULL);
}

static void el_push(EntryList *el, const char *path, off_t size, time_t mtime, uint32_t crc) {
    pthread_mutex_lock(&el->lock);
    
    if (el->len == el->cap) {
        el->cap = el->cap ? el->cap * 2 : 2048;
        el->items = realloc(el->items, el->cap * sizeof(Entry));
        if (!el->items) {
            perror("realloc");
            exit(1);
        }
    }
    
    el->items[el->len].path = strdup(path);
    el->items[el->len].size = size;
    el->items[el->len].mtime = mtime;
    el->items[el->len].crc32 = crc;
    el->len++;
    
    pthread_mutex_unlock(&el->lock);
}

static void el_free(EntryList *el) {
    if (!el) return;
    for (size_t i = 0; i < el->len; i++)
        free(el->items[i].path);
    free(el->items);
    pthread_mutex_destroy(&el->lock);
}

/* Filter out media entries from list */
static void filter_out_media(EntryList *list) {
    size_t j = 0;
    for (size_t i = 0; i < list->len; i++) {
        if (is_media_path(list->items[i].path)) {
            free(list->items[i].path);
            continue;
        }
        if (i != j)
            list->items[j] = list->items[i];
        j++;
    }
    list->len = j;
}

/* ========================== Path Queue (Producer-Consumer) ========================== */
typedef struct {
    char **items;
    size_t head;
    size_t tail;
    size_t cap;
    int closed;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} PathQueue;

static void pq_init(PathQueue *q, size_t cap) {
    q->items = malloc(sizeof(char*) * cap);
    q->head = q->tail = 0;
    q->cap = cap;
    q->closed = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void pq_close(PathQueue *q) {
    pthread_mutex_lock(&q->lock);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

static void pq_free(PathQueue *q) {
    if (!q) return;
    for (size_t i = q->head; i != q->tail; i = (i + 1) % q->cap)
        free(q->items[i]);
    free(q->items);
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static void pq_push(PathQueue *q, const char *path) {
    pthread_mutex_lock(&q->lock);
    while (((q->tail + 1) % q->cap) == q->head)
        pthread_cond_wait(&q->not_full, &q->lock);
    
    q->items[q->tail] = strdup(path);
    q->tail = (q->tail + 1) % q->cap;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

static int pq_pop(PathQueue *q, char **out) {
    pthread_mutex_lock(&q->lock);
    while (q->head == q->tail && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->lock);
    
    if (q->head == q->tail && q->closed) {
        pthread_mutex_unlock(&q->lock);
        return 0;
    }
    
    *out = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return 1;
}

/* ========================== Progress Tracking ========================== */
static size_t total_files = 0;
static size_t processed_files = 0;
static int progress_running = 0;
static pthread_mutex_t progress_lock = PTHREAD_MUTEX_INITIALIZER;

static void *spinner_thread(void *arg) {
    (void)arg;
    const char glyphs[] = "|/-\\";
    int gi = 0;
    
    while (1) {
        pthread_mutex_lock(&progress_lock);
        int running = progress_running;
        size_t done = processed_files;
        size_t tot = total_files;
        pthread_mutex_unlock(&progress_lock);
        
        if (!running) break;
        
        double pct = (tot > 0) ? ((double)done / tot * 100.0) : 0.0;
        fprintf(stdout, "\r%s[%c]%s %5.1f%% (%zu/%zu)", 
                C_CYAN, glyphs[gi], C_RESET, pct, done, tot);
        fflush(stdout);
        
        gi = (gi + 1) & 3;
        struct timespec ts = {0, 250 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ========================== File System Traversal ========================== */
static void count_files(const char *root, int include_media) {
    DIR *d = opendir(root);
    if (!d) return;
    
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        
        /* Skip VCS directories */
        if (ent->d_name[0] == '.') {
            if (strcasecmp(ent->d_name, ".git") == 0 ||
                strcasecmp(ent->d_name, ".svn") == 0 ||
                strcasecmp(ent->d_name, ".hg") == 0)
                continue;
        }
        
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
        
        struct stat st;
        if (lstat(path, &st) == -1) continue;
        if (S_ISLNK(st.st_mode)) continue;
        
        if (S_ISDIR(st.st_mode)) {
            count_files(path, include_media);
        } else if (S_ISREG(st.st_mode)) {
            if (!include_media && is_media_file(ent->d_name))
                continue;
            total_files++;
        }
    }
    closedir(d);
}

static void walk_and_enqueue(PathQueue *q, const char *root, int include_media) {
    DIR *d = opendir(root);
    if (!d) return;
    
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        
        /* Skip VCS directories */
        if (ent->d_name[0] == '.') {
            if (strcasecmp(ent->d_name, ".git") == 0 ||
                strcasecmp(ent->d_name, ".svn") == 0 ||
                strcasecmp(ent->d_name, ".hg") == 0)
                continue;
        }
        
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
        
        struct stat st;
        if (lstat(path, &st) == -1) continue;
        if (S_ISLNK(st.st_mode)) continue;
        
        if (S_ISDIR(st.st_mode)) {
            walk_and_enqueue(q, path, include_media);
        } else if (S_ISREG(st.st_mode)) {
            if (!include_media && is_media_file(ent->d_name))
                continue;
            pq_push(q, path);
        }
    }
    closedir(d);
}

/* ========================== Worker Thread ========================== */
typedef struct {
    PathQueue *q;
    EntryList *out;
} WorkerArgs;

static void *worker_fn(void *arg) {
    WorkerArgs *wa = (WorkerArgs*)arg;
    char *path;
    
    while (pq_pop(wa->q, &path)) {
        struct stat st;
        if (lstat(path, &st) == -1) {
            free(path);
            continue;
        }
        
        uint32_t crc = crc32_file_fast(path);
        el_push(wa->out, path, st.st_size, st.st_mtime, crc);
        free(path);
        
        pthread_mutex_lock(&progress_lock);
        processed_files++;
        pthread_mutex_unlock(&progress_lock);
    }
    return NULL;
}

/* ========================== Database I/O ========================== */
static int write_db(const char *dbfile, const EntryList *el) {
    FILE *f = fopen(dbfile, "w");
    if (!f) {
        perror("fopen");
        return -1;
    }
    
    fprintf(f, "path,size,mtime,crc32\n");
    for (size_t i = 0; i < el->len; i++) {
        fprintf(f, "%s,%lld,%ld,0x%08x\n",
                el->items[i].path,
                (long long)el->items[i].size,
                (long)el->items[i].mtime,
                el->items[i].crc32);
    }
    fclose(f);
    return 0;
}

static int cmp_entry_by_path(const void *a, const void *b) {
    return strcmp(((const Entry*)a)->path, ((const Entry*)b)->path);
}

static int read_db(const char *dbfile, EntryList *out) {
    FILE *f = fopen(dbfile, "r");
    if (!f) return -1;
    
    el_init(out);
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    
    /* Skip header - check return value to suppress warning */
    n = getline(&line, &cap, f);
    (void)n; /* Suppress unused variable warning */
    
    while ((n = getline(&line, &cap, f)) != -1) {
        if (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r'))
            line[--n] = '\0';
        
        char *p = line, *tok;
        
        tok = strsep(&p, ",");
        if (!tok) continue;
        char *path = strdup(tok);
        
        tok = strsep(&p, ",");
        if (!tok) { free(path); continue; }
        long long size = atoll(tok);
        
        tok = strsep(&p, ",");
        if (!tok) { free(path); continue; }
        long mtime = atol(tok);
        
        tok = strsep(&p, ",");
        if (!tok) { free(path); continue; }
        unsigned int crc = 0;
        sscanf(tok, "0x%x", &crc);
        
        el_push(out, path, (off_t)size, (time_t)mtime, (uint32_t)crc);
        free(path);
    }
    
    free(line);
    fclose(f);
    qsort(out->items, out->len, sizeof(Entry), cmp_entry_by_path);
    return 0;
}

/* ========================== Diff Reporting ========================== */
static void do_diff(const EntryList *oldL, const EntryList *curL, int filter, FILE *logfile) {
    size_t i = 0, j = 0;
    size_t newc = 0, modc = 0, delc = 0;
    
    /* Write log header with timestamp */
    if (logfile) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(logfile, "\n========================================\n");
        fprintf(logfile, "Check performed: %s\n", timestamp);
        fprintf(logfile, "========================================\n");
    }
    
    while (i < oldL->len || j < curL->len) {
        int cmp;
        if (i >= oldL->len)
            cmp = 1;
        else if (j >= curL->len)
            cmp = -1;
        else
            cmp = strcmp(oldL->items[i].path, curL->items[j].path);
        
        if (cmp == 0) {
            const Entry *o = &oldL->items[i];
            const Entry *c = &curL->items[j];
            if (o->size != c->size || o->mtime != c->mtime || o->crc32 != c->crc32) {
                if (filter & FILTER_MODIFIED) {
                    printf("%s! MODIFIED:%s %s\n", C_YELLOW, C_RESET, c->path);
                }
                if (logfile) {
                    fprintf(logfile, "! MODIFIED: %s\n", c->path);
                    fprintf(logfile, "  Old: size=%lld mtime=%ld crc=0x%08x\n", 
                            (long long)o->size, (long)o->mtime, o->crc32);
                    fprintf(logfile, "  New: size=%lld mtime=%ld crc=0x%08x\n",
                            (long long)c->size, (long)c->mtime, c->crc32);
                }
                modc++;
            }
            i++;
            j++;
        } else if (cmp < 0) {
            if (filter & FILTER_DELETED) {
                printf("%s- DELETED:%s %s\n", C_RED, C_RESET, oldL->items[i].path);
            }
            if (logfile) {
                fprintf(logfile, "- DELETED: %s\n", oldL->items[i].path);
            }
            delc++;
            i++;
        } else {
            if (filter & FILTER_NEW) {
                printf("%s+ NEW:%s %s\n", C_GREEN, C_RESET, curL->items[j].path);
            }
            if (logfile) {
                fprintf(logfile, "+ NEW: %s (size=%lld crc=0x%08x)\n",
                        curL->items[j].path,
                        (long long)curL->items[j].size,
                        curL->items[j].crc32);
            }
            newc++;
            j++;
        }
    }
    
    printf("%s----------------------------------------%s\n", C_CYAN, C_RESET);
    printf("Summary: %s+%zu new%s, %s!%zu modified%s, %s-%zu deleted%s\n",
           C_GREEN, newc, C_RESET,
           C_YELLOW, modc, C_RESET,
           C_RED, delc, C_RESET);
    
    if (logfile) {
        fprintf(logfile, "----------------------------------------\n");
        fprintf(logfile, "Summary: +%zu new, !%zu modified, -%zu deleted\n",
                newc, modc, delc);
        fflush(logfile);
    }
}

/* ========================== Main Program ========================== */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --index   [--db FILE] [--threads N] [--exclude-media]\n"
        "  %s --check   [--db FILE] [--threads N] [--filter FILTER] [--log FILE]\n"
        "  %s --reindex [--db FILE] [--threads N] [--exclude-media]\n\n"
        "Options:\n"
        "  --index          Create initial file database\n"
        "  --check          Compare current files against database\n"
        "  --reindex        Rebuild the database (same as --index)\n"
        "  --db FILE        Database file (default: findex.db)\n"
        "  --threads N      Number of worker threads (default: CPU count)\n"
        "  --exclude-media  Exclude media files from indexing (for --index/--reindex)\n"
        "  --log FILE       Log file for check results (default: findex.log)\n"
        "  --filter FILTER  Show only specified changes (default: created)\n"
        "                   Options: new, modified, deleted, all, created\n"
        "                   - new:      Show only new files\n"
        "                   - modified: Show only modified files\n"
        "                   - deleted:  Show only deleted files\n"
        "                   - all:      Show all changes\n"
        "                   - created:  Show new and modified files (default)\n\n"
        "Note: --check always uses the same media inclusion as the database was created with.\n",
        prog, prog, prog);
}

int main(int argc, char **argv) {
    const char *mode = NULL;
    const char *dbfile = "findex.db";
    const char *logfile_path = "findex.log";
    const char *filter_str = "created";
    int exclude_media = 0;
    int threads = 0;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--index") == 0)
            mode = "index";
        else if (strcmp(argv[i], "--check") == 0)
            mode = "check";
        else if (strcmp(argv[i], "--reindex") == 0)
            mode = "reindex";
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc)
            dbfile = argv[++i];
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc)
            logfile_path = argv[++i];
        else if (strcmp(argv[i], "--exclude-media") == 0)
            exclude_media = 1;
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc)
            filter_str = argv[++i];
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            threads = atoi(argv[++i]);
        else {
            usage(argv[0]);
            return 2;
        }
    }
    
    if (!mode) {
        usage(argv[0]);
        return 2;
    }
    
    /* Parse filter option */
    int filter = 0;
    if (strcmp(filter_str, "new") == 0)
        filter = FILTER_NEW;
    else if (strcmp(filter_str, "modified") == 0)
        filter = FILTER_MODIFIED;
    else if (strcmp(filter_str, "deleted") == 0)
        filter = FILTER_DELETED;
    else if (strcmp(filter_str, "all") == 0)
        filter = FILTER_ALL;
    else if (strcmp(filter_str, "created") == 0)
        filter = FILTER_NEW | FILTER_MODIFIED;
    else {
        fprintf(stderr, "Invalid filter: %s\n", filter_str);
        fprintf(stderr, "Valid filters: new, modified, deleted, all, created\n");
        return 2;
    }
    
    USE_COLOR = isatty(STDOUT_FILENO);
    
    if (threads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        threads = (n > 0) ? (int)n : 4;
    }
    
    /* Handle --index or --reindex */
    if (strcmp(mode, "index") == 0 || strcmp(mode, "reindex") == 0) {
        int include_media = !exclude_media;
        printf("%sIndexing (fast, %d threads, media %s)...%s\n", 
               C_CYAN, threads, include_media ? "included" : "excluded", C_RESET);
        
        EntryList cur;
        el_init(&cur);
        
        /* Count files for progress */
        total_files = processed_files = 0;
        count_files(".", include_media);
        
        /* Set up worker threads */
        PathQueue q;
        pq_init(&q, 8192);
        pthread_t *t = malloc(sizeof(pthread_t) * threads);
        WorkerArgs wa = {.q = &q, .out = &cur};
        
        for (int i = 0; i < threads; i++)
            pthread_create(&t[i], NULL, worker_fn, &wa);
        
        /* Start progress spinner */
        pthread_t spn;
        progress_running = 1;
        pthread_create(&spn, NULL, spinner_thread, NULL);
        
        /* Walk filesystem and enqueue work */
        walk_and_enqueue(&q, ".", include_media);
        pq_close(&q);
        
        /* Wait for completion */
        for (int i = 0; i < threads; i++)
            pthread_join(t[i], NULL);
        
        pthread_mutex_lock(&progress_lock);
        progress_running = 0;
        pthread_mutex_unlock(&progress_lock);
        pthread_join(spn, NULL);
        
        printf("\nCollected %zu files (media %s)\n", 
               cur.len, include_media ? "included" : "excluded");
        
        /* Sort and save */
        qsort(cur.items, cur.len, sizeof(Entry), cmp_entry_by_path);
        write_db(dbfile, &cur);
        printf("Saved to %s\n", dbfile);
        
        /* Cleanup */
        pq_free(&q);
        el_free(&cur);
        free(t);
        return 0;
    }
    
    /* Handle --check */
    if (strcmp(mode, "check") == 0) {
        if (exclude_media) {
            fprintf(stderr, 
                    "%sWarning:%s --exclude-media is ignored for --check mode.\n"
                    "The check will use the same media inclusion as when the database was created.\n",
                    C_YELLOW, C_RESET);
        }
        
        EntryList oldL;
        if (read_db(dbfile, &oldL) != 0) {
            fprintf(stderr, "Cannot read DB: %s\n", dbfile);
            return 1;
        }
        
        /* Open log file */
        FILE *logfile = fopen(logfile_path, "a");
        if (!logfile) {
            fprintf(stderr, "%sWarning:%s Cannot open log file %s: %s\n",
                    C_YELLOW, C_RESET, logfile_path, strerror(errno));
            fprintf(stderr, "Continuing without logging...\n");
        } else {
            printf("Logging to: %s\n", logfile_path);
        }
        
        printf("%sScanning current filesystem (fast, %d threads, filter: %s)...%s\n",
               C_CYAN, threads, filter_str, C_RESET);
        
        EntryList cur;
        el_init(&cur);
        
        /* Scan ALL files (including media) first */
        total_files = processed_files = 0;
        count_files(".", 1);
        
        /* Set up worker threads */
        PathQueue q;
        pq_init(&q, 8192);
        WorkerArgs wa = {.q = &q, .out = &cur};
        pthread_t *t = malloc(sizeof(pthread_t) * threads);
        
        for (int i = 0; i < threads; i++)
            pthread_create(&t[i], NULL, worker_fn, &wa);
        
        /* Start progress spinner */
        pthread_t spn;
        progress_running = 1;
        pthread_create(&spn, NULL, spinner_thread, NULL);
        
        /* Walk filesystem and enqueue ALL files */
        walk_and_enqueue(&q, ".", 1);
        pq_close(&q);
        
        /* Wait for completion */
        for (int i = 0; i < threads; i++)
            pthread_join(t[i], NULL);
        
        pthread_mutex_lock(&progress_lock);
        progress_running = 0;
        pthread_mutex_unlock(&progress_lock);
        pthread_join(spn, NULL);
        
        printf("\nComparing with database...\n");
        
        /* Sort current files */
        qsort(cur.items, cur.len, sizeof(Entry), cmp_entry_by_path);
        
        /* Filter BOTH lists to exclude media for comparison */
        size_t old_count = oldL.len;
        size_t cur_count = cur.len;
        filter_out_media(&oldL);
        filter_out_media(&cur);
        
        printf("Database: %zu files (%zu media filtered out)\n", 
               oldL.len, old_count - oldL.len);
        printf("Current:  %zu files (%zu media filtered out)\n", 
               cur.len, cur_count - cur.len);
        
        /* Compare and report differences with filter and logging */
        do_diff(&oldL, &cur, filter, logfile);
        
        /* Close log file */
        if (logfile) {
            fclose(logfile);
        }
        
        /* Cleanup */
        pq_free(&q);
        el_free(&oldL);
        el_free(&cur);
        free(t);
        return 0;
    }
    
    usage(argv[0]);
    return 2;
}