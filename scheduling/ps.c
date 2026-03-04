/*
 * ============================================================
 *  Priority Scheduling Simulator - Cross-Platform (Linux/Windows)
 *  Compile on Linux  : gcc -O2 -o ps ps.c
 *  Compile on Windows: gcc -O2 -o ps.exe ps.c
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #define PLATFORM_NAME "Windows"
    #include <windows.h>
#else
    #define PLATFORM_NAME "Linux"
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/time.h>
#endif

typedef struct {
    int pid;
    char task_name[50];
    int arrival_time;
    int burst_time;
    int priority;  // Lower number = higher priority

    double calc_start_time;
    double calc_completion_time;
    double calc_turnaround_time;
    double calc_waiting_time;
    double calc_response_time;

    int completed;
} Process;

double get_wall_time() {
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

void cpu_intensive_work(int seconds) {
    double target = get_wall_time() + seconds;
    volatile double result = 0.0;

    while (get_wall_time() < target) {
        for (volatile long i = 0; i < 10000000L; i++) {
            result += i * 3.14159;
            result = result / 2.71828;
        }
    }
}

void portable_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
}

/* Execute one full burst (platform-specific) */
void execute_process(Process *p) {
#ifdef _WIN32
    /* Relaunch self in worker mode: ps.exe -work <seconds> */
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;
    char exe_path[MAX_PATH];
    char cmdline[MAX_PATH + 64];

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -work %d", exe_path, p->burst_time);

    if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        cpu_intensive_work(p->burst_time); /* fallback */
    }
#else
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

void calculate_priority_times(Process p[], int n) {
    double current_time = 0;
    int completed_count = 0;

    // Initialize
    for (int i = 0; i < n; i++) {
        p[i].completed = 0;
    }

    while (completed_count < n) {
        int highest_priority = -1;
        int min_priority = 999999;

        // Find highest priority job among arrived processes
        for (int i = 0; i < n; i++) {
            if (!p[i].completed && p[i].arrival_time <= current_time) {
                if (p[i].priority < min_priority) {
                    min_priority = p[i].priority;
                    highest_priority = i;
                } else if (p[i].priority == min_priority && highest_priority != -1) {
                    // Tie-break: earlier arrival, then lower PID
                    if (p[i].arrival_time < p[highest_priority].arrival_time ||
                        (p[i].arrival_time == p[highest_priority].arrival_time &&
                         p[i].pid < p[highest_priority].pid)) {
                        highest_priority = i;
                    }
                }
            }
        }

        if (highest_priority == -1) {
            // No process available, advance time to next arrival
            double next_arrival = 999999;
            for (int i = 0; i < n; i++) {
                if (!p[i].completed && p[i].arrival_time > current_time) {
                    if (p[i].arrival_time < next_arrival) {
                        next_arrival = p[i].arrival_time;
                    }
                }
            }
            current_time = next_arrival;
        } else {
            p[highest_priority].calc_start_time      = current_time;
            p[highest_priority].calc_completion_time = current_time + p[highest_priority].burst_time;
            p[highest_priority].calc_turnaround_time = p[highest_priority].calc_completion_time
                                                       - p[highest_priority].arrival_time;
            p[highest_priority].calc_waiting_time    = p[highest_priority].calc_start_time
                                                       - p[highest_priority].arrival_time;
            p[highest_priority].calc_response_time   = p[highest_priority].calc_waiting_time;

            p[highest_priority].completed = 1;
            current_time = p[highest_priority].calc_completion_time;
            completed_count++;
        }
    }
}

