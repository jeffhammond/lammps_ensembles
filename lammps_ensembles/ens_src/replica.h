/*
 * replica.h
 * This file is part of lammps-ensembles
 *
 * Copyright (C) 2012 - Mladen Rasic & Luke Westby
 *
 * lammps-ensembles is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * lammps-ensembles is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lammps-ensembles; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mpi.h"
#include "library.h"

#ifndef __REPLICA_H__
#define __REPLICA_H__

#define MAX_FILE_LEN 100
#define MAXCHARS 256

// LAMMPS data type for timestep, may need to adjust manually to 32-bit for machine dependence
#include <inttypes.h>
typedef int64_t bigint;
// LAMMPS data type for tag int, image flags
typedef int tagint;
#define MPI_LMP_TAGINT MPI_INT

// Dimension types
#define DIM_NONE    0
#define DIM_REUS    1
#define DIM_TEMPER  2
#define DIM_LAMBDA  3 

/*
 *  Data structure for a replica
 */

typedef struct {
  int comm;             // Subcommunicator index for this replica
  int id;               // Identification number for this replica
  int N_dimensions;     // Number of dimensions this replica can exchange along
  double temperature;   // Temperature of this replica
  double lambda;        // Scaling constant, lambda
  int reus_dim;         // Dimension for REUS 
  int temp_dim;         // Dimension for temperature exchange
  int lambda_dim;       // Dimension for scaling exchange (i.e. lambda exchange)
  int *neighbors;       // List of neighbors in each dimension. Restricted to 2 neighbors (+ and -)
  int *dim_num;         // Dimension index for each dimension, starts at 0 
  int *dim_run;         // How long to run for each dimension
  int *dim_nevery;      // Frequency of swapping in each dimension
} Replica;


/*
 *  Functions
 */ 

void   temper(void *, MPI_Comm, int, int, int, int, double, char *, int, int);
double rng(int *);
int    rng2_get_time_seed();
void   rng2_seed(int);
double rng2();
void   anneal(void *, MPI_Comm, char *, char *, int, int, int, int, int, int, double, double);
char   *command_generate(char *, int);

double get_bias(void *, char *, int);
void ReadInputFiles(int*, char **, int, int, int *, int *, int *, char *);
void ReadReplica(Replica *, MPI_Comm, int, int, char *);
void coord_exchange(void *, MPI_Comm, int, int, Replica *, char *, int);
void colvar_exchange(void *, MPI_Comm, int, int, Replica *, char *, int);
void relambda(void *, MPI_Comm, char*, int, int, int, int, double, char*, int, double);

/*
 *  REUS functions
 */
void reus  (void *, MPI_Comm, char*, int, int, int, int, double, char *, int, int, int, int, int, int);
void write_to_colvar_init(double, double, int);
void write_to_colvar(double, double, double, int, int);
void write_to_colvar_init_vec(double*, double*, double*, int, char*);
void write_to_colvar_vec(bigint, double*, double, double, int, int, char*);
void get_umbrella_data(void*, char*, double*, double*, double*, double*, double*, double*, int);
void mreus (void *, MPI_Comm, int, int, char *, char *, char *, int, int, int, int, int, int, Replica *);



#endif /* __REPLICA_H__ */


