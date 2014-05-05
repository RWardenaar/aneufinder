#ifndef SCALEHMM_H
#define SCALEHMM_H

#include <vector> // needed for storing the density functions
using std::vector;
#include <R.h> // R_CheckUserInterrupt()
#include "utility.h"
#include "densities.h"
#include "logging.h"
#include <omp.h>

class ScaleHMM  {

	public:
		// Constructor and Destructor
		ScaleHMM(int T, int N);
		~ScaleHMM();

		// Member variables
		vector<Density*> densityFunctions; ///< density functions for each state

		// Methods
		void initialize_transition_probs(double* initial_A, bool use_initial_params);
		void initialize_proba(double* initial_proba, bool use_initial_params);
		void baumWelch(int* maxiter, int* maxtime, double* eps);
		void calc_weights(double* weights);
		void check_for_state_swap();

		// Getters and Setters
		void get_posteriors(double** post);
		double get_proba(int i);
		double get_A(int i, int j);
		double get_logP();

	private:
		// Member variables
		int T; ///< length of observed sequence
		int* obs; ///< vector [T] of observations
		int N; ///< number of states
		double** A; ///< matrix [N x N] of transition probabilities
		double* proba; ///< initial probabilities (length N)
		double logP; ///< loglikelihood
		double* scalefactoralpha; ///< vector[T] of scaling factors
		double** scalealpha; ///< matrix [T x N] of forward probabilities
		double** scalebeta; ///<  matrix [T x N] of backward probabilities
		double** densities; ///< matrix [N x T] of density values
		double** tdensities; ///< matrix [T x N] of density values, for use in multivariate
		double* sumgamma; ///< vector[N] of sum of posteriors (gamma values)
		double** sumxi; ///< matrix[N x N] of xi values
		double** gamma; ///< matrix[N x T] of posteriors
		double dlogP; ///< difference in loglikelihood from one iteration to the next
		time_t baumWelchStartTime_sec; ///< start time of the Baum-Welch in sec
		int baumWelchTime_real; ///< elapsed time from start of the 0th iteration
		int sumdiff_state1; ///< sum of the difference in the state 1 assignments from one iteration to the next
		double sumdiff_posterior; ///< sum of the difference in posterior (gamma) values from one iteration to the next
		bool use_tdens; ///< switch for using the tdensities in the calculations

		// Methods
		void forward(); ///< calculate forward variables (alpha)
		void backward(); ///< calculate backward variables (beta)
		void calc_sumgamma();
		void calc_sumxi();
		void calc_loglikelihood();
		void calc_densities();
		void print_uni_iteration(int iteration);
		void print_uni_params();
};

#endif