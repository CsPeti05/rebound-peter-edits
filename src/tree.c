/**
 * @file 	tree.c
 * @brief 	Tree routine, initializing and updating trees.
 * @author 	Shangfei Liu <liushangfei@pku.edu.cn> 
 * @author  Hanno Rein <hanno@hanno-rein.de>
 * 
 * @section 	LICENSE
 * Copyright (c) 2011 Hanno Rein, Shangfei Liu
 *
 * This file is part of rebound.
 *
 * rebound is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * rebound is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "particle.h"
#include "rebound.h"
#include "boundary.h"
#include "tree.h"
#ifdef MPI
#include "communication_mpi.h"
#endif // MPI


/**
  * @brief Given a particle and a pointer to a node cell, the function returns the index of the octant which the particle belongs to.
  * @param p The particles for which the octant is calculated
  * @param node is the pointer to a node cell. 
  * @return Octant of subcell
  */
static int reb_reb_tree_get_octant_for_particle_in_cell(const struct reb_particle p, struct reb_treecell *node);

/**
  * @brief This function adds a particle to the octant[o] of a node. 
  *
  * @details If node is NULL, the function allocate memory for it and calculate its geometric properties. 
  * As a leaf node, node->pt = pt. 
  *
  * If node already exists, the function calls itself recursively until reach a leaf node.
  * The leaf node would be divided into eight octants, then it puts the leaf-node hosting particle 
  * and the new particle into these octants. 
  * @param r REBOUND simulation to operate on
  * @param node is the pointer to a node cell
  * @param pt is the index of a particle.
  * @param parent is the pointer to the parent cell of node. if node is a root, then parent
  * is set to be NULL.
  * @param o is the index of the octant of the node which particles[pt] belongs to.
  */
static struct reb_treecell *reb_tree_add_particle_to_cell(struct reb_simulation* const r, struct reb_treecell *node, int pt, struct reb_treecell *parent, int o);



void reb_tree_add_particle_to_tree(struct reb_simulation* const r, int pt){
	if (r->tree_root==NULL){
		r->tree_root = calloc(r->N_root_x*r->N_root_y*r->N_root_z,sizeof(struct reb_treecell*));
	}
	struct reb_particle p = r->particles[pt];
	int rootbox = reb_get_rootbox_for_particle(r, p);
	
#ifdef MPI
	// Do not add particles that do not belong to this tree (avoid removing active particles)
	int N_root_per_node = r->N_root/r->mpi_num;
	int proc_id = rootbox/N_root_per_node;
	if (proc_id!=r->mpi_id) return;
#endif 	// MPI
	if (r->tree_root[rootbox] == NULL) {
		reb_simulation_error(r, "Rootbox is NULL!\n");
	}
	
	r->tree_root[rootbox] = reb_tree_add_particle_to_cell(r, r->tree_root[rootbox],pt,NULL,0);

}

static struct reb_treecell *reb_tree_add_particle_to_cell(struct reb_simulation* const r, struct reb_treecell *node, int pt, struct reb_treecell *parent, int o){
	struct reb_particle* const particles = r->particles;
	const struct reb_vec3d boxsize = r->boxsize;
	const double OMEGA = r->ri_sei.OMEGA;

