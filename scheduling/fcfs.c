/*
 * ============================================================
 *  C2 Server FCFS Scheduler  -  Cross-Platform (Linux/Windows)
 *  Compile on Linux : gcc -o fcfs fcfs.c
 *  Compile on Windows: gcc -o fcfs.exe fcfs.c
 * ============================================================
 */

/* ── Platform detection ─────────────────────────────────────── */
#ifdef _WIN32
    #define PLATFORM_NAME "Windows"
    #include <windows.h>          /* CreateProcess, WaitForSingleObject, Sleep */
    #include <process.h>          /* _getpid                                    */
#else
    #define PLATFORM_NAME "Linux"
    #include <unistd.h>           /* fork, sleep                                */
    #include <sys/wait.h>         /* waitpid                                    */
    #include <sys/time.h>         /* gettimeofday                               */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Data structures ────────────────────────────────────────── */
typedef struct {
    int    pid;
    char   task_name[50];
    int    arrival_time;
    int    burst_time;
    double calc_start_time;
    double calc_completion_time;
    double calc_turnaround_time;
    double calc_waiting_time;
    double calc_response_time;
} Process;

/* ══════════════════════════════════════════════════════════════
 *  SECTION 1 – Timing helper  (platform-specific)
 * ══════════════════════════════════════════════════════════════ */
