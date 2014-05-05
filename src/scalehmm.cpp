#include "scalehmm.h"

// ============================================================
// Hidden Markov Model implemented with scaling strategy
// ============================================================

// Public =====================================================

// Constructor and Destructor ---------------------------------
ScaleHMM::ScaleHMM(int T, int N)
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	this->T = T;
	this->N = N;
	this->A = allocDoubleMatrix(N, N);
	this->scalefactoralpha = (double*) calloc(T, sizeof(double));
	this->scalealpha = allocDoubleMatrix(T, N);
	this->scalebeta = allocDoubleMatrix(T, N);
	this->densities = allocDoubleMatrix(N, T);
	this->tdensities = allocDoubleMatrix(T, N);
	this->proba = (double*) calloc(N, sizeof(double));
	this->gamma = allocDoubleMatrix(N, T);
	this->sumgamma = (double*) calloc(N, sizeof(double));
	this->sumxi = allocDoubleMatrix(N, N);
	this->logP = -INFINITY;
	this->dlogP = INFINITY;
	this->sumdiff_state1 = 0;
	this->sumdiff_posterior = 0.0;
	this->use_tdens = false;

}

ScaleHMM::~ScaleHMM()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	freeDoubleMatrix(this->A, this->N);
	free(this->scalefactoralpha);
	freeDoubleMatrix(this->scalealpha, this->T);
	freeDoubleMatrix(this->scalebeta, this->T);
	freeDoubleMatrix(this->densities, this->N);
	freeDoubleMatrix(this->tdensities, this->T);
	freeDoubleMatrix(this->gamma, this->N);
	freeDoubleMatrix(this->sumxi, this->N);
	free(this->proba);
	free(this->sumgamma);
	for (int iN=0; iN<this->N; iN++)
	{
		FILE_LOG(logDEBUG1) << "Deleting density functions"; 
		delete this->densityFunctions[iN];
	}
}

// Methods ----------------------------------------------------
void ScaleHMM::initialize_transition_probs(double* initial_A, bool use_initial_params)
{

	if (use_initial_params)
	{
		for (int iN=0; iN<this->N; iN++)
		{
			for (int jN=0; jN<this->N; jN++)
			{
				// convert from vector to matrix representation
				this->A[iN][jN] = initial_A[iN*this->N + jN];
			}
		}
	}
	else
	{
		double self = 0.9;
// 		self = 1.0 / this->N; // set to uniform
		double other = (1.0 - self) / (this->N - 1.0);
		for (int iN=0; iN<this->N; iN++)
		{
			for (int jN=0; jN<this->N; jN++)
			{
				if (iN == jN)
					this->A[iN][jN] = self;
				else
					this->A[iN][jN] = other;
				// Save value to initial A
				initial_A[iN*this->N + jN] = this->A[iN][jN];
			}
		}
	}
	
}

void ScaleHMM::initialize_proba(double* initial_proba, bool use_initial_params)
{

	if (use_initial_params)
	{
		for (int iN=0; iN<this->N; iN++)
		{
			this->proba[iN] = initial_proba[iN];
		}
	}
	else
	{
		for (int iN=0; iN<this->N; iN++)
		{
			this->proba[iN] = (double)1/this->N;
			// Save value to initial proba
			initial_proba[iN] = this->proba[iN];
		}
	}

}