	// Initialize a new node
	if (node == NULL) {  
		node = calloc(1, sizeof(struct reb_treecell));
		struct reb_particle p = particles[pt];
		
		const double q = r->ri_sei.Q_NL; // Nonlinearity parameter, 0 < q < 1
		const double Rx_t = r->root_size*(1-q*cos(OMEGA*r->t));
		const double Lx_t = boxsize.x*(1-q*cos(OMEGA*r->t));
		
		if (parent == NULL){ // The new node is a root
			node->w = Rx_t;
			node->l = r->root_size;

			int i = ((int)floor((p.x + Lx_t/2.)/Rx_t))%r->N_root_x;
			int j = ((int)floor((p.y + r->boxsize.y/2.)/r->root_size))%r->N_root_y;
			int k = ((int)floor((p.z + r->boxsize.z/2.)/r->root_size))%r->N_root_z;
			
			node->x = -Lx_t/2.+Rx_t*(0.5+(double)i);
			node->y = -r->boxsize.y/2.+r->root_size*(0.5+(double)j);
			node->z = -r->boxsize.z/2.+r->root_size*(0.5+(double)k);

		}else{ // The new node is a normal node
			node->w 	= parent->w/2.;
			node->l = parent->l/2;
			node->x 	= parent->x + node->w/2.*((o>>0)%2==0?1.:-1);
			node->y 	= parent->y + node->l/2.*((o>>1)%2==0?1.:-1);
			node->z 	= parent->z + node->l/2.*((o>>2)%2==0?1.:-1);
		}
		node->pt = pt; 
		particles[pt].c = node;
		for (int i=0; i<8; i++){
			node->oct[i] = NULL;
		}

		return node;
	}
	// In a existing node
	if (node->pt >= 0) { // It's a leaf node
		int o1 = reb_reb_tree_get_octant_for_particle_in_cell(particles[node->pt], node);
		int o2 = reb_reb_tree_get_octant_for_particle_in_cell(particles[pt], node);
		if (o1==o2){ // If they fall in the same octant, check if they have same coordinates to avoid infinite recursion
			if (particles[pt].x == particles[node->pt].x && particles[pt].y == particles[node->pt].y && particles[pt].z == particles[node->pt].z){
				printf("node->pt: %d\n", node->pt);
				printf("pt: %d\n", pt);
				printf("N: %d\n", r->N);
				reb_simulation_error(r, "Cannot add two particles with the same coordinates to the tree.");
				return node;
			}
		}
		node->oct[o1] = reb_tree_add_particle_to_cell(r, node->oct[o1], node->pt, node, o1); 
		node->oct[o2] = reb_tree_add_particle_to_cell(r, node->oct[o2], pt, node, o2);
		node->pt = -2; // not a leaf node anymore pt = (-1) * total number of particles
	}else{ // It's not a leaf
		node->pt--;
		int o = reb_reb_tree_get_octant_for_particle_in_cell(particles[pt], node);
		node->oct[o] = reb_tree_add_particle_to_cell(r, node->oct[o], pt, node, o);
	}
	return node;
}

// top right is zero, top left is 1, bottom right 2, left 3, top right behind 4, top left behind 5, ...
static int reb_reb_tree_get_octant_for_particle_in_cell(const struct reb_particle p, struct reb_treecell *node){
	int octant = 0;
	if (p.x < node->x) octant+=1;
	if (p.y < node->y) octant+=2;
	if (p.z < node->z) octant+=4;
	return octant;
}

/**
  * @brief The function tests whether the particle is still within the cubic cell box. If the particle has moved outside the box, it returns 0. Otherwise, it returns 1. 
  *
  * @param r REBOUND simulation to operate on
  * @param node is the pointer to a node cell
  * @return 0 is particle is not in cell, 1 if it is.
  */
static int reb_tree_particle_is_inside_cell(const struct reb_simulation* const r, struct reb_treecell *node){
	if (fabs(r->particles[node->pt].x-node->x) > node->w/2. || 
		fabs(r->particles[node->pt].y-node->y) > node->l/2. || 
		fabs(r->particles[node->pt].z-node->z) > node->l/2. || 
		isnan(r->particles[node->pt].y)) {
		return 0;
	}
	return 1;
}

/**
  * @brief The function is called to walk through the whole tree to update its structure and node->pt at the end of each time step.
  *
  * @param r REBOUND simulation to operate on
  * @param node is the pointer to a node cell
  */