void print_table(Process p[], int n) {
    const int NAME_W = 28; // increase to 30/32 if you want more space

    printf("\n");
    printf("========================================================================================\n");
    printf("                  PRIORITY SCHEDULING - PROCESS EXECUTION TABLE\n");
    printf("                             (Theoretical Calculations)\n");
    printf("========================================================================================\n");

    // Header (matches row widths)
    printf("%-4s %-*s %3s %4s %4s %8s %8s %8s %8s %8s\n",
           "PID", NAME_W, "Task Name", "PR", "AT", "BT", "ST", "CT", "TAT", "WT", "RT");
    printf("----------------------------------------------------------------------------------------\n");

    // Rows
    for (int i = 0; i < n; i++) {
        printf("P%-3d %-*.*s %3d %4d %4d %8.2f %8.2f %8.2f %8.2f %8.2f\n",
               p[i].pid,
               NAME_W, NAME_W, p[i].task_name,     // width + truncation protection
               p[i].priority,
               p[i].arrival_time,
               p[i].burst_time,
               p[i].calc_start_time,
               p[i].calc_completion_time,
               p[i].calc_turnaround_time,
               p[i].calc_waiting_time,
               p[i].calc_response_time);
    }

    printf("========================================================================================\n");
    printf("PR=Priority (lower=higher), AT=Arrival Time, BT=Burst Time, ST=Start Time\n");
    printf("CT=Completion Time, TAT=Turnaround Time, WT=Waiting Time, RT=Response Time (seconds)\n");
    printf("Note: Table values are identical across OS (based on algorithm logic)\n");
    printf("========================================================================================\n");
}

