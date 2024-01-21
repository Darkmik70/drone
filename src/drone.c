#include "drone.h"
#include "constants.h"
#include "util.h"

#include <stdio.h>       
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <math.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <errno.h>


// Pipes working with the server
int server_drone[2];

/* Global variables */
int shm_pos_fd;             // File descriptor for drone position shm
char *ptr_pos;              // Shared memory for Drone Position 
sem_t *sem_pos;             // semaphore for drone positions

int decypher_message(const char *server_msg) {
    if (server_msg[0] == 'K') {
        return 1;
    } else if (server_msg[0] == 'I' && server_msg[1] == '1') {
        return 2;
    } else if (server_msg[0] == 'I' && server_msg[1] == '2') {
        return 3;
    }
    else {return 0;}
}

int main(int argc, char *argv[]) 
{
    get_args(argc, argv);

    struct sigaction sa;
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &sa, NULL);
    sigaction (SIGUSR1, &sa, NULL);    

    publish_pid_to_wd(DRONE_SYM, getpid());

    // DELETE: Shared memory for DRONE POSITION
    shm_pos_fd = shm_open(SHM_POS, O_RDWR, 0666);
    ptr_pos = mmap(0, SIZE_SHM, PROT_READ | PROT_WRITE, MAP_SHARED, shm_pos_fd, 0);
    // DELETE: semaphores
    sem_pos = sem_open(SEM_POS, 0);

    usleep(SLEEP_DRONE); // To let the interface.c process execute first write the initial positions.

    int x; int y;
    int maxX; int maxY;
    // DELETE: Shared memory read for drone position.
    sscanf(ptr_pos, "%d,%d,%d,%d", &x, &y, &maxX, &maxY); 
    // TODO: Using pipes obtain the initial drone position values and screen dimensions (from interface.c)
    // *

    // Variables for euler method
    int actionX; int actionY;
    double pos_x; double pos_y;
    // Initial values
    double Vx = 0.0; double Vy = 0.0;
    double forceX = 0.0; double forceY = 0.0;

    int counter = 0;
    while (1)
    {
        int xi; int yi;
        /* READ the pipe from SERVER*/
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_drone[0], &read_fds);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        
        char server_msg[20];

        int ready = select(server_drone[0] + 1, &read_fds, NULL, NULL, &timeout);
        if (ready == -1) {perror("Error in select");}

        if (ready > 0 && FD_ISSET(server_drone[0], &read_fds)) {
            char msg[MSG_LEN];
            ssize_t bytes_read = read(server_drone[0], msg, sizeof(msg));
            if (bytes_read > 0){
                msg[bytes_read] = '\0';
                strncpy(server_msg, msg, sizeof(server_msg));
                if(decypher_message(server_msg) == 1){
                    sscanf(server_msg, "K:%d,%d", &actionX, &actionY);
                }
                else if(decypher_message(server_msg) == 2){
                    sscanf(server_msg, "I1:%d,%d,%d,%d", &xi, &yi, &maxX, &maxY);
                    printf("Obtained initial parameters from server: %s\n", server_msg);
                }
                else if(decypher_message(server_msg) == 3){
                    sscanf(server_msg, "I2:%d,%d", &maxX, &maxY);
                    printf("Changed screen dimensions to: %s\n", server_msg);
                }
            }
        } else { actionX = 0; actionY = 0;}
  
        if(counter == 0){
            pos_x = (double)xi;
            pos_y = (double)yi;
            counter++;
        }

        /* Convert the action number read into force for the drone */ 

        // Initial external force
        double ext_forceX = 0.0; 
        double ext_forceY = 0.0; 

        if (abs(actionX) <= 1.0) // Only values between -1 to 1 are used to move the drone
        {
        forceX += (double)actionX;
        forceY += (double)actionY;
        /* Capping to the max value of force */
        if (forceX > F_MAX){forceX = F_MAX;}
        if (forceX < -F_MAX){forceX = -F_MAX;}
        if (forceY > F_MAX){forceY = F_MAX;}
        if (forceY < -F_MAX){forceY = -F_MAX;}
        }
        // For now, other values for action represent a STOP command.
        else{forceX = 0.0,forceY = 0.0;}

        /* Calculate the EXTERNAL FORCE from obstacles and targets */

        // TARGETS
        // TODO: Obtain the string for targets_msg from a pipe from (interface.c)
        char targets_msg[] = "140,23";
        double target_x, target_y;
        sscanf(targets_msg, "%lf,%lf", &target_x, &target_y);
        calculateExtForce(pos_x, pos_y, target_x, target_y, 0.0, 0.0, &ext_forceX, &ext_forceY);

        // OBSTACLES
        // TODO: Obtain the string for targets_msg from a pipe from (server.c) that comes originally from (obstacles.c)
        char obstacles_msg[] = "O[7]35,11|100,5|16,30|88,7|130,40|53,15|60,10";
        Obstacles obstacles[30];
        int numObstacles;
        parseObstaclesMsg(obstacles_msg, obstacles, &numObstacles);

        for (int i = 0; i < numObstacles; i++) {
            calculateExtForce(pos_x, pos_y, 0.0, 0.0, obstacles[i].x, obstacles[i].y, &ext_forceX, &ext_forceY);
        }
        
        // Using the euler method to obtain the new position for the drone, according to the forces.
        double maxX_f = (double)maxX;
        double maxY_f = (double)maxY;
        eulerMethod(&pos_x, &Vx, forceX, ext_forceX, &maxX_f);
        eulerMethod(&pos_y, &Vy, forceY, ext_forceY, &maxY_f);

        // Only print the data ONLY when there is movement from the drone.
        if (fabs(Vx) > FLOAT_TOLERANCE || fabs(Vy) > FLOAT_TOLERANCE)
        {
            printf("Drone force (X,Y): %.2f,%.2f\t|\t",forceX,forceY);
            printf("External force (X,Y): %.2f,%.2f\n",ext_forceX,ext_forceY);
            printf("X - Position: %.2f / Velocity: %.2f\t|\t", pos_x, Vx);
            printf("Y - Position: %.2f / Velocity: %.2f\n", pos_y, Vy);
            fflush(stdout);
        }

        int xf = (int)round(pos_x);
        int yf = (int)round(pos_y);
        // DELETE: Write new drone position to shared memory
        sprintf(ptr_pos, "%d,%d,%d,%d", xf, yf, maxX, maxY);
        // TODO: Using pipes write this data so it can be read from (interface.c)
        // *

        // DELETE: Semaphore post
        sem_post(sem_pos);
        
        usleep(D_T * 1e6); // 0.1 s
    }

    // DELETE: Close shared memories and semaphores
    close(shm_pos_fd);
    sem_close(sem_pos);
    return 0;
}


