#ifndef DRONE_H
#define DRONE_H

#include <constants.h>
#include <signal.h>

#define SLEEP_DRONE 100000  // To let the interface.c process execute first write the initial positions.

#define MASS 1.0    // Mass of the object
#define DAMPING 1 // Damping coefficient
#define D_T 0.1 // Time interval in seconds
#define F_MAX 30.0 // Maximal force that can be acted on the drone's motors

// New Constants 
#define Coefficient 400.0 // This was obtained by trial-error
#define minDistance 2.0
#define startDistance 5.0

void get_args(int argc, char *argv[]);

void signal_handler(int signo, siginfo_t *siginfo, void *context);

void calculateExtForce(double droneX, double droneY, double targetX, double targetY,
                        double obstacleX, double obstacleY, double *ext_forceX, double *ext_forceY);

void parseObstaclesMsg(char *obstacles_msg, Obstacles *obstacles, int *numObstacles);

void eulerMethod(double *pos, double *vel, double force, double extForce, double *maxPos);


#endif //DRONE_H