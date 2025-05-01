#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <unistd.h>

struct msgbuf {
    long m_type;
    struct message_ {
        int pid;
    } message;
};

struct schedMsg {
    long m_type;
    int message;
};

int main(int argc, char *argv[]) {
    int MQ1_id = atoi(argv[1]);
    int MQ2_id = atoi(argv[2]);
    int k = atoi(argv[3]);

    struct msgbuf msg;
    struct schedMsg schMsg;

    int cnt = 0;
    while (cnt < k) {
        msgrcv(MQ1_id, &msg, sizeof(msg.message), 1, 0);
        kill(msg.message.pid, SIGUSR1);
        msgrcv(MQ2_id, &schMsg, sizeof(schMsg.message), 1, 0);

        if (schMsg.message == 1) {
            msg.m_type = 1;
            msg.message.pid = msg.message.pid;
            msgsnd(MQ1_id, &msg, sizeof(msg.message), 0);
        } else {
            cnt++;
        }
    }

    kill(getppid(), SIGUSR1);
    pause();
}
