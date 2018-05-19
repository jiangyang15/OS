#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {

    int i;

    // init quadrants
    for (i = 0; i < 4; ++i){
        if (pthread_mutex_init(&isection.quad[i], NULL) != 0){
            exit(-1);
        }
    }

    // init lane
    for (i = 0; i < 4; ++i){
        // init mutex lock
        if (pthread_mutex_init(&(isection.lanes[i].lock), NULL) != 0){
            exit(-1);
        }
        
        // init conditional variable
        if (pthread_cond_init(&(isection.lanes[i].producer_cv), NULL) != 0){
            exit(-1);
        }
        if (pthread_cond_init(&(isection.lanes[i].consumer_cv), NULL) != 0){
            exit(-1);
        }

        isection.lanes[i].in_cars = NULL;
        isection.lanes[i].out_cars = NULL;
        isection.lanes[i].inc = 0;      
        isection.lanes[i].passed = 0;       

        isection.lanes[i].buffer = malloc(sizeof(struct car *) * LANE_LENGTH);
        if (isection.lanes[i].buffer == NULL){
            exit(-1);
        }
        isection.lanes[i].head = 0;     
        isection.lanes[i].tail = 0;     
        isection.lanes[i].capacity = LANE_LENGTH;
        isection.lanes[i].in_buf = 0;
        
    }
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;

    /* avoid compiler warning */
    //l = l;

    //from in_cars  into buf (producer)
    struct car *cur; 
    
    cur = l->in_cars;
    while (1) {

        if (l->inc == 0){
            // no  car 
            break;
        }
        
        //  lock  the lane, produce car into buf
        pthread_mutex_lock(&l->lock);

        // buf is full,wait  for space
        while (l->in_buf == l->capacity) {
            pthread_cond_wait(&l->consumer_cv, &l->lock);
        }

        l->buffer[l->tail] = cur;
        l->tail = (l->tail + 1) % LANE_LENGTH; 
        l->in_buf++;
        l->inc--;

        // next car
        cur = cur->next;

        //   tell wait comsumer, can comsume
        pthread_cond_signal(&l->producer_cv);

        //  unlock  the lane
        pthread_mutex_unlock(&l->lock);
    }

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void *car_cross(void *arg) {
    struct lane *l = arg;

    /* avoid compiler warning */
    // l = l;

    struct car *cur;
    // comsume the car from lane buffer
    while (1) {

        // first lock the lane
        pthread_mutex_lock(&l->lock);

        // if buffer is empty and no car in , then the comsumer can exit
        if (l->in_buf == 0 && l->inc == 0){ 
            pthread_mutex_unlock(&l->lock);
            break;
        }
        
        // if buffer is  empty, wait  the  producer
        while (l->in_buf == 0) {
            pthread_cond_wait(&l->producer_cv, &l->lock);
        }
    
        cur = l->buffer[l->head];

        //get out  path
        int *path = compute_path(cur->in_dir, cur->out_dir);

        if (path[0] == -1){
            // no path
            pthread_mutex_unlock(&l->lock);
            free(path);
            continue;
        }

        // lock the quad of path
        for (int i = 0; i < 4; i++) {
            if (path[i] != -1) {
                // lock  the quad
                pthread_mutex_lock(&isection.quad[path[i]]);
            }
        }

        //lock all quad,then the car can out
        // insert the out_cars head
        cur->next = l->out_cars;
        l->out_cars = cur;

        // increase  the buffer head,buffer element decrease
        l->head = (l->head + 1) % LANE_LENGTH;
        l->in_buf--;
        // the car pass
        l->passed++;
    
        printf("%d %d %d\n", cur->in_dir, cur->out_dir, cur->id);

        // unlock the quad, 1 < 2 < 3 < 4, to avoid deadlock
        for (int i = 3; i >= 0; i--) {
            if (path[i] != -1) {
                pthread_mutex_unlock(&isection.quad[path[i]]);
            }
        }

        // free path
        free(path);
        
        // tell producer,car comsume finish
        pthread_cond_signal(&l->consumer_cv);

        // unlock the lane
        pthread_mutex_unlock(&l->lock);
    }

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    
    int *path;
    int i;
    
    // malloc path
    path = malloc(sizeof(int)*4);

    // init through quadrants
    for (i = 0; i < 4; ++i)
        path[i] = -1;

    // if in_dir equal out_dir, through 4 quadrants 
    if (in_dir == out_dir){
        for (i=0; i < 4; ++i)
            path[i] = i;
        return path;
    }

    //diffrent direction
    if (in_dir == NORTH) {
        if (out_dir == EAST) {
            path[0] = 1;
            path[1] = 2;
            path[2] = 3;
        }
        else if(out_dir == SOUTH) {
            path[0] = 1;
            path[1] = 2;
        }
        else if(out_dir == WEST){
            path[0] = 1;
        }
    }
    else if (in_dir == EAST) {
        if (out_dir == NORTH) {
            path[0] = 0;
        }
        else if(out_dir == SOUTH) {
            path[0] = 0;
            path[1] = 1;
            path[2] = 2;
        }
        else if(out_dir == WEST){
            path[0] = 0;
            path[1] = 1;
        }
    }
    else if(in_dir == SOUTH) {
        if (out_dir == NORTH) {
            path[0] = 0;
            path[1] = 3;
        }
        else if (out_dir == EAST) {
            path[0] = 3;
        }
        else if(out_dir == WEST){
            path[0] = 0;
            path[1] = 1;
            path[2] = 3;
        }
    }
    else if(in_dir == WEST){

        if (out_dir == NORTH) {
            path[0] = 0;
            path[1] = 2;
            path[2] = 3;
        }
        else if (out_dir == EAST) {
            path[0] = 2;
            path[1] = 3;
        }
        else if(out_dir == SOUTH) {
            path[0] = 2;
        }
    }

    return path;
}



