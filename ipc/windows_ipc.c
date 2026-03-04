/*
 * =============================================================================
 *  C2 SERVER — IPC SHARED MEMORY (WINDOWS)
 * =============================================================================
 *
 *  SCENARIO:
 *    Same C2 keylog pipeline as the Linux version. The Beacon Collector
 *    writes stolen keylog records from 10 victims into a shared memory
 *    buffer. The Exfil Relay reads from the same buffer and forwards the
 *    data to the remote exfiltration server.
 *
 *    Windows uses CreateFileMapping + MapViewOfFile instead of shm_open
 *    + mmap. The named file mapping "C2KeylogBuffer" acts as the shared
 *    region accessible across threads or processes.
 *
 *    An Event object ("C2DataReady") replaces the ready flag polling,
 *    letting the Exfil Relay thread block efficiently instead of spinning.
 *
 *  COMPILE (MinGW):
 *    gcc -o ipc_windows.exe ipc_windows.c
 *
 *  COMPILE (MSVC):
 *    cl ipc_windows.c
 *
 *  RUN:
 *    ipc_windows.exe
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SHM_NAME        "C2KeylogBuffer"
#define EVT_NAME        "C2DataReady"
#define MAX_RECORDS      10
#define RECORDS_PER_BATCH 100

/* ── Keylog record ── */
typedef struct {
    char victim_id[32];
    char username[64];
    char service[32];
    char keydata[128];
    int  record_count;
} KeylogEntry;

/* ── Shared memory buffer layout ── */
typedef struct {
    int        total_written;
    int        total_read;
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
 *  EXFIL RELAY THREAD — reads from shared memory
 * ───────────────────────────────────────────────────────────── */
DWORD WINAPI exfil_relay(LPVOID arg) {
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);

    /* Open the named event and wait for Beacon Collector to signal */
    HANDLE hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, EVT_NAME);

    printf("\n[EXFIL RELAY] Waiting for beacon data...\n");
    WaitForSingleObject(hEvent, INFINITE);  /* blocks until SetEvent() called */
    CloseHandle(hEvent);

    QueryPerformanceCounter(&start);

    /* Open the named shared memory */
    HANDLE hMap = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
    SharedBuffer *buf = (SharedBuffer *)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuffer));

    printf("[EXFIL RELAY] Data ready — reading from shared buffer...\n\n");

    for (int i = 0; i < MAX_RECORDS; i++) {
        printf("  [>] Forwarding: %-12s | %-16s | %-16s | %d records\n",
               buf->entries[i].victim_id,
               buf->entries[i].username,
               buf->entries[i].service,
               buf->entries[i].record_count);
        buf->total_read += buf->entries[i].record_count;
    }

    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart)
                   / freq.QuadPart * 1000.0;

    printf("\n[EXFIL RELAY] Records read    : %d\n", buf->total_read);
    printf("[EXFIL RELAY] Records written : %d\n", buf->total_written);
    printf("[EXFIL RELAY] Read time       : %.3f ms\n", elapsed);

    if (buf->total_read == buf->total_written) {
        printf("[EXFIL RELAY] STATUS: OK — all records forwarded successfully.\n");
    } else {
        printf("[EXFIL RELAY] STATUS: MISMATCH — %d records missing!\n",
               buf->total_written - buf->total_read);
    }

    UnmapViewOfFile(buf);
    CloseHandle(hMap);
    return 0;
}

/* ─────────────────────────────────────────────────────────────
 *  Main — Beacon Collector + spawn Exfil Relay thread
 * ───────────────────────────────────────────────────────────── */
int main(void) {
    LARGE_INTEGER freq, prog_start, prog_end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prog_start);

    printf("========================================================\n");
    printf("  C2 SERVER — IPC SHARED MEMORY PIPELINE (WINDOWS)\n");
    printf("========================================================\n");

    /* Create named shared memory */
    HANDLE hMap = CreateFileMapping(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(SharedBuffer), SHM_NAME);

    SharedBuffer *buf = (SharedBuffer *)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBuffer));
    memset(buf, 0, sizeof(SharedBuffer));

    /* Create named event (manual reset = FALSE, initially unsignalled) */
    HANDLE hEvent = CreateEvent(NULL, FALSE, FALSE, EVT_NAME);

    /* Spawn Exfil Relay thread — it will block on WaitForSingleObject */
    HANDLE hThread = CreateThread(NULL, 0, exfil_relay, NULL, 0, NULL);

    /* ── BEACON COLLECTOR ── */
    LARGE_INTEGER wstart, wend;
    QueryPerformanceCounter(&wstart);

    printf("\n[BEACON COLLECTOR] Writing %d victim keylog batches...\n\n", MAX_RECORDS);

    for (int i = 0; i < MAX_RECORDS; i++) {
        strncpy(buf->entries[i].victim_id,  victims[i],   31);
        strncpy(buf->entries[i].username,   usernames[i], 63);
        strncpy(buf->entries[i].service,    services[i],  31);
        strncpy(buf->entries[i].keydata,    keydata[i],   127);
        buf->entries[i].record_count = RECORDS_PER_BATCH;
        buf->total_written += RECORDS_PER_BATCH;

        printf("  [+] Written: %-12s | %-16s | %-16s | %d records\n",
               victims[i], usernames[i], services[i], RECORDS_PER_BATCH);
    }

    QueryPerformanceCounter(&wend);
    double write_ms = (double)(wend.QuadPart - wstart.QuadPart)
                    / freq.QuadPart * 1000.0;

    printf("\n[BEACON COLLECTOR] Done. Total records written : %d\n", buf->total_written);
    printf("[BEACON COLLECTOR] Write time                  : %.3f ms\n", write_ms);

    /* Signal Exfil Relay that data is ready */
    SetEvent(hEvent);

    /* Wait for Exfil Relay to finish */
    WaitForSingleObject(hThread, INFINITE);

    QueryPerformanceCounter(&prog_end);
    double total_ms = (double)(prog_end.QuadPart - prog_start.QuadPart)
                    / freq.QuadPart * 1000.0;

    printf("\n========================================================\n");
    printf("  PERFORMANCE METRICS\n");
    printf("========================================================\n");
    printf("  Total Records Written    : %d\n", buf->total_written);
    printf("  Total Records Read       : %d\n", buf->total_read);
    printf("  Total Execution Time     : %.3f ms\n", total_ms);
    printf("  Throughput               : %.2f records/ms\n",
           buf->total_written / total_ms);
    printf("  IPC Mechanism            : CreateFileMapping + MapViewOfFile\n");
    printf("  Sync Mechanism           : Named Event (SetEvent / WaitForSingleObject)\n");
    printf("  Data Integrity           : %s\n",
           buf->total_read == buf->total_written ? "VERIFIED" : "MISMATCH");
    printf("========================================================\n\n");

    CloseHandle(hThread);
    CloseHandle(hEvent);
    UnmapViewOfFile(buf);
    CloseHandle(hMap);
    return 0;
}