void ScaleHMM::baumWelch(int* maxiter, int* maxtime, double* eps)
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;

	double logPold = -INFINITY;
	double logPnew, dP;
	double** gammaold = allocDoubleMatrix(this->N, this->T);

	// Parallelization settings
	omp_set_nested(1);
	
	// measuring the time
	this->baumWelchStartTime_sec = time(NULL);

	// Print some initial information
	FILE_LOG(logINFO) << "";
	FILE_LOG(logINFO) << "INITIAL PARAMETERS";
	this->print_uni_params();
	this->print_uni_iteration(0);

	R_CheckUserInterrupt();

	// Do the Baum-Welch
	int iteration = 0;
	while (((this->baumWelchTime_real < *maxtime) or (*maxtime < 0)) and ((iteration < *maxiter) or (*maxiter < 0)))
	{

		iteration++;
		
		FILE_LOG(logDEBUG1) << "Calling calc_densities() from baumWelch()";
		this->calc_densities();
		R_CheckUserInterrupt();

		FILE_LOG(logDEBUG1) << "Calling forward() from baumWelch()";
		this->forward();
		R_CheckUserInterrupt();

		FILE_LOG(logDEBUG1) << "Calling backward() from baumWelch()";
		this->backward();
		R_CheckUserInterrupt();

		FILE_LOG(logDEBUG1) << "Calling calc_loglikelihood() from baumWelch()";
		this->calc_loglikelihood();
		logPnew = this->logP;
		if(isnan(logPnew))
		{
			FILE_LOG(logWARNING) << "logPnew = " << logPnew;
			break;
		}
		this->dlogP = logPnew - logPold;

		FILE_LOG(logDEBUG1) << "Calling calc_sumxi() from baumWelch()";
		this->calc_sumxi();
		R_CheckUserInterrupt();

		FILE_LOG(logDEBUG1) << "Calling calc_sumgamma() from baumWelch()";
		this->calc_sumgamma();
		R_CheckUserInterrupt();

		// difference in state assignments
		clock_t clocktime = clock(), dtime;
		FILE_LOG(logDEBUG1) << "Calculating differences in state assignments in baumWelch()";
		int state1 = 0;
		int state1old = 0;
		int statesum = 0;
		for (int t=0; t<this->T; t++)
		{
			if (this->gamma[2][t]>0.5)
			{
				state1 = 1;
			}
			if (gammaold[2][t]>0.5)
			{
				state1old = 1;
			}
			statesum += fabs(state1-state1old);
			state1 = 0;
			state1old = 0;
		}
		this->sumdiff_state1 = statesum;
		dtime = clock() - clocktime;
		FILE_LOG(logDEBUG) << "differences in state assignments: " << dtime << " clicks";

		// difference in posterior
		FILE_LOG(logDEBUG1) << "Calculating differences in posterior in baumWelch()";
		double postsum = 0.0;
		for (int t=0; t<this->T; t++)
		{
			for (int iN=0; iN<this->N; iN++)
			{
				postsum += fabs(this->gamma[iN][t] - gammaold[iN][t]);
				gammaold[iN][t] = this->gamma[iN][t];
			}
		}
		this->sumdiff_posterior = postsum;
		dtime = clock() - clocktime;
		FILE_LOG(logDEBUG) << "differences in posterior: " << dtime << " clicks";

		R_CheckUserInterrupt();

		// Print information about current iteration
		this->print_uni_iteration(iteration);

		// Check convergence
		if(fabs(this->dlogP) < *eps) //it has converged
		{
			FILE_LOG(logINFO) << "\nConvergence reached!\n";
			this->check_for_state_swap();
			break;
		}
		else
		{ // not converged
			this->baumWelchTime_real = difftime(time(NULL),this->baumWelchStartTime_sec);
			if (iteration == *maxiter)
			{
				FILE_LOG(logINFO) << "Maximum number of iterations reached!";
				this->check_for_state_swap();
			}
			else if ((this->baumWelchTime_real >= *maxtime) and (*maxtime >= 0))
			{
				FILE_LOG(logINFO) << "Exceeded maximum time!";
				this->check_for_state_swap();
			}
			logPold = logPnew;
		}
		
		// Updating initial probabilities proba and transition matrix A
		for (int iN=0; iN<this->N; iN++)
		{
			this->proba[iN] = this->gamma[iN][0];
			FILE_LOG(logDEBUG4) << "sumgamma["<<iN<<"] = " << sumgamma[iN];
			if (this->sumgamma[iN] == 0)
			{
				FILE_LOG(logINFO) << "Not reestimating A["<<iN<<"][x] because sumgamma["<<iN<<"] = 0";
			}
			else
			{
				for (int jN=0; jN<this->N; jN++)
				{
					FILE_LOG(logDEBUG4) << "sumxi["<<iN<<"]["<<jN<<"] = " << sumxi[iN][jN];
					this->A[iN][jN] = this->sumxi[iN][jN] / this->sumgamma[iN];
					if (isnan(this->A[iN][jN]))
					{
						FILE_LOG(logWARNING) << "A["<<iN<<"]["<<jN<<"] = " << A[iN][jN];
						FILE_LOG(logWARNING) << "sumxi["<<iN<<"]["<<jN<<"] = " << sumxi[iN][jN];
						FILE_LOG(logWARNING) << "sumgamma["<<iN<<"] = " << sumgamma[iN];
						exit(1);
					}
				}
			}
		}

		// Update the parameters of the distribution
		#pragma omp parallel for
		for (int iN=0; iN<this->N; iN++)
		{
			this->densityFunctions[iN]->update(this->gamma[iN]);
		}
		dtime = clock() - clocktime;
		FILE_LOG(logDEBUG) << "updating distributions: " << dtime << " clicks";
		R_CheckUserInterrupt();

	} /* main loop end */
    
    
	//Print the last results
	FILE_LOG(logINFO) << "";
	FILE_LOG(logINFO) << "FINAL ESTIMATION RESULTS";
	this->print_uni_params();

	/* free memory */
	freeDoubleMatrix(gammaold, this->N);

	// Return values
	*maxiter = iteration;
	*eps = this->dlogP;
	this->baumWelchTime_real = difftime(time(NULL),this->baumWelchStartTime_sec);
	*maxtime = this->baumWelchTime_real;
}

