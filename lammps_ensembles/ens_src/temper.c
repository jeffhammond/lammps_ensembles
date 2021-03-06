/*
 * temper.c
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
#include "mpi.h"
#include "library.h"
#include "replica.h"


void temper(void *lmp, MPI_Comm subcomm, int nsteps, int nevery, int ncomms, 
            int comm, double temp, char* fix, int sseed, int dump_swap) {

/*----------------------------------------------------------------------------------
 * grab info from args
 */

    MPI_Barrier(MPI_COMM_WORLD);
    int i_nsteps = nsteps;          	// number of steps to run for
    int i_nevery = nevery;          	// frequency of swap
    double i_temp = temp;           	// temperature for this proc
    int i_comm = comm;              	// instance ID
    int i_ncomms = ncomms;          	// number of instances
    int i_temp_id = i_comm;		// temp ID

/*----------------------------------------------------------------------------------
 * determine number of swaps and check that they divide evenly into run length
 */

    int i_nswaps = i_nsteps / i_nevery;
    if(i_nswaps * i_nevery != i_nsteps) exit(1);

/*----------------------------------------------------------------------------------
 * setup update and initialize lammps
 */
 
    lammps_mod_inst(lmp, 4, NULL, "setup", &i_nsteps);
    lammps_mod_inst(lmp, 0, NULL, "init", NULL);

/*----------------------------------------------------------------------------------
 * MPI things
 */
    
    // who i am in subcomm, how many in subcomm
    int this_proc, n_procs;
    MPI_Comm_rank(subcomm, &this_proc);
    MPI_Comm_size(subcomm, &n_procs);

    // who i am in COMM_WORLD
    int this_global_proc;
    MPI_Comm_rank(MPI_COMM_WORLD, &this_global_proc);

/*----------------------------------------------------------------------------------
 * grab boltzmann constant from lammps - depends on user "units" command
 */

    double *boltz_ptr = (double *)lammps_extract_global(lmp, "boltz");
    double boltz = *boltz_ptr;
	
/*----------------------------------------------------------------------------------
 * initialize compute instance for potential energy calculation
 */
 
    lammps_mod_inst(lmp, 2, "thermo_pe", "addstep", &i_nevery);

/*----------------------------------------------------------------------------------
 * create communicator for roots on subcomms
 */

    int split_key;                                          // color argument
    MPI_Comm roots;                                         // newcomm argument
    if(this_proc == 0) split_key = 0;                       // grab roots on subcomm
    else split_key = 1;
    MPI_Comm_split(MPI_COMM_WORLD, split_key, 0, &roots);

/*----------------------------------------------------------------------------------
 * set up rngs
 */

    int i_sseed = sseed;
    int ranswapflag = 1;
    if(i_sseed == 0) ranswapflag = 0; 

    // Set up random number
    if (i_sseed < 0) {
      // If it's negative, the global root decides from the current time, 
      // broadcasts, and other ranks add their global rank. All seeds should be unique.
      int time_seed;
      if (this_global_proc == 0)
        time_seed = rng2_get_time_seed();
      MPI_Bcast(&time_seed, 1, MPI_INT, 0, MPI_COMM_WORLD);
      rng2_seed(time_seed + this_global_proc);
#ifdef COORDX_DEBUG
      if(this_global_proc == 0) printf("Time seed = %d\n", time_seed);
#endif
    }
    else {
      rng2_seed(i_sseed + this_global_proc);
    }
    