static struct reb_treecell *reb_simulation_update_tree_cell(struct reb_simulation* const r, struct reb_treecell *node, struct reb_treecell* parent){
	int test = -1; /**< A temporary int variable is used to store the index of an octant when it needs to be freed. */
	if (node == NULL) {
		return NULL;
	}
	const struct reb_vec3d boxsize = r->boxsize;
	const double OMEGA = r->ri_sei.OMEGA;
	const double q = r->ri_sei.Q_NL; // Nonlinearity parameter, 0 < q < 1
	const double Lx_t = boxsize.x*(1-q*cos(OMEGA*r->t));
	const double Rx_t = r->root_size*(1-q*cos(OMEGA*r->t));
	int num_rootboxes = r->N_root_x*r->N_root_y*r->N_root_z;
	int is_rootbox = 1;
	
	for (int i = 0; i < num_rootboxes; i++) {    
		if (node == r->tree_root[i] && r->tree_root[i]->w != Rx_t) {
			node->w = Rx_t;
			if (i % 2 == 0) {
				node->x = node->w / 2.;
			} else {
				node->x = -1.*node->w / 2.;
			}

		}
		printf("Rootbox %d.x", i);
		printf(": %f\n", 0.0);
		if (r->tree_root[i] != NULL) {
			printf("Rootbox %d.x", i);
			printf(": %f\n", r->tree_root[i]->x);
		}
	}

	// Non-leaf nodes	
	if (node->pt < 0) {
		for (int o=0; o<8; o++) {
			node->oct[o] = reb_simulation_update_tree_cell(r, node->oct[o], node);
		}

		// Update to the right number of particles
		node->pt = 0;
		for (int o=0; o<8; o++) {
			struct reb_treecell *d = node->oct[o];
			if (d != NULL) {
				// Update node->pt
				if (d->pt >= 0) {	// The child is a leaf
					node->pt--;
					test = o;
				}else{				// The child cell contains several particles
					node->pt += d->pt;
				}
			}		
		}
		// Check if the node requires derefinement.
		if (node->pt == 0) {	// The node is empty.
			free(node);
			return NULL;
		} else if (node->pt == -1) { // The node becomes a leaf.
			node->pt = node->oct[test]->pt;
			r->particles[node->pt].c = node;
			free(node->oct[test]);
			node->oct[test]=NULL;
			if (parent != NULL && node->w != parent->w/2) {
				node->w = parent->w/2;
				int oct = reb_reb_tree_get_octant_for_particle_in_cell(r->particles[node->pt], parent);
				//change center of octant depending on which octant the particle is in
				if (oct % 2 == 0) {
					node->x = parent->x + node->w / 2;
				} else {
					node->x = parent->x - node->w / 2;
				}

			}
			return node;
		}
		if (parent != NULL && node->w != parent->w/2) {
			node->w = parent->w/2;
			int oct = reb_reb_tree_get_octant_for_particle_in_cell(r->particles[node->pt], parent);
			//change center of octant depending on which octant the particle is in
			if (oct % 2 == 0) {
				node->x = parent->x + node->w / 2;
			} else {
				node->x = parent->x - node->w / 2;
			}
		}
	
		return node;
	} 
	// Leaf nodes
	if (reb_tree_particle_is_inside_cell(r, node) == 0) {
		int oldpos = node->pt;
		struct reb_particle reinsertme = r->particles[oldpos];
		if (r->N){ // Check if there remains any particle in the simulation 
			(r->N)--;
			r->particles[oldpos] = r->particles[r->N];
			r->particles[oldpos].c->pt = oldpos;
			if (!isnan(reinsertme.y)){ // Do not reinsert if flagged for removal
				reb_simulation_add(r, reinsertme);
			}
		}
		free(node);
		return NULL; 
	} else {
		r->particles[node->pt].c = node;
		if (parent != NULL && node->w != parent->w/2) {
			node->w = parent->w/2;
			int oct = reb_reb_tree_get_octant_for_particle_in_cell(r->particles[node->pt], parent);
			//change center of octant depending on which octant the particle is in
			if (oct % 2 == 0) {
				node->x = parent->x + node->w / 2;
			} else {
				node->x = parent->x - node->w / 2;
			}
		} // It is not enough to change the width, the center must be adjusted as well!
		
		return node;
	}
}

