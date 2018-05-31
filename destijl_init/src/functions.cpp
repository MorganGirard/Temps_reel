#include "../header/functions.h"

char mode_start;

void write_in_queue(RT_QUEUE *, MessageToMon);

void f_server(void *arg) {
    int err;
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    err = run_nodejs("/usr/local/bin/node", "/home/pi/Interface_Robot/server.js");

    if (err < 0) {
        printf("Failed to start nodejs: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    } else {
#ifdef _WITH_TRACE_
        printf("%s: nodejs started\n", info.name);
#endif
        open_server();
        rt_sem_broadcast(&sem_serverOk);
    }
}

void f_sendToMon(void * arg) {
    int err;
    MessageToMon msg;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    while (1) {

#ifdef _WITH_TRACE_
        printf("%s : waiting for a message in queue\n", info.name);
#endif
        if (rt_queue_read(&q_messageToMon, &msg, sizeof (MessageToRobot), TM_INFINITE) >= 0) {
#ifdef _WITH_TRACE_
            printf("%s : message {%s,%s} in queue\n", info.name, msg.header, msg.data);
#endif

            send_message_to_monitor(msg.header, msg.data);
            free_msgToMon_data(&msg);
            rt_queue_free(&q_messageToMon, &msg);
        } else {
            printf("Error msg queue write: %s\n", strerror(-err));
        }
    }
}

void f_receiveFromMon(void *arg) {
    MessageFromMon msg;
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    do {
#ifdef _WITH_TRACE_
        printf("%s : waiting for a message from monitor\n", info.name);
#endif
        err = receive_message_from_monitor(msg.header, msg.data);
#ifdef _WITH_TRACE_
        printf("%s: msg {header:%s,data=%s} received from UI\n", info.name, msg.header, msg.data);
#endif
        if (strcmp(msg.header, HEADER_MTS_COM_DMB) == 0) {
            if (msg.data[0] == OPEN_COM_DMB) { // Open communication supervisor-robot
#ifdef _WITH_TRACE_
                printf("%s: message open Xbee communication\n", info.name);
#endif
                rt_sem_v(&sem_openComRobot);
            }
        } else if (strcmp(msg.header, HEADER_MTS_DMB_ORDER) == 0) {
            if (msg.data[0] == DMB_START_WITHOUT_WD) { // Start robot
#ifdef _WITH_TRACE_
                printf("%s: message start robot\n", info.name);
#endif 
                rt_sem_v(&sem_startRobot);

            } else if ((msg.data[0] == DMB_GO_BACK)
                    || (msg.data[0] == DMB_GO_FORWARD)
                    || (msg.data[0] == DMB_GO_LEFT)
                    || (msg.data[0] == DMB_GO_RIGHT)
                    || (msg.data[0] == DMB_STOP_MOVE)) {

                rt_mutex_acquire(&mutex_move, TM_INFINITE);
                move = msg.data[0];
                rt_mutex_release(&mutex_move);
#ifdef _WITH_TRACE_
                printf("%s: message update movement with %c\n", info.name, move);
#endif

            }
        }
    } while (err > 0);

}

void f_openComRobot(void * arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_openComRobot\n", info.name);
#endif
        rt_sem_p(&sem_openComRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_openComRobot arrived => open communication robot\n", info.name);
#endif
        err = open_communication_robot();
        if (err == 0) {
#ifdef _WITH_TRACE_
            printf("%s : the communication is opened\n", info.name);
#endif
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_ACK);
            write_in_queue(&q_messageToMon, msg);
        } else {
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_NO_ACK);
            write_in_queue(&q_messageToMon, msg);
        }
    }
}

void f_startRobot(void * arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_startRobot\n", info.name);
#endif
        rt_sem_p(&sem_startRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_startRobot arrived => Start robot\n", info.name);
#endif
        err = send_command_to_robot(DMB_START_WITHOUT_WD);
        if (err == 0) {
#ifdef _WITH_TRACE_
            printf("%s : the robot is started\n", info.name);
#endif
            rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
            robotStarted = 1;
            rt_mutex_release(&mutex_robotStarted);
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_ACK);
            write_in_queue(&q_messageToMon, msg);
        } else {
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_NO_ACK);
            write_in_queue(&q_messageToMon, msg);
        }
    }
}

