/*
 * ============================================================
 *  C2 Server Round Robin Scheduler - Cross-Platform (Linux/Windows)
 *  Compile on Linux  : gcc -o rr rr.c
 *  Compile on Windows: gcc -o rr.exe rr.c
 * ============================================================
 */

/* ── Platform detection ─────────────────────────────────────── */
#ifdef _WIN32
    #define PLATFORM_NAME "Windows"
    #include <windows.h>
    #include <process.h>
#else
    #define PLATFORM_NAME "Linux"
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIME_QUANTUM 2

/* ── Data structure ─────────────────────────────────────────── */
typedef struct {
    int    pid;
    char   task_name[50];
    int    arrival_time;
    int    burst_time;
    int    remaining_time;
    double calc_start_time;
    double calc_completion_time;
    double calc_turnaround_time;
    double calc_waiting_time;
    double calc_response_time;
    int    first_response;
} Process;

/* ══════════════════════════════════════════════════════════════
 *  SECTION 1 – Timing helper
 * ══════════════════════════════════════════════════════════════ */
double get_wall_time(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
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
void execute_process_quantum(Process *p, int quantum) {
#ifdef _WIN32
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    char exe_path[MAX_PATH];
    char cmdline[MAX_PATH + 32];

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -work %d", exe_path, quantum);

    if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        cpu_intensive_work(quantum);   /* fallback */
    }
#else
    pid_t child = fork();
    if (child == 0) {
        cpu_intensive_work(quantum);
        exit(0);
    } else {
        int status;
        waitpid(child, &status, 0);
    }
#endif
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 4 – Sleep helper
 * ══════════════════════════════════════════════════════════════ */
void portable_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 5 – Round Robin algorithm (pure C, identical on both OS)
 * ══════════════════════════════════════════════════════════════ */
void calculate_rr_times(Process p[], int n, int quantum) {
    double current_time = 0;
    int    completed    = 0;

    for (int i = 0; i < n; i++) {
        p[i].remaining_time = p[i].burst_time;
        p[i].first_response = 1;
        p[i].calc_start_time = -1;
    }

    int queue[200], front = 0, rear = 0;
    int in_queue[200];
    for (int i = 0; i < n; i++) in_queue[i] = 0;

    /* Enqueue processes arriving at time 0 */
    for (int i = 0; i < n; i++) {
        if (p[i].arrival_time == 0) {
            queue[rear++] = i;
            in_queue[i]   = 1;
        }
    }

    while (completed < n) {
        if (front == rear) {
            /* Queue empty — jump to next arrival */
            double next_arrival = 999999;
            int    next_idx     = -1;
            for (int i = 0; i < n; i++) {
                if (p[i].remaining_time > 0 && p[i].arrival_time > current_time) {
                    if (p[i].arrival_time < next_arrival) {
                        next_arrival = p[i].arrival_time;
                        next_idx     = i;
                    }
                }
            }
            if (next_idx != -1) {
                current_time    = next_arrival;
                queue[rear++]   = next_idx;
                in_queue[next_idx] = 1;
            }
        } else {
            int idx = queue[front++];

            if (p[idx].first_response) {
                p[idx].calc_start_time    = current_time;
                p[idx].calc_response_time = current_time - p[idx].arrival_time;
                p[idx].first_response     = 0;
            }

            int exec_time = (p[idx].remaining_time < quantum)
                            ? p[idx].remaining_time : quantum;
            p[idx].remaining_time -= exec_time;
            current_time          += exec_time;

            /* Enqueue newly arrived processes */
            for (int i = 0; i < n; i++) {
                if (!in_queue[i] && p[i].arrival_time <= current_time
                    && p[i].remaining_time > 0) {
                    queue[rear++] = i;
                    in_queue[i]   = 1;
                }
            }

            if (p[idx].remaining_time > 0) {
                queue[rear++] = idx;   /* re-queue for next quantum */
            } else {
                p[idx].calc_completion_time  = current_time;
                p[idx].calc_turnaround_time  = p[idx].calc_completion_time
                                               - p[idx].arrival_time;
                p[idx].calc_waiting_time     = p[idx].calc_turnaround_time
                                               - p[idx].burst_time;
                completed++;
                in_queue[idx] = 0;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 6 – Output helpers
 * ══════════════════════════════════════════════════════════════ */
void print_table(Process p[], int n) {
    printf("\n");
    printf("========================================================================================\n");
    printf("                  ROUND ROBIN SCHEDULING - PROCESS EXECUTION TABLE\n");
    printf("                          (Theoretical Calculations)\n");
    printf("========================================================================================\n");
    printf("PID  Task Name                    AT    BT    ST      CT      TAT     WT      RT\n");
    printf("----------------------------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("P%-3d %-26s %-5d %-5d %-7.2f %-7.2f %-7.2f %-7.2f %-7.2f\n",
               p[i].pid, p[i].task_name,
               p[i].arrival_time, p[i].burst_time,
               p[i].calc_start_time,      p[i].calc_completion_time,
               p[i].calc_turnaround_time, p[i].calc_waiting_time,
               p[i].calc_response_time);
    }
    printf("========================================================================================\n");
    printf("AT=Arrival Time, BT=Burst Time, ST=Start Time, CT=Completion Time\n");
    printf("TAT=Turnaround Time, WT=Waiting Time, RT=Response Time (all in seconds)\n");
    printf("Time Quantum = %d seconds\n", TIME_QUANTUM);
    printf("Note: Table values are identical across OS (based on algorithm logic)\n");
    printf("========================================================================================\n");
}

void calculate_metrics(Process p[], int n,
                       double total_actual_time, int context_switches) {
    double total_wt = 0, total_tat = 0, total_rt = 0;
    double max_wt   = 0, max_tat  = 0, total_burst = 0;

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
    printf("           PERFORMANCE METRICS (%s - Round Robin)\n", PLATFORM_NAME);
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
    printf("Time Quantum               = %d seconds\n", TIME_QUANTUM);
    printf("========================================================================================\n");
    printf("Note: These metrics vary by OS due to kernel overhead and scheduling efficiency\n");
    printf("========================================================================================\n");
}

/* ══════════════════════════════════════════════════════════════
 *  SECTION 7 – main()
 * ══════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {

    /* Hidden worker-child mode (Windows only)
     * Launched as: rr.exe -work <seconds>                 */
#ifdef _WIN32
    if (argc >= 3 && strcmp(argv[1], "-work") == 0) {
        cpu_intensive_work(atoi(argv[2]));
        return 0;
    }
#endif

    /* ── C2 process table ─────────────────────────────────── */
    Process processes[] = {
        {1,  "Microphone Feed - Victim1",  0,  5, 0, 0,0,0,0,0, 1},
        {2,  "Screen Sharing - Victim 2",  0,  5, 0, 0,0,0,0,0, 1},
        {3,  "Live Camera Feed - Victim1", 1,  4, 0, 0,0,0,0,0, 1},
        {4,  "Live Camera Feed - Victim2", 1,  4, 0, 0,0,0,0,0, 1},
        {5,  "Reverse Tunnel Proxy1",      2,  6, 0, 0,0,0,0,0, 1},
        {6,  "Reverse Tunnel Proxy2",      2,  6, 0, 0,0,0,0,0, 1},
        {7,  "Log Collection - Victim1",   3,  3, 0, 0,0,0,0,0, 1},
        {8,  "Key logger",                 5,  5, 0, 0,0,0,0,0, 1},
        {9,  "C2 Command Dispatcher",      9,  5, 0, 0,0,0,0,0, 1},
        {10, "Victim Probe Handler",       10, 4, 0, 0,0,0,0,0, 1},
    };
    int n = sizeof(processes) / sizeof(processes[0]);

    /* ── Banner ──────────────────────────────────────────────  */
    printf("========================================================================================\n");
    printf("       C2 Command & Control Server - Process Scheduler (%s)\n", PLATFORM_NAME);
    printf("                   Algorithm: Round Robin (RR)\n");
    printf("========================================================================================\n\n");

    printf("C2 Server Configuration:\n");
    printf("  - Total Processes     : %d\n", n);
    printf("  - Scheduling Policy   : Round Robin (Preemptive)\n");
    printf("  - Time Quantum        : %d seconds\n", TIME_QUANTUM);
    printf("  - Platform            : %s\n", PLATFORM_NAME);
    printf("  - Execution Mode      : Kernel-level process scheduling\n");
    printf("  - Load Type           : C2 server operations with multiple victim connections\n\n");

    /* ── Calculate theoretical times (same on both OS) ───────  */
    calculate_rr_times(processes, n, TIME_QUANTUM);

    printf("--- Process Queue (Arrival Order) ---\n");
    printf("PID  C2 Component                      Arrival Time  Burst Time\n");
    printf("-----------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("P%-3d %-35s %-13ds %-10ds\n",
               processes[i].pid, processes[i].task_name,
               processes[i].arrival_time, processes[i].burst_time);
    }

    printf("\n=== Starting Round Robin Scheduling Simulation (C2 Server) ===\n");
    printf("Measuring actual OS performance on %s...\n\n", PLATFORM_NAME);

    double simulation_start = get_wall_time();
    int    context_switches = 0;
    int    completed        = 0;

    /* Reset for execution phase */
    for (int i = 0; i < n; i++) {
        processes[i].remaining_time = processes[i].burst_time;
        processes[i].first_response = 1;
    }

    int queue[200], front = 0, rear = 0;
    int in_queue[200];
    for (int i = 0; i < n; i++) in_queue[i] = 0;

    /* Enqueue processes arriving at time 0 */
    for (int i = 0; i < n; i++) {
        if (processes[i].arrival_time == 0) {
            queue[rear++] = i;
            in_queue[i]   = 1;
        }
    }

    while (completed < n) {
        double elapsed = get_wall_time() - simulation_start;

        if (front == rear) {
            portable_sleep_ms(100);
            /* Check for newly arrived processes */
            elapsed = get_wall_time() - simulation_start;
            for (int i = 0; i < n; i++) {
                if (!in_queue[i] && processes[i].arrival_time <= (int)elapsed
                    && processes[i].remaining_time > 0) {
                    queue[rear++] = i;
                    in_queue[i]   = 1;
                }
            }
        } else {
            int idx       = queue[front++];
            int exec_time = (processes[idx].remaining_time < TIME_QUANTUM)
                            ? processes[idx].remaining_time : TIME_QUANTUM;

            printf(">>  Executing P%d: %s (Remaining: %ds, Quantum: %ds)\n",
                   processes[idx].pid, processes[idx].task_name,
                   processes[idx].remaining_time, exec_time);

            execute_process_quantum(&processes[idx], exec_time);
            processes[idx].remaining_time -= exec_time;
            context_switches++;

            /* Check for newly arrived processes after execution */
            elapsed = get_wall_time() - simulation_start;
            for (int i = 0; i < n; i++) {
                if (!in_queue[i] && processes[i].arrival_time <= (int)elapsed
                    && processes[i].remaining_time > 0) {
                    queue[rear++] = i;
                    in_queue[i]   = 1;
                }
            }

            if (processes[idx].remaining_time > 0) {
                queue[rear++] = idx;
                printf("   P%d preempted, re-queued\n\n", processes[idx].pid);
            } else {
                printf("OK  Completed P%d\n\n", processes[idx].pid);
                completed++;
                in_queue[idx] = 0;
            }
        }
    }

    double total_actual_time = get_wall_time() - simulation_start;

    print_table(processes, n);
    calculate_metrics(processes, n, total_actual_time, context_switches);

    printf("\nSimulation completed on %s!\n", PLATFORM_NAME);
    printf("Compare these metrics between Linux and Windows to evaluate C2 server performance.\n\n");

    return 0;
}
