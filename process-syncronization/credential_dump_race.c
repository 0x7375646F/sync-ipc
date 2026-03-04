/*
 * =============================================================================
 *  C2 SERVER — CREDENTIAL STORE RACE CONDITION (LINUX — NO SYNCHRONISATION)
 * =============================================================================
 *
 *  SCENARIO:
 *    The Lazarus Group C2 server receives stolen credentials from 10 compromised
 *    victim machines simultaneously. Each victim's beacon thread harvests a
 *    username/password pair and writes it into a shared credential store array.
 *
 *    The shared state is:
 *      - credential_count  : integer tracking how many credentials are stored
 *      - credential_store[] : array holding the actual stolen credentials
 *
 *  THE PROBLEM (Race Condition):
 *    Two threads (e.g. Victim-4 and Victim-7) both read credential_count = 5
 *    at the same moment. Both calculate that they should write to index 5.
 *    Victim-4 writes its credential to slot 5. Then Victim-7 also writes to
 *    slot 5, silently overwriting Victim-4's credential. Both then increment
 *    credential_count to 6 instead of 7.
 *
 *    Result: One stolen credential is permanently lost. The C2 operator sees
 *    only 9 entries and has no idea one victim's data was overwritten.
 *
 *  COMPILE:
 *    gcc -o credential_race credential_race_linux.c -lpthread
 *
 *  RUN:
 *    ./credential_race
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

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
 *  credentials back to the C2 server.
 * ───────────────────────────────────────────────────────────── */
void *beacon_thread(void *arg) {
    int id = *(int *)arg;

    /* Simulate slight network jitter so threads arrive nearly simultaneously */
    usleep((rand() % 5000));  /* 0–5 ms random delay */

    printf("[%s] Beacon received — writing credential to store...\n", victims[id]);

    /*
     * BUG: Classic read-modify-write race condition.
     *
     * Step 1: Read current count (e.g. both Victim-4 and Victim-7 read 5)
     * Step 2: Use count as the write index (both target slot 5)
     * Step 3: Write credential to slot 5 (Victim-7 overwrites Victim-4)
     * Step 4: Increment count (both set count to 6, not 7)
     *
     * No mutex means no atomicity guarantee across these steps.
     */
    int slot = credential_count;                        /* READ  (not atomic) */

    /* Simulate the thread being preempted right here — widens the race window */
    usleep(1000);

    strncpy(credential_store[slot].victim_id, victims[id],   31);
    strncpy(credential_store[slot].username,  usernames[id], 63);
    strncpy(credential_store[slot].password,  passwords[id], 63);
    strncpy(credential_store[slot].service,   services[id],  63);

    credential_count = slot + 1;                        /* WRITE (not atomic) */

    printf("[%s] Credential stored at slot %d — count now %d\n",
           victims[id], slot, credential_count);

    return NULL;
}

/* ─────────────────────────────────────────────────────────────
 *  Print credential store contents
 * ───────────────────────────────────────────────────────────── */
void dump_credentials(void) {
    printf("\n========================================================\n");
    printf("  C2 CREDENTIAL STORE — FINAL STATE\n");
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
    pthread_t threads[MAX_VICTIMS];
    int ids[MAX_VICTIMS];

    srand((unsigned)time(NULL));

    printf("\n[C2 SERVER] Starting credential collection — NO synchronisation\n");
    printf("[C2 SERVER] Spawning %d beacon threads simultaneously...\n\n", MAX_VICTIMS);

    /* Spawn all beacon threads at once to maximise race window */
    for (int i = 0; i < MAX_VICTIMS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, beacon_thread, &ids[i]);
    }

    for (int i = 0; i < MAX_VICTIMS; i++) {
        pthread_join(threads[i], NULL);
    }

    dump_credentials();
    return 0;
}
