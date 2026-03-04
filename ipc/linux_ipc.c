/*
 * =============================================================================
 *  C2 SERVER — IPC SHARED MEMORY (LINUX)
 * =============================================================================
 *
 *  SCENARIO:
 *    The Lazarus Group C2 server uses shared memory as the data pipeline
 *    between two services running on the same machine:
 *
 *      BEACON COLLECTOR  — receives stolen keylog data from victim beacons
 *                          and writes it into a shared memory buffer.
 *
 *      EXFIL RELAY       — reads from the same buffer and forwards the
 *                          records to the remote exfiltration server.
 *
 *    Shared memory (shm_open + mmap) is chosen because it is the fastest
 *    IPC mechanism available — data is exchanged directly in memory with
 *    no kernel copying overhead, making it ideal for high-throughput C2
 *    keylog data streams.
 *
 *  HOW IT WORKS:
 *    1. Beacon Collector creates the shared memory region and writes 10
 *       batches of keylog records from different victims into the buffer.
 *    2. Exfil Relay opens the same shared memory by name, reads all
 *       records, and confirms the total received matches what was written.
 *    3. Both processes exit cleanly and the shared memory is unlinked.
 *
 *  COMPILE:
 *    gcc -o ipc_linux ipc_linux.c -lrt
 *
 *  RUN:
 *    ./ipc_linux
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>

#define SHM_NAME        "/c2_keylog_buffer"
#define MAX_RECORDS      10
#define RECORDS_PER_BATCH 100

/* ── Keylog record written by each victim beacon ── */
typedef struct {
    char victim_id[32];
    char username[64];
    char service[32];
    char keydata[128];
    int  record_count;
} KeylogEntry;

/* ── Shared memory buffer layout ── */
typedef struct {
    int        total_written;              /* set by Beacon Collector  */
    int        total_read;                 /* set by Exfil Relay       */
    int        ready;                      /* 1 = data ready to read   */
    KeylogEntry entries[MAX_RECORDS];
} SharedBuffer;

/* ── Simulated victim data ── */
const char *victims[]   = { "Victim-1","Victim-2","Victim-3","Victim-4","Victim-5",
                             "Victim-6","Victim-7","Victim-8","Victim-9","Victim-10" };
const char *usernames[] = { "jsmith","alee","mwong","dkumar","rfarid",
                             "eolsen","tbosch","lhung","cpatel","nrios" };
const char *services[]  = { "Outlook","Chrome","SSH","RDP","VPN",
                             "SFTP","Citrix","SAP","Oracle","ActiveDirectory" };
const char *keydata[]   = { "password123!","letmein99","C0mplex#Key","S3cr3tP@ss",
                             "Admin!2024","Root@Server","Qwerty!900","Login$2024",
                             "P@ssw0rd!!","Welcome#01" };

/* ─────────────────────────────────────────────────────────────
 *  BEACON COLLECTOR — writes keylog data into shared memory
 * ───────────────────────────────────────────────────────────── */