/*----------------------------------------------------------------------------------
 * create lookup tables for useful values
 */

    // world2root - return global proc of root proc on a given subcomm
    int *world2root = (int *)malloc(sizeof(int) * i_ncomms);
    if(this_proc == 0)
        MPI_Allgather(&this_global_proc, 1, MPI_INT, world2root, 1, MPI_INT, roots);
    MPI_Bcast(world2root, i_ncomms, MPI_INT, 0, subcomm);
    
    // tempid2temp - return given temperature for a given temp ID
    double *tempid2temp = (double *)malloc(sizeof(double) * i_ncomms);
    if(this_proc == 0)
        MPI_Allgather(&i_temp, 1, MPI_DOUBLE, tempid2temp, 1, MPI_DOUBLE, roots);
    MPI_Bcast(tempid2temp, i_ncomms, MPI_DOUBLE, 0, subcomm);
	
    // temp2world - return subcomm for a given temp_id
    int *world2tempid = (int *)malloc(sizeof(int) * i_ncomms);
    int *tempid2world = (int *)malloc(sizeof(int) * i_ncomms);
    if(this_proc == 0){
        MPI_Allgather(&i_temp_id, 1, MPI_INT, world2tempid, 1, MPI_INT, roots);
        int i;
        for (i = 0; i < i_ncomms; i++) tempid2world[world2tempid[i]] = i;
    }
    MPI_Bcast(tempid2world, i_ncomms, MPI_INT, 0, subcomm);
		
/*----------------------------------------------------------------------------------
 * initialize integrator
 */

    lammps_mod_inst(lmp, 3, NULL, "setup", NULL);

/*----------------------------------------------------------------------------------
 * print status header
 */

    if(this_global_proc == 0) {
	printf("Step\t\t");
	int p;
	for(p = 0; p < i_ncomms; p++)
		printf("T%d\t", p);
	printf("\n");
    }	
    // Print initial status to screen
    if(this_global_proc == 0) {
        int *current_ptr = (int *)lammps_extract_global(lmp, "ntimestep");
	printf("%d\t\t", *current_ptr);
	int p;
	for(p = 0; p < i_ncomms; p++) {
	  printf("%d\t", world2tempid[p]);
	}
	printf("\n");
     }