void f_move(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    /* PERIODIC START */
#ifdef _WITH_TRACE_
    printf("%s: start period\n", info.name);
#endif
    rt_task_set_periodic(NULL, TM_NOW, 100000000);
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s: Wait period \n", info.name);
#endif
        rt_task_wait_period(NULL);
#ifdef _WITH_TRACE_
        printf("%s: Periodic activation\n", info.name);
        printf("%s: move equals %c\n", info.name, move);
#endif
        rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
        if (robotStarted) {
            rt_mutex_acquire(&mutex_move, TM_INFINITE);
            err = send_command_to_robot(move);
            rt_mutex_release(&mutex_move);
            rt_mutex_acquire(&mutex_errCounter, TM_INFINITE);
            if (err == 0) {
            	errCounter = 0;
            } else {
            	errCounter++;
            	if (errCounter > 3) {
            		// send communication lost signal
            	}
            }
            rt_mutex_release(&mutex_errCounter);
#ifdef _WITH_TRACE_
            printf("%s: the movement %c was sent\n", info.name, move);
#endif            
        }
        rt_mutex_release(&mutex_robotStarted);
    }
}

void f_battery(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    int battery;
    /* PERIODIC START */
#ifdef _WITH_TRACE_
    printf("%s: start period\n", info.name);
#endif
    rt_task_set_periodic(NULL, TM_NOW, 500000000);
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s: Wait period \n", info.name);
#endif
        rt_task_wait_period(NULL);
#ifdef _WITH_TRACE_
        printf("%s: Periodic activation\n", info.name);
        printf("%s: move equals %c\n", info.name, move);
#endif
        rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
        if (robotStarted) {
            err = send_command_to_robot(DMB_GET_VBAT);
            rt_mutex_acquire(&mutex_errCounter, TM_INFINITE);
            if (err != -1) {
            	errCounter = 0;
            	battery = err + 48;
            	MessageToMon msg;
            	set_msgToMon_header(&msg, HEADER_STM_BAT);
            	set_msgToMon_data(&msg, (void *) &battery);
            	write_in_queue(&q_messageToMon, msg);
            } else {
            	errCounter++;
            	if (errCounter > 3) {
            		// send communication lost signal
            	}
            }
            rt_mutex_release(&mutex_errCounter);
#ifdef _WITH_TRACE_
            printf("%s: the movement %c was sent\n", info.name, move);
#endif
        }
        rt_mutex_release(&mutex_robotStarted);
    }
}

void f_gestCamera(void * arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    Camera camera;
    Image image;
    Arene local_arene;
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_msgForCamera\n", info.name);
#endif
        rt_sem_p(&sem_msgForCamera, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_msgForCamera arrived \n", info.name);
#endif
        rt_mutex_acquire(&mutex_msgFromMonitor, TM_INFINITE);
        char order = msgFromMonitor;
        rt_mutex_release(&mutex_msgFromMonitor);
        switch (order) {
			case CAM_OPEN:
				err = open_camera(&camera);
				if (err == 0) {
					rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
					imageControl = 1;
					rt_mutex_release(&mutex_imageControl);
				} else {
					MessageToMon msg;
                    set_msgToMon_header(&msg, HEADER_STM_MES);
                    set_msgToMon_data(&msg, (void *) "open_camera failed\n");
                    write_in_queue(&q_messageToMon, msg);
				}
				break;
			case CAM_CLOSE:
				close_camera(&camera);
				break;
			case CAM_COMPUTE_POSITION:
				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = 2;
				rt_mutex_release(&mutex_imageControl);
				break;
			case CAM_STOP_COMPUTE_POSITION:
				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = 1;
				rt_mutex_release(&mutex_imageControl);
				break;
			case CAM_ASK_ARENA:
				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = 0;
				rt_mutex_release(&mutex_imageControl);
				get_image(&camera, &image, NULL);
				err = detect_arene(&image, &local_arene);
				if (err == 0) {
					draw_arene(&image, &image, &local_arene);
					MessageToMon msg;
                    set_msgToMon_header(&msg, HEADER_STM_IMAGE);
                    set_msgToMon_data(&msg, (void *) image);
                    write_in_queue(&q_messageToMon, msg);
				} else {
					MessageToMon msg;
                    set_msgToMon_header(&msg, HEADER_STM_MES);
                    set_msgToMon_data(&msg, (void *) "Unable to detect arena\n");
                    write_in_queue(&q_messageToMon, msg);
				}
				break;
			case CAM_ARENA_CONFIRM:
				rt_mutex_acquire(&mutex_arene, TM_INFINITE);
				arene = local_arene;
				rt_mutex_release(&mutex_arene);

				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = 1;
				rt_mutex_release(&mutex_imageControl);
				break;
			case CAM_ARENA_INFIRM:
				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = 1;
				rt_mutex_release(&mutex_imageControl);
				break;
			default:
        }
    }
}

void write_in_queue(RT_QUEUE *queue, MessageToMon msg) {
    void *buff;
    buff = rt_queue_alloc(&q_messageToMon, sizeof (MessageToMon));
    memcpy(buff, &msg, sizeof (MessageToMon));
    rt_queue_send(&q_messageToMon, buff, sizeof (MessageToMon), Q_NORMAL);
}
