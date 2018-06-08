#include "../header/functions.h"

char mode_start;
const int maxErrCount = 10;

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

/*#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif*/
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    while (1) {
/*#ifdef _WITH_TRACE_
        printf("%s : waiting for a message in queue\n", info.name);
#endif*/
        if (rt_queue_read(&q_messageToMon, &msg, sizeof (MessageToRobot), TM_INFINITE) >= 0) {
/*#ifdef _WITH_TRACE_
            printf("%s : message {%s,%s} in queue\n", info.name, msg.header, msg.data);
#endif*/    
            err = send_message_to_monitor(msg.header, msg.data);
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
            rt_mutex_acquire(&mutex_msgFromMon, TM_INFINITE);
            msgFromMon = msg.data[0];
            rt_mutex_release(&mutex_msgFromMon);
            rt_sem_v(&sem_msgForComRobot);
        } else if (strcmp(msg.header, HEADER_MTS_DMB_ORDER) == 0) {
            if (msg.data[0] == DMB_START_WITHOUT_WD || msg.data[0] == DMB_IDLE) { // Start robot or IDLE
#ifdef _WITH_TRACE_     
                printf("%s: message start robot\n", info.name);
#endif 
                rt_mutex_acquire(&mutex_msgFromMon, TM_INFINITE);
                msgFromMon = msg.data[0];
                rt_mutex_release(&mutex_msgFromMon);
                rt_sem_v(&sem_msgForRobot);
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
        } else if (strcmp(msg.header, HEADER_MTS_CAMERA) == 0) {
        	rt_mutex_acquire(&mutex_msgFromMon, TM_INFINITE);
		msgFromMon = msg.data[0];
		rt_mutex_release(&mutex_msgFromMon);
                printf("%s: wrote %c into msgFromMon\n", info.name, msgFromMon);
		rt_sem_v(&sem_msgForCamera);
        }
    } while (err > 0);
    printf("/!\\ Lost connection with server /!\\\n");
    rt_sem_v(&sem_lostNodeJs);  
}