/*----------------------------------------------------------------------------------
 * main loop - let's do some parallel tempering fo sho
 */

    int iswap, swap, dir, p_temp_id, partner_proc, partner_comm;
    double pe, pe_partner, boltz_factor;
    double *pe_ptr;
    MPI_Status status;

    // For swapping output file names
    char *my_dumpfile      = (char *)malloc(sizeof(char) * MAXCHARS);
    char *partner_dumpfile = (char *)malloc(sizeof(char) * MAXCHARS);


    // ** The big old loop ** //
    for (iswap = 0; iswap < i_nswaps; iswap += 1) {

        // 1. run for one period of timesteps
        lammps_mod_inst(lmp, 3, NULL, "run", &i_nevery);

        // 2. extract potential energy from compute instance and add another to queue
        pe_ptr = (double *)lammps_extract_compute(lmp, "thermo_pe", 0, 0);
        pe = *pe_ptr;
        lammps_mod_inst(lmp, 2, "thermo_pe", "addstep", &i_nevery);


        // 3. determine which direction matching takes place
        // AWGL : Modified this so that only global root proc decides.
        //        Otherwise, bug b/c different procs might not end up with same
        //        direction b/c rng might be different, I think. Anyhow, it works now.
        if (this_global_proc == 0) {
          if (ranswapflag == 0)         dir = iswap % 2;
          else if (rng2() < 0.5) dir = 0;
          else                          dir = 1;
        }
        // broadcast direction to all procs
        MPI_Bcast(&dir, 1, MPI_INT, 0, MPI_COMM_WORLD);

        // 4. match partner temps
        if (dir == 0) {
          if (i_temp_id % 2 == 0) p_temp_id = i_temp_id + 1;
          else                    p_temp_id = i_temp_id - 1;
        } else {
          if (i_temp_id % 2 == 1) p_temp_id = i_temp_id + 1;
          else                    p_temp_id = i_temp_id - 1;
        }

        // 5. get partner global rank
        partner_proc = -1;
        if(this_proc == 0) {
            if(p_temp_id >= 0 && p_temp_id < i_ncomms) {
              partner_comm = tempid2world[p_temp_id];
              partner_proc = world2root[partner_comm];
            }
        }

        // 6. figure out if swap is okay
        swap = 0;
        if(partner_proc != -1) {
            if(this_proc == 0) {


                // higher proc sends pe to lower proc
                if(this_global_proc > partner_proc) {
                    MPI_Send(&pe, 1, MPI_DOUBLE, partner_proc, 0, MPI_COMM_WORLD);
                }
                // lower proc recieves
                else {
		    MPI_Recv(&pe_partner, 1, MPI_DOUBLE, partner_proc, 0, MPI_COMM_WORLD, &status);
                }

	        // lower proc does calculations
                if(this_global_proc < partner_proc) {

                    // calculate boltzmann factor
                    boltz_factor = (pe - pe_partner) * 
                                   (1.0 / (boltz * tempid2temp[i_temp_id]) -
                                    1.0 / (boltz * tempid2temp[p_temp_id]));
					double my_rand = rng2();
#ifdef TEMPER_DEBUG
	            printf("delta: %lf, probability: %lf, rand: %lf\n", boltz_factor, exp(boltz_factor), my_rand);
#endif

                    // make decision monte carlo style
                    if(boltz_factor >= 0.0) swap = 1; // criterion of e^0 or greater -> probability of 1
                    else if(my_rand < exp(boltz_factor)) swap = 1; 

                }

                // send decision to higher proc
                if(this_global_proc < partner_proc)
                    MPI_Send(&swap, 1, MPI_INT, partner_proc, 0, MPI_COMM_WORLD);
                else
                    MPI_Recv(&swap, 1, MPI_INT, partner_proc, 0, MPI_COMM_WORLD, &status);
            }
        }

        // broadcast decision to subcomm
        MPI_Bcast(&swap, 1, MPI_INT, 0, subcomm);
				
        // 7. perform swap
        if(swap == 1) {
	    // scale velocities
	    lammps_scale_velocities(lmp, tempid2temp[i_temp_id], tempid2temp[p_temp_id]);

            // reset thermostat temp
            lammps_mod_inst(lmp, 1, fix, "reset_target", &tempid2temp[p_temp_id]);

            // reset temp_id
            i_temp_id = p_temp_id;

            if (dump_swap) {
              // swap dump file names for convenience in post-processing
              if (lammps_get_dump_file(lmp) != NULL) {
                strcpy(my_dumpfile, lammps_get_dump_file(lmp));
                if (this_proc == 0) {
                    MPI_Sendrecv(my_dumpfile,      MAXCHARS, MPI_CHAR, partner_proc, 0,
                                 partner_dumpfile, MAXCHARS, MPI_CHAR, partner_proc, 0, MPI_COMM_WORLD, &status);
                }
                MPI_Bcast(partner_dumpfile, MAXCHARS, MPI_CHAR, 0, subcomm);
                lammps_change_dump_file(lmp, i_comm, partner_dumpfile);
              } else {
               printf("Error: No dump file specified for TEMPER module to swap. Check inputs.\n");
               exit(1);
              }
            }
        }

        // 8. Update lookup table
        if(this_proc == 0){
            MPI_Allgather(&i_temp_id, 1, MPI_INT, world2tempid, 1, MPI_INT, roots);
            int i;
            for (i = 0; i < i_ncomms; i++) tempid2world[world2tempid[i]] = i;
        }
        MPI_Bcast(tempid2world, i_ncomms, MPI_INT, 0, subcomm);

        // Print status to screen
        if(this_global_proc == 0) {
		int *current_ptr = (int *)lammps_extract_global(lmp, "ntimestep");
		printf("%d\t\t", *current_ptr);
		int p;
		for(p = 0; p < i_ncomms; p++) {
			printf("%d\t", world2tempid[p]);
		}
		printf("\n");
	}
    } // close iswap

/*----------------------------------------------------------------------------------
 * clean it up
 */

    if (this_global_proc == 0) {
      printf("Parallel tempering complete. Cleaning up LAMMPS...\n");
    }

    lammps_mod_inst(lmp, 3, NULL, "cleanup", NULL);
    lammps_mod_inst(lmp, 0, NULL, "finish", NULL);
    lammps_mod_inst(lmp, 4, NULL, "cleanup", NULL);

    free(world2root);
    free(tempid2temp);
    free(world2tempid);
    free(tempid2world);
    
    free(my_dumpfile);
    free(partner_dumpfile);

}
