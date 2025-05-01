#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const int STR_SIZE = 5000;
const float prob = 0.05;
int waitSched = 1;

typedef struct pageTable_ {
    int valid;
    int frame_no;
} pageTable;

typedef struct pageMap_ {
    int pid;
    int num_page;
} pageMap;

void signalHandler(int sig) {
    if (sig == SIGUSR1) {
        waitSched = 0;
    }
}

int main() {
    int k, m, f;
    printf("Enter the total number of processes: ");
    scanf("%d", &k);
    printf("Enter the Virtual address space - Maximum number of pages required per process: ");
    scanf("%d", &m);
    printf("Enter the physical address space - Total number of frames: ");
    scanf("%d", &f);

    signal(SIGUSR1, signalHandler);

    key_t SM1_key = ftok("/tmp", 100);
    int SM1_id = shmget(SM1_key, k * m * sizeof(pageTable), IPC_CREAT | 0666);

    key_t SM2_key = ftok("/tmp", 101);
    int SM2_id = shmget(SM2_key, f * sizeof(int), IPC_CREAT | 0666);

    key_t SM3_key = ftok("/tmp", 102);
    int SM3_id = shmget(SM3_key, k * sizeof(pageMap), IPC_CREAT | 0666);

    key_t MQ1_key = ftok("/tmp", 103);
    int MQ1_id = msgget(MQ1_key, IPC_CREAT | 0666);

    key_t MQ2_key = ftok("/tmp", 104);
    int MQ2_id = msgget(MQ2_key, IPC_CREAT | 0666);

    key_t MQ3_key = ftok("/tmp", 105);
    int MQ3_id = msgget(MQ3_key, IPC_CREAT | 0666);

    pageTable *page_table = (pageTable *)shmat(SM1_id, NULL, 0);
    int *free_frame_list = (int *)shmat(SM2_id, NULL, 0);
    pageMap *page_map = (pageMap *)shmat(SM3_id, NULL, 0);

    for (int i = 0; i < k; i++) {
        for (int j = 0; j < m; j++) {
            page_table[i * m + j].valid = 0;
            page_table[i * m + j].frame_no = -1;
        }
    }

    for (int i = 0; i < f; i++) {
        free_frame_list[i] = 1;
    }

    for (int i = 0; i < k; i++) {
        page_map[i].pid = -1;
        page_map[i].num_page = -1;
    }

    pid_t sched_pid, mmu_pid;

    if ((sched_pid = fork()) == 0) {
        char MQ1_str[STR_SIZE], MQ2_str[STR_SIZE], num_process_str[STR_SIZE];
        sprintf(MQ1_str, "%d", MQ1_id);
        sprintf(MQ2_str, "%d", MQ2_id);
        sprintf(num_process_str, "%d", k);
        execlp("./sched", "./sched", MQ1_str, MQ2_str, num_process_str, NULL);
    }
    if ((mmu_pid = fork()) == 0) {
        char SM1_str[STR_SIZE], SM2_str[STR_SIZE], SM3_str[STR_SIZE], MQ2_str[STR_SIZE], MQ3_str[STR_SIZE], k_str[STR_SIZE], m_str[STR_SIZE], f_str[STR_SIZE];
        sprintf(SM1_str, "%d", SM1_id);
        sprintf(SM2_str, "%d", SM2_id);
        sprintf(SM3_str, "%d", SM3_id);
        sprintf(MQ2_str, "%d", MQ2_id);
        sprintf(MQ3_str, "%d", MQ3_id);
        sprintf(k_str, "%d", k);
        sprintf(m_str, "%d", m);
        sprintf(f_str, "%d", f);
        execlp("xterm", "xterm", "-hold", "-e", "./mmu", MQ2_str, MQ3_str, SM1_str, SM2_str, SM3_str, k_str, m_str, f_str, NULL);
    }

    int total_pages = 0;
    srand((unsigned)time(NULL));
    for (int i = 0; i < k; i++) {
        int m_i = rand() % m + 1;
        page_map[i].num_page = m_i;
        total_pages += m_i;
    }

    int frame_cnt = 0;
    for (int i = 0; i < k; i++) {
        if (frame_cnt >= f) {
            break;
        }
        int num_frames = (page_map[i].num_page * f) / total_pages;
        for (int j = 0; j < num_frames; j++) {
            page_table[i * m + j].frame_no = frame_cnt;
            page_table[i * m + j].valid = 1;
            free_frame_list[frame_cnt] = 0;
            frame_cnt++;
        }
        if (num_frames == 0) {
            page_table[i * m].frame_no = frame_cnt;
            page_table[i * m].valid = 1;
            free_frame_list[frame_cnt] = 0;
            frame_cnt++;
        }
    }

    for (int i = 0; i < k; i++) {
        int m_i = page_map[i].num_page;
        int x = 2 * m_i + rand() % (8 * m_i + 1);
        char ref_str[STR_SIZE];
        memset(ref_str, '\0', STR_SIZE);
        for (int j = 0; j < x; j++) {
            int page_no;
            if ((float)rand() / RAND_MAX < prob) {
                page_no = m_i;
            } else {
                page_no = rand() % m_i;
            }
            char page_no_str[50];
            memset(page_no_str, '\0', 50);
            if (j == x - 1)
                sprintf(page_no_str, "%d", page_no);
            else
                sprintf(page_no_str, "%d,", page_no);
            strcat(ref_str, page_no_str);
        }

        int process_pid;
        if ((process_pid = fork()) == 0) {
            page_map[i].pid = getpid();
            char MQ1_str[STR_SIZE], MQ3_str[STR_SIZE];
            sprintf(MQ1_str, "%d", MQ1_id);
            sprintf(MQ3_str, "%d", MQ3_id);
            execlp("./process", "./process", MQ1_str, MQ3_str, ref_str, NULL);
        }
        usleep(250000);
    }

    if (waitSched == 1) {
        pause();
    }
    waitSched = 0;
    printf("Scheduler finish...\n");

    kill(sched_pid, SIGINT);
    usleep(500000);
    kill(mmu_pid, SIGINT);

    shmdt(page_map);
    shmdt(page_table);
    shmdt(free_frame_list);
    shmctl(SM1_id, IPC_RMID, NULL);
    shmctl(SM2_id, IPC_RMID, NULL);
    shmctl(SM3_id, IPC_RMID, NULL);

    msgctl(MQ1_id, IPC_RMID, NULL);
    msgctl(MQ2_id, IPC_RMID, NULL);
    msgctl(MQ3_id, IPC_RMID, NULL);

    exit(0);
}