void ScaleHMM::calc_weights(double* weights)
{
	#pragma omp parallel for
	for (int iN=0; iN<this->N; iN++)
	{
		// Do not use weights[iN] = ( this->sumgamma[iN] + this->gamma[iN][T-1] ) / this->T; here, since states are swapped and gammas not
		double sum_over_gammas_per_state = 0;
		for (int t=0; t<this->T; t++)
		{
			sum_over_gammas_per_state += this->gamma[iN][t];
		}
		weights[iN] = sum_over_gammas_per_state / this->T;
	}
}

void ScaleHMM::check_for_state_swap()
{
// TODO
}

// Getters and Setters ----------------------------------------
void ScaleHMM::get_posteriors(double** post)
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	for (int iN=0; iN<this->N; iN++)
	{
		for (int t=0; t<this->T; t++)
		{
			post[iN][t] = this->gamma[iN][t];
		}
	}
}

double ScaleHMM::get_proba(int i)
{
	return( this->proba[i] );
}

double ScaleHMM::get_A(int i, int j)
{
	return( this->A[i][j] );
}

double ScaleHMM::get_logP()
{
	return( this->logP );
}

// Private ====================================================
// Methods ----------------------------------------------------
void ScaleHMM::forward()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	clock_t time = clock(), dtime;

	if (not this->use_tdens)
	{

		double alpha [this->N];
		// Initialization
		this->scalefactoralpha[0] = 0.0;
		for (int iN=0; iN<this->N; iN++)
		{
			alpha[iN] = this->proba[iN] * this->densities[iN][0];
			FILE_LOG(logDEBUG4) << "alpha["<<iN<<"] = " << alpha[iN];
			this->scalefactoralpha[0] += alpha[iN];
		}
		FILE_LOG(logDEBUG4) << "scalefactoralpha["<<0<<"] = " << scalefactoralpha[0];
		for (int iN=0; iN<this->N; iN++)
		{
			this->scalealpha[0][iN] = alpha[iN] / this->scalefactoralpha[0];
			FILE_LOG(logDEBUG4) << "scalealpha["<<0<<"]["<<iN<<"] = " << scalealpha[0][iN];
		}
		// Induction
		for (int t=1; t<this->T; t++)
		{
			this->scalefactoralpha[t] = 0.0;
			for (int iN=0; iN<this->N; iN++)
			{
				double helpsum = 0.0;
				for (int jN=0; jN<this->N; jN++)
				{
					helpsum += this->scalealpha[t-1][jN] * this->A[jN][iN];
				}
				alpha[iN] = helpsum * this->densities[iN][t];
				FILE_LOG(logDEBUG4) << "alpha["<<iN<<"] = " << alpha[iN];
				this->scalefactoralpha[t] += alpha[iN];
			}
			FILE_LOG(logDEBUG4) << "scalefactoralpha["<<t<<"] = " << scalefactoralpha[t];
			for (int iN=0; iN<this->N; iN++)
			{
				this->scalealpha[t][iN] = alpha[iN] / this->scalefactoralpha[t];
				FILE_LOG(logDEBUG4) << "scalealpha["<<t<<"]["<<iN<<"] = " << scalealpha[t][iN];
				if(isnan(this->scalealpha[t][iN]))
				{
					FILE_LOG(logWARNING) << __PRETTY_FUNCTION__;
					for (int jN=0; jN<this->N; jN++)
					{
						FILE_LOG(logWARNING) << "scalealpha["<<t-1<<"]["<<jN<<"] = " << scalealpha[t-1][jN];
						FILE_LOG(logWARNING) << "A["<<iN<<"]["<<jN<<"] = " << A[iN][jN];
					}
					FILE_LOG(logWARNING) << "scalefactoralpha["<<t<<"] = "<<scalefactoralpha[t] << ", densities = "<<densities[iN][t];
					FILE_LOG(logWARNING) << "scalealpha["<<t<<"]["<<iN<<"] = " << scalealpha[t][iN];
					exit(1);
				}
			}
		}

	}
	else if (this->use_tdens)
	{

		double alpha [this->N];
		// Initialization
		this->scalefactoralpha[0] = 0.0;
		for (int iN=0; iN<this->N; iN++)
		{
			alpha[iN] = this->proba[iN] * this->tdensities[0][iN];
			FILE_LOG(logDEBUG4) << "alpha["<<iN<<"] = " << alpha[iN];
			this->scalefactoralpha[0] += alpha[iN];
		}
		FILE_LOG(logDEBUG4) << "scalefactoralpha["<<0<<"] = " << scalefactoralpha[0];
		for (int iN=0; iN<this->N; iN++)
		{
			this->scalealpha[0][iN] = alpha[iN] / this->scalefactoralpha[0];
			FILE_LOG(logDEBUG4) << "scalealpha["<<0<<"]["<<iN<<"] = " << scalealpha[0][iN];
		}
		// Induction
		for (int t=1; t<this->T; t++)
		{
			this->scalefactoralpha[t] = 0.0;
			for (int iN=0; iN<this->N; iN++)
			{
				double helpsum = 0.0;
				for (int jN=0; jN<this->N; jN++)
				{
					helpsum += this->scalealpha[t-1][jN] * this->A[jN][iN];
				}
				alpha[iN] = helpsum * this->tdensities[t][iN];
				FILE_LOG(logDEBUG4) << "alpha["<<iN<<"] = " << alpha[iN];
				this->scalefactoralpha[t] += alpha[iN];
			}
			FILE_LOG(logDEBUG4) << "scalefactoralpha["<<t<<"] = " << scalefactoralpha[t];
			for (int iN=0; iN<this->N; iN++)
			{
				this->scalealpha[t][iN] = alpha[iN] / this->scalefactoralpha[t];
				FILE_LOG(logDEBUG4) << "scalealpha["<<t<<"]["<<iN<<"] = " << scalealpha[t][iN];
				if(isnan(this->scalealpha[t][iN]))
				{
					FILE_LOG(logWARNING) << __PRETTY_FUNCTION__;
					for (int jN=0; jN<this->N; jN++)
					{
						FILE_LOG(logWARNING) << "scalealpha["<<t-1<<"]["<<jN<<"] = " << scalealpha[t-1][jN];
						FILE_LOG(logWARNING) << "A["<<iN<<"]["<<jN<<"] = " << A[iN][jN];
					}
					FILE_LOG(logWARNING) << "scalefactoralpha["<<t<<"] = "<<scalefactoralpha[t] << ", tdensities = "<<tdensities[t][iN];
					FILE_LOG(logWARNING) << "scalealpha["<<t<<"]["<<iN<<"] = " << scalealpha[t][iN];
					exit(1);
				}
			}
		}

	}

	dtime = clock() - time;
	FILE_LOG(logDEBUG) << "forward(): " << dtime << " clicks";
}