void calculate_metrics(Process p[], int n, double total_actual_time, int context_switches) {
    double total_waiting_time = 0;
    double total_turnaround_time = 0;
    double total_response_time = 0;
    double max_waiting_time = 0;
    double max_turnaround_time = 0;
    double total_burst_time = 0;

    for (int i = 0; i < n; i++) {
        total_waiting_time    += p[i].calc_waiting_time;
        total_turnaround_time += p[i].calc_turnaround_time;
        total_response_time   += p[i].calc_response_time;
        total_burst_time      += p[i].burst_time;

        if (p[i].calc_waiting_time > max_waiting_time)
            max_waiting_time = p[i].calc_waiting_time;
        if (p[i].calc_turnaround_time > max_turnaround_time)
            max_turnaround_time = p[i].calc_turnaround_time;
    }

    double cpu_utilization = (total_burst_time / total_actual_time) * 100.0;

    printf("\n");
    printf("========================================================================================\n");
    printf("                   PERFORMANCE METRICS (%s - Priority Scheduling)\n", PLATFORM_NAME);
    printf("                          (Actual OS Measurements)\n");
    printf("========================================================================================\n");
    printf("Average Waiting Time       = %.2f seconds\n", total_waiting_time / n);
    printf("Average Response Time      = %.2f seconds\n", total_response_time / n);
    printf("Average Turnaround Time    = %.2f seconds\n", total_turnaround_time / n);
    printf("Max Waiting Time           = %.2f seconds\n", max_waiting_time);
    printf("Max Turnaround Time        = %.2f seconds\n", max_turnaround_time);
    printf("Throughput                 = %.3f processes/second\n", n / total_actual_time);
    printf("CPU Utilization            = %.2f%%\n", cpu_utilization);
    printf("Avg Process Latency        = %.2f seconds\n", total_waiting_time / n);
    printf("Worst-case Latency         = %.2f seconds (heuristic)\n", total_burst_time);
    printf("Total Execution Time       = %.2f seconds\n", total_actual_time);
    printf("Context Switches           = %d\n", context_switches);
    printf("========================================================================================\n");
    printf("Note: These metrics vary by OS due to kernel overhead and scheduling efficiency\n");
    printf("========================================================================================\n");
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Worker mode (Windows only): ps.exe -work <seconds> */
    if (argc >= 3 && strcmp(argv[1], "-work") == 0) {
        cpu_intensive_work(atoi(argv[2]));
        return 0;
    }
#endif

    /* Use your same arrival/burst values.
       Paste your exact task_name strings here in your own file if needed. */
    Process processes[] = {
        {1,  "Microphone Feed - Victim1",  0,  5,  2, 0,0,0,0,0, 0},
        {2,  "Screen Sharing - Victim 2",  0,  5,  2, 0,0,0,0,0, 0},
        {3,  "Live Camera Feed - Victim1",  1,  4,  2, 0,0,0,0,0, 0},
        {4,  "Live Camera Feed - Victim2",  1,  4,  2, 0,0,0,0,0, 0},
        {5,  "Reverse Tunnel Proxy1",  2,  6,  1, 0,0,0,0,0, 0},
        {6,  "Reverse Tunnel Proxy2",  2,  6,  1, 0,0,0,0,0, 0},
        {7,  "Log Collection - Victim1",  3,  3,  3, 0,0,0,0,0, 0},
        {8,  "Key logger",  5,  5,  4, 0,0,0,0,0, 0},
        {9,  "C2 Command Dispatcher",  9,  5,  0, 0,0,0,0,0, 0},
        {10, "Victim Probe Handler", 10, 4,  0, 0,0,0,0,0, 0},
    };

    int n = (int)(sizeof(processes) / sizeof(processes[0]));

    printf("========================================================================================\n");
    printf("           Process Scheduler (%s)\n", PLATFORM_NAME);
    printf("                 Algorithm: Priority Scheduling (Non-preemptive)\n");
    printf("========================================================================================\n\n");

    printf("System Configuration:\n");
    printf("  - Total Processes     : %d\n", n);
    printf("  - Scheduling Policy   : Priority Scheduling (Non-preemptive)\n");
    printf("  - Lower number = higher priority\n\n");

    /* Theoretical calculations */
    calculate_priority_times(processes, n);

    printf("--- Process Queue (Arrival Order with Priorities) ---\n");
    printf("PID  Task Name                         Priority  Arrival Time  Burst Time\n");
    printf("------------------------------------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        printf("P%-3d %-35s %-9d %-13ds %-10ds\n",
               processes[i].pid, processes[i].task_name,
               processes[i].priority, processes[i].arrival_time,
               processes[i].burst_time);
    }

    printf("\n=== Starting Priority Scheduling Simulation ===\n");
    printf("Measuring actual OS performance on %s...\n\n", PLATFORM_NAME);

    double simulation_start = get_wall_time();
    int context_switches = 0;
    int completed_count = 0;

    /* Reset completion flags for runtime loop */
    for (int i = 0; i < n; i++) {
        processes[i].completed = 0;
    }

    while (completed_count < n) {
        int pick = -1;
        int best_pr = 999999;

        double elapsed = get_wall_time() - simulation_start;

        /* Find highest priority among arrived */
        for (int i = 0; i < n; i++) {
            if (!processes[i].completed && processes[i].arrival_time <= (int)elapsed) {
                if (processes[i].priority < best_pr) {
                    best_pr = processes[i].priority;
                    pick = i;
                } else if (processes[i].priority == best_pr && pick != -1) {
                    if (processes[i].arrival_time < processes[pick].arrival_time ||
                        (processes[i].arrival_time == processes[pick].arrival_time &&
                         processes[i].pid < processes[pick].pid)) {
                        pick = i;
                    }
                }
            }
        }

        if (pick == -1) {
            portable_sleep_ms(100);
            continue;
        }

        printf("▶  Executing P%d: %s (Priority: %d, Burst: %ds)\n",
               processes[pick].pid,
               processes[pick].task_name,
               processes[pick].priority,
               processes[pick].burst_time);

        execute_process(&processes[pick]);
        processes[pick].completed = 1;
        context_switches++;
        completed_count++;

        printf("✓  Completed P%d\n\n", processes[pick].pid);
    }

    double total_actual_time = get_wall_time() - simulation_start;

    print_table(processes, n);
    calculate_metrics(processes, n, total_actual_time, context_switches);

    printf("\n✓ Simulation completed on %s!\n\n", PLATFORM_NAME);
    return 0;
}