void signal_handler(int signo, siginfo_t *siginfo, void *context) 
{
    //printf("Received signal number: %d \n", signo);
    if( signo == SIGINT)
    {
        printf("Caught SIGINT \n");
        printf("Succesfully closed all semaphores\n");
        exit(1);
    }
    if (signo == SIGUSR1)
    {
        // Get watchdog's pid
        pid_t wd_pid = siginfo->si_pid;
        // inform on your condition
        kill(wd_pid, SIGUSR2);
        // printf("SIGUSR2 SENT SUCCESSFULLY\n");
    }
}

void get_args(int argc, char *argv[])
{
    sscanf(argv[1], "%d", &server_drone[0]);
}

void calculateExtForce(double droneX, double droneY, double targetX, double targetY,
                        double obstacleX, double obstacleY, double *ext_forceX, double *ext_forceY)
{

    // ***Calculate ATTRACTION force towards the target***
    double distanceToTarget = sqrt(pow(targetX - droneX, 2) + pow(targetY - droneY, 2));
    double angleToTarget = atan2(targetY - droneY, targetX - droneX);

    // To avoid division by zero or extremely high forces
    if (distanceToTarget < minDistance) {
        distanceToTarget = minDistance; // Keep the force value as it were on the minDistance
    }
    // Bellow 5m the attraction force will be calculated.
    else if (distanceToTarget < startDistance){
    *ext_forceX += Coefficient * (1.0 / distanceToTarget - 1.0 / 5.0) * (1.0 / pow(distanceToTarget,2)) * cos(angleToTarget);
    *ext_forceY += Coefficient * (1.0 / distanceToTarget - 1.0 / 5.0) * (1.0 / pow(distanceToTarget,2)) * sin(angleToTarget);
    }
    else{
        *ext_forceX += 0;
        *ext_forceY += 0;  
    }


    // ***Calculate REPULSION force from the obstacle***
    double distanceToObstacle = sqrt(pow(obstacleX - droneX, 2) + pow(obstacleY - droneY, 2));
    double angleToObstacle = atan2(obstacleY - droneY, obstacleX - droneX);

    // To avoid division by zero or extremely high forces
    if (distanceToObstacle < minDistance) {
            distanceToObstacle = minDistance; // Keep the force value as it were on the minDistance
        }
    // Bellow 5m the repulsion force will be calculated
    else if (distanceToObstacle < startDistance){
    *ext_forceX -= Coefficient * (1.0 / distanceToObstacle - 1.0 / 5.0) * (1.0 / pow(distanceToObstacle,2)) * cos(angleToObstacle);
    *ext_forceY -= Coefficient * (1.0 / distanceToObstacle - 1.0 / 5.0) * (1.0 / pow(distanceToObstacle,2)) * sin(angleToObstacle);
    }
    else{
        *ext_forceX -= 0;
        *ext_forceY -= 0;
    }
    // TO FIX A BUG WITH BIG FORCES APPEARING OUT OF NOWHERE
    // if(*ext_forceX > 50){*ext_forceX=0;}
    // if(*ext_forceY > 50){*ext_forceY=0;}
    // if(*ext_forceX < 50){*ext_forceX=0;}
    // if(*ext_forceY < 50){*ext_forceY=0;}
}


void parseObstaclesMsg(char *obstacles_msg, Obstacles *obstacles, int *numObstacles)
{
    int totalObstacles;
    sscanf(obstacles_msg, "O[%d]", &totalObstacles);

    char *token = strtok(obstacles_msg + 4, "|");
    *numObstacles = 0;

    while (token != NULL && *numObstacles < totalObstacles) {
        sscanf(token, "%d,%d", &obstacles[*numObstacles].x, &obstacles[*numObstacles].y);
        obstacles[*numObstacles].total = *numObstacles + 1;

        token = strtok(NULL, "|");
        (*numObstacles)++;
    }
}


void eulerMethod(double *pos, double *vel, double force, double extForce, double *maxPos)
{
    double total_force = force + extForce;
    double accelerationX = (total_force - DAMPING * (*vel)) / MASS;
    *vel = *vel + accelerationX * D_T;
    *pos = *pos + (*vel) * D_T;

    // Walls are the maximum position the drone can reach
    if (*pos < 0){*pos = 0;}
    if (*pos > *maxPos){*pos = *maxPos - 1;}
}