void ScaleHMM::backward()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	clock_t time = clock(), dtime;

	if (not this->use_tdens)
	{

		double beta [this->N];
		// Initialization
		for (int iN=0; iN<this->N; iN++)
		{
			beta[iN] = 1.0;
			FILE_LOG(logDEBUG4) << "beta["<<iN<<"] = " << beta[iN];
		}
		FILE_LOG(logDEBUG4) << "scalefactoralpha["<<T-1<<"] = " << scalefactoralpha[T-1];
		for (int iN=0; iN<this->N; iN++)
		{
			this->scalebeta[T-1][iN] = beta[iN] / this->scalefactoralpha[T-1];
			FILE_LOG(logDEBUG4) << "scalebeta["<<T-1<<"]["<<iN<<"] = " << scalebeta[T-1][iN];
		}
		// Induction
		for (int t=this->T-2; t>=0; t--)
		{
			for (int iN=0; iN<this->N; iN++)
			{
				FILE_LOG(logDEBUG4) << "Calculating backward variable for state " << iN;
				beta[iN] = 0.0;
				for(int jN=0; jN<this->N; jN++)
				{
					beta[iN] += this->A[iN][jN] * this->densities[jN][t+1] * this->scalebeta[t+1][jN];
				}
			}
			FILE_LOG(logDEBUG4) << "scalefactoralpha["<<t<<"] = " << scalefactoralpha[t];
			for (int iN=0; iN<this->N; iN++)
			{
				this->scalebeta[t][iN] = beta[iN] / this->scalefactoralpha[t];
				FILE_LOG(logDEBUG4) << "scalebeta["<<t<<"]["<<iN<<"] = " << scalebeta[t][iN];
				if (isnan(this->scalebeta[t][iN]))
				{
					FILE_LOG(logWARNING) << __PRETTY_FUNCTION__;
					for (int jN=0; jN<this->N; jN++)
					{
						FILE_LOG(logWARNING) << "scalebeta["<<jN<<"]["<<t+1<<"] = " << scalebeta[t+1][jN];
						FILE_LOG(logWARNING) << "A["<<iN<<"]["<<jN<<"] = " << A[iN][jN];
						FILE_LOG(logWARNING) << "densities["<<jN<<"]["<<t+1<<"] = " << densities[jN][t+1];
					}
					FILE_LOG(logWARNING) << "this->scalefactoralpha[t]["<<t<<"] = "<<this->scalefactoralpha[t] << ", densities = "<<densities[iN][t];
					FILE_LOG(logWARNING) << "scalebeta["<<iN<<"]["<<t<<"] = " << scalebeta[t][iN];
					exit(1);
				}
			}
		}

	}
	else if (this->use_tdens)
	{

		double beta [this->N];
		// Initialization
		for (int iN=0; iN<this->N; iN++)
		{
			beta[iN] = 1.0;
			FILE_LOG(logDEBUG4) << "beta["<<iN<<"] = " << beta[iN];
		}
		FILE_LOG(logDEBUG4) << "scalefactoralpha["<<T-1<<"] = " << scalefactoralpha[T-1];
		for (int iN=0; iN<this->N; iN++)
		{
			this->scalebeta[T-1][iN] = beta[iN] / this->scalefactoralpha[T-1];
			FILE_LOG(logDEBUG4) << "scalebeta["<<T-1<<"]["<<iN<<"] = " << scalebeta[T-1][iN];
		}
		// Induction
		for (int t=this->T-2; t>=0; t--)
		{
			for (int iN=0; iN<this->N; iN++)
			{
				FILE_LOG(logDEBUG4) << "Calculating backward variable for state " << iN;
				beta[iN] = 0.0;
				for(int jN=0; jN<this->N; jN++)
				{
					beta[iN] += this->A[iN][jN] * this->tdensities[t+1][jN] * this->scalebeta[t+1][jN];
				}
			}
			FILE_LOG(logDEBUG4) << "scalefactoralpha["<<t<<"] = " << scalefactoralpha[t];
			for (int iN=0; iN<this->N; iN++)
			{
				this->scalebeta[t][iN] = beta[iN] / this->scalefactoralpha[t];
				FILE_LOG(logDEBUG4) << "scalebeta["<<t<<"]["<<iN<<"] = " << scalebeta[t][iN];
				if (isnan(this->scalebeta[t][iN]))
				{
					FILE_LOG(logWARNING) << __PRETTY_FUNCTION__;
					for (int jN=0; jN<this->N; jN++)
					{
						FILE_LOG(logWARNING) << "scalebeta["<<jN<<"]["<<t+1<<"] = " << scalebeta[t+1][jN];
						FILE_LOG(logWARNING) << "A["<<iN<<"]["<<jN<<"] = " << A[iN][jN];
						FILE_LOG(logWARNING) << "tdensities["<<t+1<<"]["<<jN<<"] = " << tdensities[t+1][jN];
					}
					FILE_LOG(logWARNING) << "this->scalefactoralpha[t]["<<t<<"] = "<<this->scalefactoralpha[t] << ", tdensities = "<<densities[t][iN];
					FILE_LOG(logWARNING) << "scalebeta["<<iN<<"]["<<t<<"] = " << scalebeta[t][iN];
					exit(1);
				}
			}
		}

	}

	dtime = clock() - time;
	FILE_LOG(logDEBUG) << "backward(): " << dtime << " clicks";
}