/**
  * @brief The function calculates the total mass and center of mass of a node. When QUADRUPOLE is defined, it also calculates the mass quadrupole tensor for all non-leaf nodes.
  */
static void reb_simulation_update_tree_gravity_data_in_cell(const struct reb_simulation* const r, struct reb_treecell *node){
#ifdef QUADRUPOLE
	node->mxx = 0;
	node->mxy = 0;
	node->mxz = 0;
	node->myy = 0;
	node->myz = 0;
	node->mzz = 0;
#endif // QUADRUPOLE
	if (node->pt < 0) {
		// Non-leaf nodes	
		node->m  = 0;
		node->mx = 0;
		node->my = 0;
		node->mz = 0;
		for (int o=0; o<8; o++) {
			struct reb_treecell* d = node->oct[o];
			if (d!=NULL){
				reb_simulation_update_tree_gravity_data_in_cell(r, d);
				// Calculate the total mass and the center of mass
				double d_m = d->m;
				node->mx += d->mx*d_m;
				node->my += d->my*d_m;
				node->mz += d->mz*d_m;
				node->m  += d_m;
			}
		}
		double m_tot = node->m;
		if (m_tot>0){
			node->mx /= m_tot;
			node->my /= m_tot;
			node->mz /= m_tot;
		}
#ifdef QUADRUPOLE
		for (int o=0; o<8; o++) {
			struct reb_treecell* d = node->oct[o];
			if (d!=NULL){
				// Ref: Hernquist, L., 1987, APJS
				double d_m = d->m;
				double qx  = d->mx - node->mx;
				double qy  = d->my - node->my;
				double qz  = d->mz - node->mz;
				double qr2 = qx*qx + qy*qy + qz*qz;
				node->mxx += d->mxx + d_m*(3.*qx*qx - qr2);
				node->mxy += d->mxy + d_m*3.*qx*qy;
				node->mxz += d->mxz + d_m*3.*qx*qz;
				node->myy += d->myy + d_m*(3.*qy*qy - qr2);
				node->myz += d->myz + d_m*3.*qy*qz;
			}
		}
		node->mzz = -node->mxx -node->myy;
#endif // QUADRUPOLE
	}else{ 
		// Leaf nodes
		struct reb_particle p = r->particles[node->pt];
		node->m = p.m;
		node->mx = p.x;
		node->my = p.y;
		node->mz = p.z;
	}
}

void reb_simulation_update_tree_gravity_data(struct reb_simulation* const r){
	for(int i=0;i<r->N_root;i++){
#ifdef MPI
		if (reb_communication_mpi_rootbox_is_local(r, i)==1){
#endif // MPI
			if (r->tree_root[i]!=NULL){
				reb_simulation_update_tree_gravity_data_in_cell(r, r->tree_root[i]);
			}
#ifdef MPI
		}
#endif // MPI
	}
}

void reb_simulation_update_tree(struct reb_simulation* const r){
	if (r->tree_root==NULL){
		r->tree_root = calloc(r->N_root_x*r->N_root_y*r->N_root_z,sizeof(struct reb_treecell*));
	}
	for(int i=0;i<r->N_root;i++){

#ifdef MPI
		if (reb_communication_mpi_rootbox_is_local(r, i)==1){
#endif // MPI
			r->tree_root[i] = reb_simulation_update_tree_cell(r, r->tree_root[i], NULL);
			//printf("Rootbox: %d", reb_get_rootbox_for_particle(r, r->particles[i]));
			//printf("Number of rootboxes: %d\n", sizeof(r->tree_root) / sizeof(struct reb_treecell));
#ifdef MPI
		}
#endif // MPI
	}
	r->tree_needs_update= 0;
}
static void reb_tree_delete_cell(struct reb_treecell* node){
	if (node==NULL){
		return;
	}
	if (node->remote==1){
		return;
	}
	for (int o=0; o<8; o++) {
		reb_tree_delete_cell(node->oct[o]);
	}
	free(node);
}