void f_openComRobot(void * arg) {
    int err;
    char command;
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_openComRobot\n", info.name);
#endif
        rt_sem_p(&sem_msgForComRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_openComRobot arrived => open communication robot\n", info.name);
#endif  
        rt_mutex_acquire(&mutex_msgFromMon, TM_INFINITE);
        command = msgFromMon;
        rt_mutex_release(&mutex_msgFromMon);
        switch (command) {
            case OPEN_COM_DMB:
                err = open_communication_robot();
                if (err == 0) {
                    send_message_to_monitor(HEADER_STM_ACK, NULL);
                } else {
                    send_message_to_monitor(HEADER_STM_NO_ACK, NULL);
                }
                break;
            case CLOSE_COM_DMB:
                close_communication_robot();
                send_message_to_monitor(HEADER_STM_ACK, NULL);
                break;
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
    char command;
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_startRobot\n", info.name);
#endif
        rt_sem_p(&sem_msgForRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_startRobot arrived => Start robot\n", info.name);
#endif
        rt_mutex_acquire(&mutex_msgFromMon, TM_INFINITE);
        command = msgFromMon;
        rt_mutex_release(&mutex_msgFromMon);
        if (command == DMB_START_WITHOUT_WD) {
            err = send_command_to_robot(command);
            rt_mutex_acquire(&mutex_errCounter, TM_INFINITE);
            if (err == 0) {
                errCounter = 0;
                rt_mutex_release(&mutex_errCounter);
                send_message_to_monitor(HEADER_STM_ACK, NULL);
#ifdef _WITH_TRACE_
 printf("%s : the robot is started\n", info.name);
#endif
            rt_mutex_acquire(&mutex_restart, TM_INFINITE);
            restart = 0;
            rt_mutex_release(&mutex_restart);
            rt_sem_broadcast(&sem_robotStarted);
            } else {
                errCounter++;
                rt_mutex_release(&mutex_errCounter);
                send_message_to_monitor(HEADER_STM_NO_ACK, NULL);
                if (errCounter > maxErrCount) {
                    errRobot();
                }
            }
        } else if (command == DMB_IDLE) {
            rt_mutex_acquire(&mutex_restart, TM_INFINITE);
            restart = 1;
            rt_mutex_release(&mutex_restart);
            send_command_to_robot(DMB_STOP_MOVE);
            send_command_to_robot(command);
        }
    }
}

void f_move(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    int err;
    while(1) {
        rt_sem_p(&sem_robotStarted, TM_INFINITE);
        rt_task_set_periodic(NULL, TM_NOW, 100000000);
        /* PERIODIC START */
        rt_mutex_acquire(&mutex_restart, TM_INFINITE);
        while (!restart) {
            rt_mutex_release(&mutex_restart);
            rt_task_wait_period(NULL);
            rt_mutex_acquire(&mutex_move, TM_INFINITE);
            err = send_command_to_robot(move);
            rt_mutex_release(&mutex_move);
            rt_mutex_acquire(&mutex_errCounter, TM_INFINITE);
            if (err == 0) {
                errCounter = 0;
                rt_mutex_release(&mutex_errCounter);
            } else {
                errCounter++;
                rt_mutex_release(&mutex_errCounter);
                if (errCounter > maxErrCount) {
                    errRobot();
                }
            }  
        rt_mutex_acquire(&mutex_restart, TM_INFINITE);
#ifdef _WITH_TRACE_
            printf("%s: the movement %c was sent\n", info.name, move);
#endif            
        }
        rt_mutex_release(&mutex_restart);
    }
}

void f_battery(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    int battery;
    int err;
    while(1) {
        rt_sem_p(&sem_robotStarted, TM_INFINITE);
        rt_task_set_periodic(NULL, TM_NOW, 500000000);
        /* PERIODIC START */
        rt_mutex_acquire(&mutex_restart, TM_INFINITE);
        while (!restart) {
            rt_mutex_release(&mutex_restart);
            rt_task_wait_period(NULL);
            err = send_command_to_robot(DMB_GET_VBAT);
            rt_mutex_acquire(&mutex_errCounter, TM_INFINITE);
            if (err >= 0 && err <=2) {
                errCounter = 0;
                rt_mutex_release(&mutex_errCounter);
                battery = err + 48;
                send_message_to_monitor(HEADER_STM_BAT, &battery);
            } else {
                errCounter++;
                rt_mutex_release(&mutex_errCounter);
                if (errCounter > maxErrCount) {
                    errRobot();
                }
            }
        rt_mutex_acquire(&mutex_restart, TM_INFINITE);
#ifdef _WITH_TRACE_
            printf("%s: the battery level %d was sent\n", info.name, err);
#endif
        }
        rt_mutex_release(&mutex_restart);
    }
}

void f_gestCamera(void * arg) {
    int err;
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    int previousState;
    Camera camera;
    Image image;
    Arene arena, emptyArena;
    Jpg compressedImage;
    while (1) {
        rt_sem_p(&sem_msgForCamera, TM_INFINITE);
        rt_mutex_acquire(&mutex_msgFromMon, TM_INFINITE);
        char order = msgFromMon;
        rt_mutex_release(&mutex_msgFromMon);
#ifdef _WITH_TRACE_
        printf("%s: read %c in msgFromMon\n", info.name, order);
#endif
        switch (order) {
			case CAM_OPEN:
				err = open_camera(&camera);
				if (err == 0) {
                                    rt_mutex_acquire(&mutex_sharedCameraRes, TM_INFINITE);
                                    sharedCamera = camera;
                                    rt_mutex_release(&mutex_sharedCameraRes);
                                    rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
                                    imageControl = 1;
                                    rt_mutex_release(&mutex_imageControl);
                                    rt_sem_v(&sem_cameraStarted);
                                    send_message_to_monitor(HEADER_STM_ACK, NULL);
				} else {
                                    send_message_to_monitor(HEADER_STM_NO_ACK, NULL);
				}
				break;
			case CAM_CLOSE:
				close_camera(&camera);
                                send_message_to_monitor(HEADER_STM_ACK, NULL);
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
				previousState = imageControl;
                                imageControl = 0;
				rt_mutex_release(&mutex_imageControl);
				get_image(&camera, &image, NULL);
                                arena = emptyArena;
                                err = detect_arena(&image, &arena);
				if (err >= 0) {
                                    draw_arena(&image, &image, &arena);
                                    compress_image(&image, &compressedImage);
                                    send_message_to_monitor(HEADER_STM_IMAGE, &compressedImage);
				} else {
                                    send_message_to_monitor(HEADER_STM_NO_ACK, NULL);
                                    imageControl = previousState;
				}
				break;
			case CAM_ARENA_CONFIRM:
				rt_mutex_acquire(&mutex_sharedCameraRes, TM_INFINITE);
				sharedArena = arena;
				rt_mutex_release(&mutex_sharedCameraRes);

				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = previousState;
				rt_mutex_release(&mutex_imageControl);
				break;
			case CAM_ARENA_INFIRM:
				rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
				imageControl = previousState;
				rt_mutex_release(&mutex_imageControl);
				break;
        }
    }
}

void f_sendImage(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    /* PERIODIC START */
    Image image;
    Jpg compressedImage;
    Camera camera;
    Arene arena;
    rt_sem_p(&sem_cameraStarted, TM_INFINITE);
    rt_task_set_periodic(NULL, TM_NOW, 100000000);
    while (1) {
        rt_task_wait_period(NULL);
        rt_mutex_acquire(&mutex_imageControl, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s: imageControl=%d\n", info.name, imageControl);
#endif     
        if (imageControl) {
            rt_mutex_acquire(&mutex_sharedCameraRes, TM_INFINITE);
            camera = sharedCamera;
            get_image(&camera, &image, "");
            arena = sharedArena;
            draw_arena(&image, &image, &arena);
            if (imageControl == 2) {
            	Position position;
            	detect_position(&image, &position, &arena);
                send_message_to_monitor(HEADER_STM_POS, &position);
            	draw_position(&image, &image, &position);
            }
            compress_image(&image, &compressedImage);
            send_message_to_monitor(HEADER_STM_IMAGE, &compressedImage);
            rt_mutex_release(&mutex_sharedCameraRes);
        }
        rt_mutex_release(&mutex_imageControl);
    }
}

void f_cleanup(void *arg) {
     /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    rt_sem_p(&sem_lostNodeJs, TM_INFINITE);
    send_command_to_robot(DMB_STOP_MOVE);
    close_communication_robot();
    rt_mutex_acquire(&mutex_sharedCameraRes, TM_INFINITE);
    close_camera(&sharedCamera);
    rt_mutex_release(&mutex_sharedCameraRes);
    close_server();
    exit(EXIT_FAILURE);
    
    
}

void write_in_queue(RT_QUEUE *queue, MessageToMon msg) {
    void *buff;
    buff = rt_queue_alloc(&q_messageToMon, sizeof (MessageToMon));
    memcpy(buff, &msg, sizeof (MessageToMon));
    rt_queue_send(&q_messageToMon, buff, sizeof (MessageToMon), Q_NORMAL);
}

void errRobot() {
    printf("/!\\ Lost connection with robot /!\\\n");
    close_communication_robot();
    send_message_to_monitor(HEADER_STM_LOST_DMB, NULL);
    send_message_to_monitor(HEADER_STM_MES, "Robot connection lost");
    rt_mutex_acquire(&mutex_errCounter, TM_INFINITE);
    errCounter = 0;
    rt_mutex_release(&mutex_errCounter);
    rt_mutex_acquire(&mutex_restart, TM_INFINITE);
    restart = 1;
    rt_mutex_release(&mutex_restart);
}