void ScaleHMM::calc_sumgamma()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	clock_t time = clock(), dtime;

	// Initialize the sumgamma
	for (int iN=0; iN<this->N; iN++)
	{
		this->sumgamma[iN] = 0.0;
	}

	// Compute the gammas (posteriors) and sumgamma
	#pragma omp parallel for
	for (int iN=0; iN<this->N; iN++)
	{
		for (int t=0; t<this->T; t++)
		{
			this->gamma[iN][t] = this->scalealpha[t][iN] * this->scalebeta[t][iN] * this->scalefactoralpha[t];
			this->sumgamma[iN] += this->gamma[iN][t];
		}
	}
	// Subtract the last value because sumgamma goes only until T-1 and we computed until T to get also loggamma at T
	for (int iN=0; iN<this->N; iN++)
	{
		this->sumgamma[iN] -= this->gamma[iN][T-1];
	}

	dtime = clock() - time;
	FILE_LOG(logDEBUG) << "calc_sumgamma(): " << dtime << " clicks";
}

void ScaleHMM::calc_sumxi()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	clock_t time = clock(), dtime;

	double xi;
	// Initialize the sumxi
	for (int iN=0; iN<this->N; iN++)
	{
		for (int jN=0; jN<this->N; jN++)
		{
			this->sumxi[iN][jN] = 0.0;
		}
	}	

	// Compute the sumxi
	if (not this->use_tdens)
	{

		#pragma omp parallel for
		for (int iN=0; iN<this->N; iN++)
		{
			FILE_LOG(logDEBUG3) << "Calculating sumxi["<<iN<<"][jN]";
			for (int t=0; t<this->T-1; t++)
			{
				for (int jN=0; jN<this->N; jN++)
				{
					xi = this->scalealpha[t][iN] * this->A[iN][jN] * this->densities[jN][t+1] * this->scalebeta[t+1][jN];
					this->sumxi[iN][jN] += xi;
				}
			}
		}

	}
	else if (this->use_tdens)
	{

		#pragma omp parallel for
		for (int iN=0; iN<this->N; iN++)
		{
			FILE_LOG(logDEBUG3) << "Calculating sumxi["<<iN<<"][jN]";
			for (int t=0; t<this->T-1; t++)
			{
				for (int jN=0; jN<this->N; jN++)
				{
					xi = this->scalealpha[t][iN] * this->A[iN][jN] * this->tdensities[t+1][jN] * this->scalebeta[t+1][jN];
					this->sumxi[iN][jN] += xi;
				}
			}
		}

	}

	dtime = clock() - time;
	FILE_LOG(logDEBUG) << "calc_sumxi(): " << dtime << " clicks";
}