void beacon_collector(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("\n[BEACON COLLECTOR] Creating shared memory buffer: %s\n", SHM_NAME);

    /* Create and size the shared memory region */
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(fd, sizeof(SharedBuffer));

    SharedBuffer *buf = (SharedBuffer *)mmap(
        NULL, sizeof(SharedBuffer),
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    /* Initialise buffer */
    memset(buf, 0, sizeof(SharedBuffer));
    buf->ready = 0;

    printf("[BEACON COLLECTOR] Writing %d victim keylog batches...\n\n", MAX_RECORDS);

    /* Write each victim's keylog entry into the buffer */
    for (int i = 0; i < MAX_RECORDS; i++) {
        strncpy(buf->entries[i].victim_id,   victims[i],   31);
        strncpy(buf->entries[i].username,    usernames[i], 63);
        strncpy(buf->entries[i].service,     services[i],  31);
        strncpy(buf->entries[i].keydata,     keydata[i],   127);
        buf->entries[i].record_count = RECORDS_PER_BATCH;
        buf->total_written += RECORDS_PER_BATCH;

        printf("  [+] Written: %-12s | %-16s | %-16s | %d records\n",
               victims[i], usernames[i], services[i], RECORDS_PER_BATCH);
    }

    /* Signal Exfil Relay that data is ready */
    buf->ready = 1;

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0
                   + (end.tv_nsec - start.tv_nsec) / 1e6;

    printf("\n[BEACON COLLECTOR] Done. Total records written : %d\n", buf->total_written);
    printf("[BEACON COLLECTOR] Write time                  : %.3f ms\n", elapsed);

    munmap(buf, sizeof(SharedBuffer));
}

/* ─────────────────────────────────────────────────────────────
 *  EXFIL RELAY — reads keylog data from shared memory
 * ───────────────────────────────────────────────────────────── */
void exfil_relay(void) {
    struct timespec start, end;

    /* Open existing shared memory */
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    SharedBuffer *buf = (SharedBuffer *)mmap(
        NULL, sizeof(SharedBuffer),
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    /* Wait for Beacon Collector to signal ready */
    printf("\n[EXFIL RELAY] Waiting for beacon data...\n");
    while (buf->ready == 0) {
        usleep(1000);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("[EXFIL RELAY] Data ready — reading from shared buffer...\n\n");

    /* Read and forward each entry */
    for (int i = 0; i < MAX_RECORDS; i++) {
        printf("  [>] Forwarding: %-12s | %-16s | %-16s | %d records\n",
               buf->entries[i].victim_id,
               buf->entries[i].username,
               buf->entries[i].service,
               buf->entries[i].record_count);
        buf->total_read += buf->entries[i].record_count;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0
                   + (end.tv_nsec - start.tv_nsec) / 1e6;

    printf("\n[EXFIL RELAY] Records read    : %d\n", buf->total_read);
    printf("[EXFIL RELAY] Records written : %d\n", buf->total_written);
    printf("[EXFIL RELAY] Read time       : %.3f ms\n", elapsed);

    if (buf->total_read == buf->total_written) {
        printf("[EXFIL RELAY] STATUS: OK — all records forwarded successfully.\n");
    } else {
        printf("[EXFIL RELAY] STATUS: MISMATCH — %d records missing!\n",
               buf->total_written - buf->total_read);
    }

    munmap(buf, sizeof(SharedBuffer));
}

/* ─────────────────────────────────────────────────────────────
 *  Main — fork into Beacon Collector and Exfil Relay
 * ───────────────────────────────────────────────────────────── */
int main(void) {
    struct timespec prog_start, prog_end;
    clock_gettime(CLOCK_MONOTONIC, &prog_start);

    printf("========================================================\n");
    printf("  C2 SERVER — IPC SHARED MEMORY PIPELINE (LINUX)\n");
    printf("========================================================\n");

    pid_t pid = fork();

    if (pid == 0) {
        /* Child: Exfil Relay */
        exfil_relay();
        exit(0);
    } else {
        /* Parent: Beacon Collector */
        beacon_collector();
        waitpid(pid, NULL, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &prog_end);
    double total = (prog_end.tv_sec - prog_start.tv_sec) * 1000.0
                 + (prog_end.tv_nsec - prog_start.tv_nsec) / 1e6;

    /* Final summary */
    int fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    SharedBuffer *buf = (SharedBuffer *)mmap(
        NULL, sizeof(SharedBuffer), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    printf("\n========================================================\n");
    printf("  PERFORMANCE METRICS\n");
    printf("========================================================\n");
    printf("  Total Records Written    : %d\n", buf->total_written);
    printf("  Total Records Read       : %d\n", buf->total_read);
    printf("  Total Execution Time     : %.3f ms\n", total);
    printf("  Throughput               : %.2f records/ms\n",
           buf->total_written / total);
    printf("  IPC Mechanism            : POSIX shm_open + mmap\n");
    printf("  Sync Mechanism           : ready flag (single producer/consumer)\n");
    printf("  Data Integrity           : %s\n",
           buf->total_read == buf->total_written ? "VERIFIED" : "MISMATCH");
    printf("========================================================\n\n");

    munmap(buf, sizeof(SharedBuffer));
    shm_unlink(SHM_NAME);
    return 0;
}