double get_wall_time(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* 100-nanosecond intervals since 1601 → convert to seconds */
    return (double)uli.QuadPart / 10000000.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
#endif
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 2 – CPU-intensive busy-work (pure C, portable)
 * ══════════════════════════════════════════════════════════════ */
void cpu_intensive_work(int seconds) {
    double target = get_wall_time() + seconds;
    volatile double result = 0.0;
    while (get_wall_time() < target) {
        for (volatile long i = 0; i < 10000000L; i++) {
            result += i * 3.14159;
            result  = result / 2.71828;
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 3 – Process execution  (platform-specific)
 * ══════════════════════════════════════════════════════════════ */
void execute_process(Process *p) {

#ifdef _WIN32
    /* ── Windows: spawn a child that re-runs this exe with a
     *    special hidden flag "-work <seconds>"                  */
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    char cmdline[128];

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* Build: fcfs.exe -work <burst> */
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -work %d",
             /* argv[0] equivalent: use GetModuleFileName */
             "", p->burst_time);

    /* Get our own exe path */
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -work %d",
             exe_path, p->burst_time);

    if (CreateProcessA(
            NULL,           /* use cmdline */
            cmdline,        /* command line */
            NULL, NULL,     /* process/thread security */
            FALSE,          /* no handle inheritance */
            CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        /* Fallback: just run work inline */
        cpu_intensive_work(p->burst_time);
    }

#else
    /* ── Linux/macOS: classic fork/exec                        */
    pid_t child = fork();
    if (child == 0) {
        cpu_intensive_work(p->burst_time);
        exit(0);
    } else {
        int status;
        waitpid(child, &status, 0);
    }
#endif
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 4 – Sleep helper  (platform-specific)
 * ══════════════════════════════════════════════════════════════ */
void portable_sleep(int seconds) {
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000));   /* Sleep takes milliseconds */
#else
    sleep((unsigned int)seconds);
#endif
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 5 – FCFS algorithm  (pure C, identical on both OS)
 * ══════════════════════════════════════════════════════════════ */
void calculate_fcfs_times(Process p[], int n) {
    double current_time = 0;
    for (int i = 0; i < n; i++) {
        if (current_time < p[i].arrival_time)
            current_time = p[i].arrival_time;

        p[i].calc_start_time       = current_time;
        p[i].calc_completion_time  = current_time + p[i].burst_time;
        p[i].calc_turnaround_time  = p[i].calc_completion_time - p[i].arrival_time;
        p[i].calc_waiting_time     = p[i].calc_start_time - p[i].arrival_time;
        p[i].calc_response_time    = p[i].calc_start_time - p[i].arrival_time;

        current_time = p[i].calc_completion_time;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 6 – Output helpers
 * ══════════════════════════════════════════════════════════════ */
void print_table(Process p[], int n) {
    printf("\n");
    printf("========================================================================================\n");
    printf("                     FCFS SCHEDULING - PROCESS EXECUTION TABLE\n");
    printf("========================================================================================\n");
    printf("PID  Task Name                    AT    BT    ST      CT      TAT     WT      RT\n");
    printf("----------------------------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("P%-3d %-26s %-5d %-5d %-7.2f %-7.2f %-7.2f %-7.2f %-7.2f\n",
               p[i].pid, p[i].task_name,
               p[i].arrival_time, p[i].burst_time,
               p[i].calc_start_time, p[i].calc_completion_time,
               p[i].calc_turnaround_time, p[i].calc_waiting_time,
               p[i].calc_response_time);
    }
    printf("========================================================================================\n");
    printf("AT=Arrival Time  BT=Burst Time  ST=Start Time  CT=Completion Time\n");
    printf("TAT=Turnaround Time  WT=Waiting Time  RT=Response Time  (all in seconds)\n");
    printf("Note: Table values are identical across OS (based on algorithm logic)\n");
    printf("========================================================================================\n");
}

void calculate_metrics(Process p[], int n,
                       double total_actual_time, int context_switches) {
    double total_wt = 0, total_tat = 0, total_rt = 0;
    double max_wt = 0, max_tat = 0, total_burst = 0;

    for (int i = 0; i < n; i++) {
        total_wt    += p[i].calc_waiting_time;
        total_tat   += p[i].calc_turnaround_time;
        total_rt    += p[i].calc_response_time;
        total_burst += p[i].burst_time;
        if (p[i].calc_waiting_time    > max_wt)  max_wt  = p[i].calc_waiting_time;
        if (p[i].calc_turnaround_time > max_tat) max_tat = p[i].calc_turnaround_time;
    }

    double cpu_util = (total_burst / total_actual_time) * 100.0;

    printf("\n");
    printf("========================================================================================\n");
    printf("              PERFORMANCE METRICS (%s - FCFS)\n", PLATFORM_NAME);
    printf("                      (Actual OS Measurements)\n");
    printf("========================================================================================\n");
    printf("Average Waiting Time       = %.2f seconds\n", total_wt  / n);
    printf("Average Response Time      = %.2f seconds\n", total_rt  / n);
    printf("Average Turnaround Time    = %.2f seconds\n", total_tat / n);
    printf("Max Waiting Time           = %.2f seconds\n", max_wt);
    printf("Max Turnaround Time        = %.2f seconds\n", max_tat);
    printf("Throughput                 = %.3f processes/second\n", n / total_actual_time);
    printf("CPU Utilization            = %.2f%%\n", cpu_util);
    printf("Avg Process Latency        = %.2f seconds\n", total_wt / n);
    printf("Worst-case Latency         = %.2f seconds (heuristic)\n", total_burst);
    printf("Total Execution Time       = %.2f seconds\n", total_actual_time);
    printf("Context Switches           = %d\n", context_switches);
    printf("========================================================================================\n");
    printf("Note: Metrics vary by OS due to kernel overhead and scheduling efficiency\n");
    printf("========================================================================================\n");
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 7 – main()
 * ══════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {

    /* ── Hidden worker-child mode (Windows only) ──────────────
     *  When this exe is launched as: fcfs.exe -work <seconds>
     *  it just does CPU work and exits.  This replaces fork().  */
#ifdef _WIN32
    if (argc >= 3 && strcmp(argv[1], "-work") == 0) {
        int seconds = atoi(argv[2]);
        cpu_intensive_work(seconds);
        return 0;
    }
#endif

    /* ── C2 process table ─────────────────────────────────── */
    Process processes[] = {
        {1,  "Microphone Feed - Victim1",  0,  5,  0,0,0,0,0},
        {2,  "Screen Sharing - Victim 2",  0,  5,  0,0,0,0,0},
        {3,  "Live Camera Feed - Victim1", 1,  4,  0,0,0,0,0},
        {4,  "Live Camera Feed - Victim2", 1,  4,  0,0,0,0,0},
        {5,  "Reverse Tunnel Proxy1",      2,  6,  0,0,0,0,0},
        {6,  "Reverse Tunnel Proxy2",      2,  6,  0,0,0,0,0},
        {7,  "Log Collection - Victim1",   3,  3,  0,0,0,0,0},
        {8,  "Key logger",                 5,  5,  0,0,0,0,0},
        {9,  "C2 Command Dispatcher",      9,  5,  0,0,0,0,0},
        {10, "Victim Probe Handler",       10, 4,  0,0,0,0,0},
    };
    int n = sizeof(processes) / sizeof(processes[0]);

    /* ── Banner ──────────────────────────────────────────────  */
    printf("========================================================================================\n");
    printf("       C2 Command & Control Server - Process Scheduler (%s)\n", PLATFORM_NAME);
    printf("                   Algorithm: First-Come-First-Serve (FCFS)\n");
    printf("========================================================================================\n\n");

    printf("C2 Server Configuration:\n");
    printf("  - Total Processes     : %d\n", n);
    printf("  - Scheduling Policy   : FCFS (Non-preemptive)\n");
    printf("  - Platform            : %s\n", PLATFORM_NAME);
    printf("  - Components          : Victim Handlers, Proxies, Log Collection\n\n");

    /* ── Calculate theoretical times (same on both OS) ───────  */
    calculate_fcfs_times(processes, n);

    printf("--- Process Queue (Arrival Order) ---\n");
    printf("PID  C2 Component                      Arrival Time  Burst Time\n");
    printf("-----------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("P%-3d %-35s %-13ds %-10ds\n",
               processes[i].pid, processes[i].task_name,
               processes[i].arrival_time, processes[i].burst_time);
    }

    printf("\n=== Starting FCFS Scheduling Simulation (C2 Server) ===\n");
    printf("Measuring actual OS performance on %s...\n\n", PLATFORM_NAME);

    double simulation_start = get_wall_time();
    int    context_switches = 0;

    for (int i = 0; i < n; i++) {
        double elapsed = get_wall_time() - simulation_start;

        /* Wait if process has not yet arrived */
        if (processes[i].arrival_time > elapsed) {
            int wait_sec = (int)(processes[i].arrival_time - elapsed);
            if (wait_sec > 0) portable_sleep(wait_sec);
        }

        printf(">>  Executing P%d: %s\n",
               processes[i].pid, processes[i].task_name);

        execute_process(&processes[i]);
        context_switches++;

        printf("OK  Completed P%d\n\n", processes[i].pid);
    }

    double total_actual_time = get_wall_time() - simulation_start;

    print_table(processes, n);
    calculate_metrics(processes, n, total_actual_time, context_switches);

    printf("\nSimulation completed on %s!\n", PLATFORM_NAME);
    printf("Compare these metrics between Linux and Windows to evaluate C2 server performance.\n\n");

    return 0;
}