void ScaleHMM::calc_loglikelihood()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	clock_t time = clock(), dtime;

	this->logP = 0.0;
	for (int t=0; t<this->T; t++)
	{
		this->logP += log(this->scalefactoralpha[t]);
	}

	dtime = clock() - time;
	FILE_LOG(logDEBUG) << "calc_loglikelihood(): " << dtime << " clicks";
}

void ScaleHMM::calc_densities()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	clock_t time = clock(), dtime;
	#pragma omp parallel for
	for (int iN=0; iN<this->N; iN++)
	{
		FILE_LOG(logDEBUG3) << "Calculating densities for state " << iN;
		this->densityFunctions[iN]->calc_densities(this->densities[iN]);
	}

	if (this->use_tdens)
	{

		for (int t=0; t<this->T; t++)
		{
			for (int iN=0; iN<this->N; iN++)
			{
				this->tdensities[t][iN] = this->densities[iN][t];
			}
		}

	}

	dtime = clock() - time;
	FILE_LOG(logDEBUG) << "calc_densities(): " << dtime << " clicks";
}

void ScaleHMM::print_uni_iteration(int iteration)
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	this->baumWelchTime_real = difftime(time(NULL),this->baumWelchStartTime_sec);
	int bs = 106;
	char buffer [bs];
	if (iteration % 20 == 0)
	{
		snprintf(buffer, bs, "%10s%20s%20s%20s%20s%15s\n", "Iteration", "log(P)", "dlog(P)", "Diff in state 1", "Diff in posterior", "Time in sec");
		FILE_LOG(logITERATION) << buffer;
	}
	snprintf(buffer, bs, "%*d%*f%*f%*d%*f%*d\n", 10, iteration, 20, this->logP, 20, this->dlogP, 20, this->sumdiff_state1, 20, this->sumdiff_posterior, 15, this->baumWelchTime_real);
	FILE_LOG(logITERATION) << buffer;
}

