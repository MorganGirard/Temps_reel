/* 
 * File:   main.c
 * Author: pehladik
 *
 * Created on 23 décembre 2017, 19:45
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <alchemy/task.h>
#include <alchemy/timer.h>
#include <alchemy/mutex.h>
#include <alchemy/sem.h>
#include <alchemy/queue.h>

#include "./header/functions.h"

// Déclaration des taches
RT_TASK th_server;
RT_TASK th_sendToMon;
RT_TASK th_receiveFromMon;
RT_TASK th_openComRobot;
RT_TASK th_startRobot;
RT_TASK th_move;
RT_TASK th_battery;
RT_TASK th_gestCamera;
RT_TASK th_sendImage;
RT_TASK th_cleanup;

// Déclaration des priorités des taches
int PRIORITY_TSERVER = 30;
int PRIORITY_TOPENCOMROBOT = 20;
int PRIORITY_TMOVE = 10;
int PRIORITY_TSENDTOMON = 25;
int PRIORITY_TRECEIVEFROMMON = 22;
int PRIORITY_TSTARTROBOT = 20;
int PRIORITY_TBATTERY = 24;
int PRIORITY_TGESTCAMERA = 35;
int PRIORITY_TSENDIMAGE = 36;
int PRIORITY_TCLEANUP = 40;

// Déclaration des mutex
//RT_MUTEX mutex_robotStarted;
RT_MUTEX mutex_move;
RT_MUTEX mutex_errCounter;
RT_MUTEX mutex_imageControl;
RT_MUTEX mutex_msgFromMon;
RT_MUTEX mutex_sharedCameraRes;
RT_MUTEX mutex_restart;

// Déclaration des sémaphores
RT_SEM sem_barrier;
RT_SEM sem_openComRobot;
RT_SEM sem_serverOk;
RT_SEM sem_startRobot;
RT_SEM sem_robotStarted;
RT_SEM sem_msgForCamera;
RT_SEM sem_cameraStarted;
RT_SEM sem_msgForComRobot;

// Déclaration des files de message
RT_QUEUE q_messageToMon;

int MSG_QUEUE_SIZE = 10;
// Déclaration des ressources partagées
int etatCommMoniteur = 1;
int robotStarted = 0;
char move = DMB_STOP_MOVE;
int errCounter = 0;
int imageControl = 0;
char msgFromMon;
Arene sharedArena;
Camera sharedCamera;
int restart = 0;

/**
 * \fn void initStruct(void)
 * \brief Initialisation des structures de l'application (tâches, mutex, 
 * semaphore, etc.)
 */
void initStruct(void);

/**
 * \fn void startTasks(void)
 * \brief Démarrage des tâches
 */
void startTasks(void);

/**
 * \fn void deleteTasks(void)
 * \brief Arrêt des tâches
 */
void deleteTasks(void);

int main(int argc, char **argv) {
    int err;
    //Lock the memory to avoid memory swapping for this program
    mlockall(MCL_CURRENT | MCL_FUTURE);

    printf("#################################\n");
    printf("#      DE STIJL PROJECT         #\n");
    printf("#################################\n");

    initStruct();
    startTasks();
    rt_sem_broadcast(&sem_barrier);
    pause();
    deleteTasks();

    return 0;
}

void initStruct(void) {
    int err;
    /* Creation des mutex */
    /*f (err = rt_mutex_create(&mutex_robotStarted, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }*/
    if (err = rt_mutex_create(&mutex_move, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_mutex_create(&mutex_errCounter, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_mutex_create(&mutex_imageControl, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_mutex_create(&mutex_msgFromMon, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_mutex_create(&mutex_sharedCameraRes, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_mutex_create(&mutex_restart, NULL)) {
        printf("Error mutex create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }

    /* Creation du semaphore */
    if (err = rt_sem_create(&sem_barrier, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_openComRobot, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_serverOk, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_startRobot, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_msgForCamera, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_robotStarted, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_cameraStarted, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_sem_create(&sem_msgForComRobot, NULL, 0, S_FIFO)) {
        printf("Error semaphore create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }

    /* Creation des taches */
    if (err = rt_task_create(&th_server, "th_server", 0, PRIORITY_TSERVER, 0)) {
        printf("Error task create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_receiveFromMon, "th_receiveFromMon", 0, PRIORITY_TRECEIVEFROMMON, 0)) {
        printf("Error task create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_sendToMon, "th_sendToMon", 0, PRIORITY_TSENDTOMON, 0)) {
        printf("Error task create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_openComRobot, "th_openComRobot", 0, PRIORITY_TOPENCOMROBOT, 0)) {
        printf("Error task create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_startRobot, "th_startRobot", 0, PRIORITY_TSTARTROBOT, 0)) {
        printf("Error task create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_move, "th_move", 0, PRIORITY_TMOVE, 0)) {
        printf("Error task create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_battery, "th_battery", 0, PRIORITY_TBATTERY, 0)) {
            printf("Error task create: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_gestCamera, "th_gestCamera", 0, PRIORITY_TGESTCAMERA, 0)) {
            printf("Error task create: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_sendImage, "th_sendImage", 0, PRIORITY_TSENDIMAGE, 0)) {
            printf("Error task create: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    if (err = rt_task_create(&th_cleanup, "th_cleanup", 0, PRIORITY_TCLEANUP, 0)) {
            printf("Error task create: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }

    /* Creation des files de messages */
    if (err = rt_queue_create(&q_messageToMon, "toto", MSG_QUEUE_SIZE * sizeof (MessageToRobot), MSG_QUEUE_SIZE, Q_FIFO)) {
        printf("Error msg queue create: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
}

void startTasks() {

    int err;
    
    if (err = rt_task_start(&th_startRobot, &f_startRobot, NULL)) {
        printf("Error task start: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }

    if (err = rt_task_start(&th_receiveFromMon, &f_receiveFromMon, NULL)) {
        printf("Error task start: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_sendToMon, &f_sendToMon, NULL)) {
        printf("Error task start: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_openComRobot, &f_openComRobot, NULL)) {
        printf("Error task start: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_move, &f_move, NULL)) {
        printf("Error task start: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_battery, &f_battery, NULL)) {
            printf("Error task start: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_gestCamera, &f_gestCamera, NULL)) {
            printf("Error task start: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_sendImage, &f_sendImage, NULL)) {
            printf("Error task start: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    if (err = rt_task_start(&th_cleanup, &f_cleanup, NULL)) {
            printf("Error task start: %s\n", strerror(-err));
            exit(EXIT_FAILURE);
    }
    
    if (err = rt_task_start(&th_server, &f_server, NULL)) {
        printf("Error task start: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    }
}

void deleteTasks() {
    rt_task_delete(&th_server);
    rt_task_delete(&th_openComRobot);
    rt_task_delete(&th_move);
}
