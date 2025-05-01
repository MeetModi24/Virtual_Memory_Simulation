#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

int timestamp = 0;

typedef struct pageTable_ {
    int valid;
    int frame_no;
} pageTable;

typedef struct pageMap_ {
    int pid;
    int num_page;
} pageMap;

struct pageFrame {
    long m_type;
    struct message {
        int pid;
        int data;
    } message;
};

struct schedMsg {
    long m_type;
    int message;
};

void PageFaultHandler(int p_i, int m, int f, int page_num, pageTable *page_table, int *free_frame_list, int pageTimeStamp[][m]) {
    for (int i = 0; i < f; i++) {
        if (free_frame_list[i] == 1) {
            page_table[p_i * m + page_num].frame_no = i;
            page_table[p_i * m + page_num].valid = 1;
            free_frame_list[i] = 0;
            return;
        }
    }

    int diff;
    int lruPage;
    int maxDiff = -1;
    for (int j = 0; j < m; j++) {
        if (page_table[p_i * m + j].valid == 1) {
            diff = timestamp - pageTimeStamp[p_i][j];
            if (diff > maxDiff) {
                maxDiff = diff;
                lruPage = j;
            }
        }
    }

    if (maxDiff != -1) {
        page_table[p_i * m + page_num].frame_no = page_table[p_i * m + lruPage].frame_no;
        page_table[p_i * m + lruPage].valid = 0;
        page_table[p_i * m + page_num].valid = 1;
        pageTimeStamp[p_i][page_num] = timestamp;
    }
}

int main(int argc, char *argv[]) {
    int MQ2_id = atoi(argv[1]);
    int MQ3_id = atoi(argv[2]);
    int SM1_id = atoi(argv[3]);
    int SM2_id = atoi(argv[4]);
    int SM3_id = atoi(argv[5]);
    int k = atoi(argv[6]);
    int m = atoi(argv[7]);
    int f = atoi(argv[8]);

    int fd = open("result.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);

    pageTable *page_table = (pageTable *)shmat(SM1_id, NULL, 0);
    int *free_frame_list = (int *)shmat(SM2_id, NULL, 0);
    pageMap *page_map = (pageMap *)shmat(SM3_id, NULL, 0);

    int pageTimeStamp[k][m];
    for (int i = 0; i < k; i++) {
        for (int j = 0; j < m; j++) {
            if (page_table[i * m + j].valid == 1) {
                pageTimeStamp[i][j] = timestamp;
            } else {
                pageTimeStamp[i][j] = -1;
            }
        }
    }

    int page_faults[k], invalid_page_ref[k];
    for (int i = 0; i < k; i++) {
        page_faults[i] = 0;
        invalid_page_ref[i] = 0;
    }

    struct schedMsg schMsg;
    struct pageFrame pgf_msg;

    int cnt = 0;

    while (cnt < k) {
        msgrcv(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 1, 0);

        int page_num = pgf_msg.message.data;
        int p_pid = pgf_msg.message.pid;

        int process_num;
        for (int i = 0; i < k; i++) {
            if (page_map[i].pid == p_pid) {
                process_num = i + 1;
            }
        }

        char str[100];
        memset(str, '\0', 100);
        printf("Global ordering - (%d,%d,%d)\n", timestamp, process_num, page_num);
        sprintf(str, "Global ordering - (%d,%d,%d)\n", timestamp, process_num, page_num);
        write(fd, str, strlen(str));

        if (page_num == -9) {
            for (int i = 0; i < k; i++) {
                if (page_map[i].pid == p_pid) {
                    for (int j = 0; j < m; j++) {
                        if (page_table[i * m + j].valid == 1) {
                            free_frame_list[page_table[i * m + j].frame_no] = 1;
                            page_table[i * m + j].valid = 0;
                        }
                    }
                }
            }

            schMsg.m_type = 1;
            schMsg.message = 2;
            msgsnd(MQ2_id, &schMsg, sizeof(schMsg.message), 0);
            cnt++;
        } else {
            for (int i = 0; i < k; i++) {
                if (page_map[i].pid == p_pid) {
                    if (page_map[i].num_page <= page_num) {
                        printf("Invalid page reference - (%d,%d)\n", process_num, page_num);
                        memset(str, '\0', 100);
                        sprintf(str, "Invalid page reference - (%d,%d)\n", process_num, page_num);
                        write(fd, str, strlen(str));

                        pgf_msg.m_type = p_pid;
                        pgf_msg.message.data = -2;
                        msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);
                        invalid_page_ref[i]++;

                        for (int j = 0; j < m; j++) {
                            if (page_table[i * m + j].valid == 1) {
                                free_frame_list[page_table[i * m + j].frame_no] = 1;
                                page_table[i * m + j].valid = 0;
                            }
                        }

                        schMsg.m_type = 1;
                        schMsg.message = 2;
                        msgsnd(MQ2_id, &schMsg, sizeof(schMsg.message), 0);
                        cnt++;
                    } else {
                        if (page_table[i * m + page_num].valid == 1) {
                            pageTimeStamp[i][page_num] = timestamp;
                            pgf_msg.m_type = p_pid;
                            pgf_msg.message.data = page_table[i * m + page_num].frame_no;
                            msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);
                        } else {
                            printf("Page fault sequence - (%d,%d)\n", process_num, page_num);
                            memset(str, '\0', 100);
                            sprintf(str, "Page fault sequence - (%d,%d)\n", process_num, page_num);
                            write(fd, str, strlen(str));

                            PageFaultHandler(i, m, f, page_num, page_table, free_frame_list, pageTimeStamp);
                            page_faults[i]++;

                            pgf_msg.m_type = p_pid;
                            pgf_msg.message.data = -1;
                            msgsnd(MQ3_id, &pgf_msg, sizeof(pgf_msg.message), 0);

                            schMsg.m_type = 1;
                            schMsg.message = 1;
                            msgsnd(MQ2_id, &schMsg, sizeof(schMsg.message), 0);
                        }
                    }
                }
            }
        }
        timestamp++;
    }

    for (int i = 0; i < k; i++) {
        printf("Process %d: No. of Page faults = %d, No. of Invalid page references = %d\n", i + 1, page_faults[i], invalid_page_ref[i]);
        char str[500];
        memset(str, '\0', 500);
        sprintf(str, "Process %d: No. of Page faults = %d, No. of Invalid page references = %d\n", i + 1, page_faults[i], invalid_page_ref[i]);
        write(fd, str, strlen(str));
    }

    pause();
}

