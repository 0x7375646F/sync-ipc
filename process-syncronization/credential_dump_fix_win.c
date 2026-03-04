/*
 * =============================================================================
 *  C2 SERVER — CREDENTIAL STORE WITH MUTEX PROTECTION (WINDOWS — FIXED)
 * =============================================================================
 *
 *  SCENARIO:
 *    Same as the problem version — 10 victim beacon threads simultaneously
 *    write stolen credentials to a shared credential store.
 *
 *  THE FIX (Windows Mutex via CreateMutex / WaitForSingleObject):
 *    A Windows Mutex object wraps the entire read-index → write-data →
 *    increment-count sequence as one protected critical section. Only one
 *    beacon thread can hold the mutex at a time. All others block at
 *    WaitForSingleObject and wait their turn, guaranteeing each credential
 *    gets its own unique slot with no overwrites.
 *
 *    Result: All 10 credentials are stored correctly.
 *    The C2 operator sees a complete, uncorrupted credential store.
 *
 *  COMPILE (MinGW):
 *    gcc -o credential_fix.exe credential_fix_windows.c
 *
 *  COMPILE (MSVC):
 *    cl credential_fix_windows.c
 *
 *  RUN:
 *    credential_fix.exe
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_VICTIMS     10
#define MAX_CREDENTIALS 20

/* ── Shared credential store ── */
typedef struct {
    char victim_id[32];
    char username[64];
    char password[64];
    char service[64];
} Credential;

Credential credential_store[MAX_CREDENTIALS];
int        credential_count = 0;

/* ── SOLUTION: Windows Mutex handle ── */
HANDLE store_mutex;

/* ── Simulated stolen credential data per victim ── */
const char *victims[]   = { "Victim-1","Victim-2","Victim-3","Victim-4","Victim-5",
                             "Victim-6","Victim-7","Victim-8","Victim-9","Victim-10" };
const char *usernames[] = { "jsmith","alee","mwong","dkumar","rfarid",
                             "eolsen","tbosch","lhung","cpatel","nrios" };
const char *passwords[] = { "P@ss1234","Qwerty!9","LetM3In","C0mplex#","S3cur3!!",
                             "Access99","H4ck3r00","L0gin$99","Root@123","Admin!00" };
const char *services[]  = { "VPN","RDP","SSH","Outlook","FTP",
                             "SFTP","Citrix","SAP","Oracle","Active Directory" };

/* ─────────────────────────────────────────────────────────────
 *  Beacon thread — mutex-protected credential write
 * ───────────────────────────────────────────────────────────── */
DWORD WINAPI beacon_thread(LPVOID arg) {
    int id = *(int *)arg;

    /* Simulate slight network jitter */
    Sleep(rand() % 5);

    printf("[%s] Beacon received | waiting for store mutex...\n", victims[id]);

    /*
     * FIX: Acquire the Windows Mutex before entering the critical section.
     *
     * WaitForSingleObject blocks this thread until the mutex is available.
     * Once it returns WAIT_OBJECT_0, this thread owns the mutex exclusively.
     * All other beacon threads are blocked at their own WaitForSingleObject
     * call until ReleaseMutex is called below.
     *
     * This guarantees that the read-index → write-data → increment sequence
     * is fully atomic from the perspective of all other threads.
     */
    DWORD wait_result = WaitForSingleObject(store_mutex, INFINITE);

    if (wait_result != WAIT_OBJECT_0) {
        printf("[%s] ERROR: Failed to acquire mutex (0x%lx)\n", victims[id], wait_result);
        return 1;
    }

    /* ── CRITICAL SECTION START ── */
    int slot = credential_count;   /* safe: we own the mutex */

    strncpy(credential_store[slot].victim_id, victims[id],   31);
    strncpy(credential_store[slot].username,  usernames[id], 63);
    strncpy(credential_store[slot].password,  passwords[id], 63);
    strncpy(credential_store[slot].service,   services[id],  63);

    credential_count = slot + 1;   /* safe: no other thread can read stale value */
    /* ── CRITICAL SECTION END ── */

    ReleaseMutex(store_mutex);     /* release so next beacon thread can proceed */

    printf("[%s] Credential stored at slot %d | count now %d\n",
           victims[id], slot, credential_count);

    return 0;
}

/* ─────────────────────────────────────────────────────────────
 *  Print credential store contents
 * ───────────────────────────────────────────────────────────── */
void dump_credentials(void) {
    printf("\n========================================================\n");
    printf("  C2 CREDENTIAL STORE | FINAL STATE (MUTEX PROTECTED)\n");
    printf("========================================================\n");
    printf("%-12s %-16s %-14s %-20s\n", "Victim", "Username", "Password", "Service");
    printf("--------------------------------------------------------\n");
    for (int i = 0; i < credential_count; i++) {
        printf("%-12s %-16s %-14s %-20s\n",
               credential_store[i].victim_id,
               credential_store[i].username,
               credential_store[i].password,
               credential_store[i].service);
    }
    printf("========================================================\n");
    printf("Total stored : %d / %d expected\n", credential_count, MAX_VICTIMS);
    if (credential_count == MAX_VICTIMS) {
        printf("SUCCESS: All credentials stored correctly. No data lost.\n");
    } else {
        printf("ERROR: %d credential(s) still missing!\n",
               MAX_VICTIMS - credential_count);
    }
    printf("========================================================\n\n");
}

/* ─────────────────────────────────────────────────────────────
 *  Main
 * ───────────────────────────────────────────────────────────── */
int main(void) {
    HANDLE threads[MAX_VICTIMS];
    int    ids[MAX_VICTIMS];

    srand((unsigned)GetTickCount());

    /*
     * Create an unnamed, unowned mutex.
     * NULL owner means no thread holds it initially — first thread to call
     * WaitForSingleObject will acquire it immediately.
     */
    store_mutex = CreateMutex(
        NULL,   /* default security: not inheritable by child processes */
        FALSE,  /* not initially owned — open for first caller */
        NULL    /* unnamed — only accessible within this process */
    );

    if (store_mutex == NULL) {
        printf("[C2 SERVER] FATAL: Failed to create mutex (error %lu)\n", GetLastError());
        return 1;
    }

    printf("\n[C2 SERVER] Starting credential collection | MUTEX PROTECTED\n");
    printf("[C2 SERVER] Spawning %d beacon threads simultaneously...\n\n", MAX_VICTIMS);

    for (int i = 0; i < MAX_VICTIMS; i++) {
        ids[i] = i;
        threads[i] = CreateThread(NULL, 0, beacon_thread, &ids[i], 0, NULL);
    }

    WaitForMultipleObjects(MAX_VICTIMS, threads, TRUE, INFINITE);

    for (int i = 0; i < MAX_VICTIMS; i++) {
        CloseHandle(threads[i]);
    }

    CloseHandle(store_mutex);   /* release the mutex kernel object */

    dump_credentials();
    return 0;
}