void reb_tree_delete(struct reb_simulation* const r){
	if (r->tree_root!=NULL){
		for(int i=0;i<r->N_root;i++){
			reb_tree_delete_cell(r->tree_root[i]);
		}
		free(r->tree_root);
		r->tree_root = NULL;
	}
}



#ifdef MPI
/**
  * @brief The function returns the index of the root which contains the cell.
  *
  * @param node is a pointer to a node cell.
  */
int reb_particles_get_rootbox_for_node(struct reb_simulation* const r, struct reb_treecell* node){
	int i = ((int)floor((node->x + r->boxsize.x/2.)/r->root_size)+r->N_root_x)%r->N_root_x;
	int j = ((int)floor((node->y + r->boxsize.y/2.)/r->root_size)+r->N_root_y)%r->N_root_y;
	int k = ((int)floor((node->z + r->boxsize.z/2.)/r->root_size)+r->N_root_z)%r->N_root_z;
	int index = (k*r->N_root_y+j)*r->N_root_x+i;
	return index;
}

/**
  * @brief The function returns the octant index of a child cell within a parent cell.
  *
  * @param nnode is a pointer to a child cell of the cell which node points to.
  * @param node is a pointer to a node cell.
  */
int reb_reb_tree_get_octant_for_cell_in_cell(struct reb_treecell* nnode, struct reb_treecell *node){
	int octant = 0;
	if (nnode->x < node->x) octant+=1;
	if (nnode->y < node->y) octant+=2;
	if (nnode->z < node->z) octant+=4;
	return octant;
}

/**
  * @brief Needs more comments!
  *
  * @param nnode is a pointer to a child cell of the cell which node points to.
  * @param node is a pointer to a node cell.
  */
void reb_tree_add_essential_node_to_node(struct reb_treecell* nnode, struct reb_treecell* node){
	int o = reb_reb_tree_get_octant_for_cell_in_cell(nnode, node);
	if (node->oct[o]==NULL){
		node->oct[o] = nnode;
	}else{
		reb_tree_add_essential_node_to_node(nnode, node->oct[o]);
	}
}

void reb_tree_add_essential_node(struct reb_simulation* const r, struct reb_treecell* node){
	node->remote = 1;
	// Add essential node to appropriate parent.
	for (int o=0;o<8;o++){
		node->oct[o] = NULL;	
	}
	int index = reb_particles_get_rootbox_for_node(r, node);
	if (r->tree_root[index]==NULL){
		r->tree_root[index] = node;
	}else{
		reb_tree_add_essential_node_to_node(node, r->tree_root[index]);
	}
}
void reb_tree_prepare_essential_tree_for_gravity(struct reb_simulation* const r){
	for(int i=0;i<r->N_root;i++){
		if (reb_communication_mpi_rootbox_is_local(r, i)==1){
			reb_communication_mpi_prepare_essential_tree_for_gravity(r, r->tree_root[i]);
		}else{
			// Delete essential tree reference. 
			// Tree itself is saved in tree_essential_recv[][] and
			// will be overwritten the next timestep.
			r->tree_root[i] = NULL;
		}
	}
}
void reb_tree_prepare_essential_tree_for_collisions(struct reb_simulation* const r){
	for(int i=0;i<r->N_root;i++){
		if (reb_communication_mpi_rootbox_is_local(r, i)==1){
			reb_communication_mpi_prepare_essential_tree_for_collisions(r, r->tree_root[i]);
		}else{
			// Delete essential tree reference. 
			// Tree itself is saved in tree_essential_recv[][] and
			// will be overwritten the next timestep.
			r->tree_root[i] = NULL;
		}
	}
}
#endif // MPI

