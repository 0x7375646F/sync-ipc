/*
 * =============================================================================
 *  C2 SERVER — CREDENTIAL STORE RACE CONDITION (WINDOWS — NO SYNCHRONISATION)
 * =============================================================================
 *
 *  SCENARIO:
 *    The Lazarus Group C2 server receives stolen credentials from 10 compromised
 *    victim machines simultaneously. Each victim's beacon thread harvests a
 *    username/password pair and writes it into a shared credential store array.
 *
 *  THE PROBLEM (Race Condition):
 *    Two threads both read credential_count = 5 at the same moment.
 *    Both calculate they should write to index 5. Victim-4 writes first.
 *    Victim-7 then overwrites Victim-4's credential in slot 5.
 *    Both increment count to 6 instead of 7.
 *
 *    Result: One stolen credential is permanently lost with no error reported.
 *    The C2 operator sees 9 entries instead of 10.
 *
 *  COMPILE (MinGW):
 *    gcc -o credential_race.exe credential_race_windows.c
 *
 *  COMPILE (MSVC):
 *    cl credential_race_windows.c
 *
 *  RUN:
 *    credential_race.exe
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_VICTIMS     10
#define MAX_CREDENTIALS 20

/* ── Shared credential store (accessed by all beacon threads) ── */
typedef struct {
    char victim_id[32];
    char username[64];
    char password[64];
    char service[64];
} Credential;

Credential credential_store[MAX_CREDENTIALS];
int        credential_count = 0;   /* <-- shared counter, NO protection */

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
 *  Beacon thread — simulates a victim machine sending stolen
 *  credentials back to the C2 server (no synchronisation).
 * ───────────────────────────────────────────────────────────── */
DWORD WINAPI beacon_thread(LPVOID arg) {
    int id = *(int *)arg;

    /* Simulate slight network jitter */
    Sleep(rand() % 5);   /* 0–4 ms */

    printf("[%s] Beacon received | writing credential to store...\n", victims[id]);

    /*
     * BUG: Classic read-modify-write race condition.
     *
     * Step 1: Read credential_count (two threads may read the same value)
     * Step 2: Both calculate the same slot index
     * Step 3: One thread overwrites the other's credential
     * Step 4: Both set credential_count to slot+1 instead of slot+2
     */
    int slot = credential_count;     /* READ  — not atomic on Windows either */

    /* Simulate preemption between read and write to widen the race window */
    Sleep(1);

    strncpy(credential_store[slot].victim_id, victims[id],   31);
    strncpy(credential_store[slot].username,  usernames[id], 63);
    strncpy(credential_store[slot].password,  passwords[id], 63);
    strncpy(credential_store[slot].service,   services[id],  63);

    credential_count = slot + 1;     /* WRITE — not atomic */

    printf("[%s] Credential stored at slot %d | count now %d\n",
           victims[id], slot, credential_count);

    return 0;
}

/* ─────────────────────────────────────────────────────────────
 *  Print credential store contents
 * ───────────────────────────────────────────────────────────── */
void dump_credentials(void) {
    printf("\n========================================================\n");
    printf("  C2 CREDENTIAL STORE | FINAL STATE\n");
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
    if (credential_count < MAX_VICTIMS) {
        printf("WARNING: %d credential(s) LOST due to race condition!\n",
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

    printf("\n[C2 SERVER] Starting credential collection | NO synchronisation\n");
    printf("[C2 SERVER] Spawning %d beacon threads simultaneously...\n\n", MAX_VICTIMS);

    for (int i = 0; i < MAX_VICTIMS; i++) {
        ids[i] = i;
        threads[i] = CreateThread(
            NULL,           /* default security attributes */
            0,              /* default stack size */
            beacon_thread,  /* thread function */
            &ids[i],        /* argument */
            0,              /* run immediately */
            NULL            /* thread ID not needed */
        );
    }

    /* Wait for all beacon threads to finish */
    WaitForMultipleObjects(MAX_VICTIMS, threads, TRUE, INFINITE);

    for (int i = 0; i < MAX_VICTIMS; i++) {
        CloseHandle(threads[i]);
    }

    dump_credentials();
    return 0;
}
