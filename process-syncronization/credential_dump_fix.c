/*
 * =============================================================================
 *  C2 SERVER — CREDENTIAL STORE WITH MUTEX PROTECTION (LINUX — FIXED)
 * =============================================================================
 *
 *  SCENARIO:
 *    Same as the problem version — 10 victim beacon threads simultaneously
 *    write stolen credentials to a shared credential store.
 *
 *  THE FIX (pthread_mutex):
 *    A POSIX mutex (pthread_mutex_t) wraps the entire read-index → write-data
 *    → increment-count sequence as one atomic critical section. Only one beacon
 *    thread can enter this section at a time. All others block at the lock and
 *    wait their turn, guaranteeing each credential gets its own unique slot.
 *
 *    Result: All 10 credentials are stored correctly with no overwrites.
 *    The C2 operator sees a complete, uncorrupted credential store.
 *
 *  COMPILE:
 *    gcc -o credential_fix credential_fix_linux.c -lpthread
 *
 *  RUN:
 *    ./credential_fix
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

/* ── Shared credential store ── */
typedef struct {
    char victim_id[32];
    char username[64];
    char password[64];
    char service[64];
} Credential;

Credential       credential_store[MAX_CREDENTIALS];
int              credential_count = 0;

/* ── SOLUTION: mutex protects the critical section ── */
pthread_mutex_t  store_mutex = PTHREAD_MUTEX_INITIALIZER;

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
void *beacon_thread(void *arg) {
    int id = *(int *)arg;

    /* Simulate slight network jitter */
    usleep((rand() % 5000));

    printf("[%s] Beacon received — waiting for store lock...\n", victims[id]);

    /*
     * FIX: Lock the mutex before entering the critical section.
     *
     * Only one thread can hold this lock at a time. If Victim-4 holds it,
     * Victim-7 blocks here and waits. Once Victim-4 finishes writing and
     * releases the lock, Victim-7 acquires it and reads the updated count,
     * so it correctly targets the next available slot — not the same one.
     */
    pthread_mutex_lock(&store_mutex);

    /* ── CRITICAL SECTION START ── */
    int slot = credential_count;   /* safe: no other thread can be here */

    strncpy(credential_store[slot].victim_id, victims[id],   31);
    strncpy(credential_store[slot].username,  usernames[id], 63);
    strncpy(credential_store[slot].password,  passwords[id], 63);
    strncpy(credential_store[slot].service,   services[id],  63);

    credential_count = slot + 1;   /* safe: increment is now protected */
    /* ── CRITICAL SECTION END ── */

    pthread_mutex_unlock(&store_mutex);

    printf("[%s] Credential stored at slot %d — count now %d\n",
           victims[id], slot, credential_count);

    return NULL;
}

/* ─────────────────────────────────────────────────────────────
 *  Print credential store contents
 * ───────────────────────────────────────────────────────────── */
void dump_credentials(void) {
    printf("\n========================================================\n");
    printf("  C2 CREDENTIAL STORE — FINAL STATE (MUTEX PROTECTED)\n");
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
    pthread_t threads[MAX_VICTIMS];
    int ids[MAX_VICTIMS];

    srand((unsigned)time(NULL));

    printf("\n[C2 SERVER] Starting credential collection — MUTEX PROTECTED\n");
    printf("[C2 SERVER] Spawning %d beacon threads simultaneously...\n\n", MAX_VICTIMS);

    for (int i = 0; i < MAX_VICTIMS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, beacon_thread, &ids[i]);
    }

    for (int i = 0; i < MAX_VICTIMS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Destroy mutex after all threads complete */
    pthread_mutex_destroy(&store_mutex);

    dump_credentials();
    return 0;
}