void ScaleHMM::print_uni_params()
{
	FILE_LOG(logDEBUG2) << __PRETTY_FUNCTION__;
	int bs = 82;
	char buffer [bs];
	int cx;
	snprintf(buffer, bs, " -------------------------------------------------------------------------------");
	FILE_LOG(logINFO) << buffer;
	snprintf(buffer, bs, "|%80s", "|");
	FILE_LOG(logINFO) << buffer;
	// print loglik
	snprintf(buffer, bs, "| log(P) = %*.6f%54s", 16, this->logP, "|");
	FILE_LOG(logINFO) << buffer;
	snprintf(buffer, bs, "|%80s", "|");
	FILE_LOG(logINFO) << buffer;
	// print initial probabilities
	cx = snprintf(buffer, bs, "|%7s", "");
	for (int iN=0; iN<this->N; iN++)
	{
		cx += snprintf(buffer+cx, bs-cx, "proba[%d] = %.6f    ", iN, this->proba[iN]);
	}
	cx += snprintf(buffer+cx, bs-cx, "   |");
	FILE_LOG(logINFO) << buffer;
	snprintf(buffer, bs, "|%80s", "|");
	FILE_LOG(logINFO) << buffer;
	// print transition probabilities
	for (int iN=0; iN<this->N; iN++)
	{
		cx = snprintf(buffer, bs, "|%7s", "");
		for (int jN=0; jN<this->N; jN++)
		{
			cx += snprintf(buffer+cx, bs-cx, "A[%d][%d] = %.6f    ", iN, jN, this->A[iN][jN]);
		}
		cx += snprintf(buffer+cx, bs-cx, "      |");
		FILE_LOG(logINFO) << buffer;
	}
	// print emission parameters
	snprintf(buffer, bs, "|%80s", "|");
	FILE_LOG(logINFO) << buffer;
	for (int iN=0; iN<this->N; iN++)
	{
		if (iN == 1)
		{
			snprintf(buffer, bs, "| unmodified component%59s", "|");
			FILE_LOG(logINFO) << buffer;
		}
		if (iN == 2)
		{
			snprintf(buffer, bs, "| modified component%61s", "|");
			FILE_LOG(logINFO) << buffer;
		}
		double curMean = this->densityFunctions[iN]->get_mean();
		double curVar = this->densityFunctions[iN]->get_variance();
		snprintf(buffer, bs, "| mean = %*.2f, var = %*.2f%20s", 6, curMean, 8, curVar, "|");
		FILE_LOG(logINFO) << buffer;
	}
	
	snprintf(buffer, bs, "|%80s", "|");
	FILE_LOG(logINFO) << buffer;
	snprintf(buffer, bs, " -------------------------------------------------------------------------------");
	FILE_LOG(logINFO) << buffer;
	FILE_LOG(logINFO) << "";
}

