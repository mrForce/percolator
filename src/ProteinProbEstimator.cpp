/*******************************************************************************
 Copyright 2006-2009 Lukas Käll <lukas.kall@cbr.su.se>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 *******************************************************************************/

#include <iostream>
#include <fstream>
#include "ProteinProbEstimator.h"

/** Helper functions **/

template<class T> void bootstrap(const vector<T>& in, vector<T>& out,
                                 size_t max_size = 1000) {
  out.clear();
  double n = in.size();
  size_t num_draw = min(in.size(), max_size);
  for (size_t ix = 0; ix < num_draw; ++ix) {
    size_t draw = (size_t)((double)rand() / ((double)RAND_MAX + (double)1) * n);
    out.push_back(in[draw]);
  }
  // sort in desending order
  sort(out.begin(), out.end());
}

double antiderivativeAt(double m, double b, double xVal)
{
  return m*xVal*xVal/2.0 + b*xVal;
}

double squareAntiderivativeAt(double m, double b, double xVal)
{
  // turn into ux^2+vx+t
  double u = m*m;
  double v = 2*m*b;
  double t = b*b;

  return u*xVal*xVal*xVal/3.0 + v*xVal*xVal/2.0 + t*xVal;
}

double area(double x1, double y1, double x2, double y2, double max_x)
{
  double m = (y2-y1)/(x2-x1);
  double b = y1-m*x1;

  double area =  antiderivativeAt(m, b, min(max_x, x2) ) - antiderivativeAt(m, b, x1);
  
  if(isnan(area)) return 0.0;
  else return area;
}

double areaSq(double x1, double y1, double x2, double y2, double threshold) {
  double m = (y2-y1)/(x2-x1);
  double b = y1-m*x1;
  double area = squareAntiderivativeAt(m, b, min(threshold, x2) ) -
      squareAntiderivativeAt(m, b, x1);
  if(isnan(area)) return 0.0;
  else return area;
}

double fac(int n)
{
    double t=1;
    for (int i=n;i>1;i--)
        t*=i;
    return t;
}

double Bin(int n,double p,int r)
{
    return fac(n)/(fac(n-r)*fac(r))*pow(p,r)*pow(1-p,n-r);
}


ProteinProbEstimator::ProteinProbEstimator(double alpha_par, double beta_par, double gamma_par ,bool __tiesAsOneProtein
			 ,bool __usePi0, bool __outputEmpirQVal, bool __groupProteins, bool __noseparate, bool __noprune, 
			  bool __dogridSearch, unsigned __deepness, double __lambda, double __threshold, unsigned __rocN,
			  std::string __targetDB, std::string __decoyDB, std::string __decoyPattern, bool __mayufdr, bool __conservative) {
  peptideScores = 0;
  proteinGraph = 0;
  gamma = gamma_par;
  alpha = alpha_par;
  beta = beta_par;
  numberDecoyProteins = 0;
  numberTargetProteins = 0;
  pi0 = 1.0;
  tiesAsOneProtein = __tiesAsOneProtein;
  usePi0 = __usePi0;
  outputEmpirQVal = __outputEmpirQVal;
  groupProteins = __groupProteins;
  noseparate = __noseparate;
  noprune = __noprune;
  dogridSearch = __dogridSearch;
  deepness = __deepness;
  lambda = __lambda;
  threshold = __threshold;
  targetDB = __targetDB;
  decoyDB = __decoyDB;
  decoyPattern = __decoyPattern;
  mayufdr = __mayufdr;
  conservative = __conservative;
  if(__rocN > 0)
  {
    updateRocN = false;
    rocN = __rocN;
  }
  else
  {
    updateRocN = true;
  }
}

ProteinProbEstimator::~ProteinProbEstimator()
{
  
  if(proteinGraph)
    delete proteinGraph;
  
  FreeAll(qvalues);
  FreeAll(qvaluesEmp);
  FreeAll(pvalues);
  
  if(mayufdr && fastReader)
    delete fastReader;
  
  for(std::multimap<double,std::vector<std::string> >::iterator it = pepProteins.begin();
      it != pepProteins.end(); it++)
      {
	FreeAll(it->second);
      }
      
  for(std::map<const std::string,Protein*>::iterator it = proteins.begin(); 
      it != proteins.end(); it++)
      {
	if(it->second)
	  delete it->second;
      }
}


bool ProteinProbEstimator::initialize(Scores* fullset){
  // percolator's peptide level statistics
  peptideScores = fullset;
  setTargetandDecoysNames();
  proteinGraph = new GroupPowerBigraph (peptideScores,alpha,beta,gamma,groupProteins,noseparate,noprune);
}

void ProteinProbEstimator::run(){
  
  time_t startTime;
  clock_t startClock;
  time(&startTime);
  startClock = clock();

  if(mayufdr)
  { 
    /** MAYUS method for estimation of Protein FDR **/
    
    fastReader = new ProteinFDRestimator();
    
    /* Default configuration (changeable by functions)
     * min peptide lenght = 4
     * min mass = 400
     * max mass = 6000
     * decoy prefix = random
     * num missed cleavages = 0
     * number of bins = 10
     * target decoy ratio = 1.0
     * binning mode = equal deepth
     * correct identical sequences = true
     */
    
    if(decoyPattern.size() > 0) fastReader->setDecoyPrefix(decoyPattern);
    
    std::cerr << "\nEstimating Protein FDR using Mayu's method described in : http://prottools.ethz.ch/muellelu/web/LukasReiter/Mayu/\n" << std::endl;
    
    if(decoyDB == "" && targetDB != "")
      fastReader->parseDataBase(targetDB.c_str());
    else if(targetDB != "" && decoyDB != "")
      fastReader->parseDataBase(targetDB.c_str(),decoyDB.c_str());
    else
    {  
      std::cerr << "\nError database file could not be loaded\n" << std::endl;
      exit(-1);
    }
    //These guys are the number of target and decoys proteins but from the subset of PSM with FDR < threshold
    std::pair<std::set<std::string>,std::set<std::string> > TPandPF = getTPandPFfromPeptides(psmThresholdMayu);
    
    double fptol = fastReader->estimateFDR(TPandPF.first,TPandPF.second);
    
    if(fptol == -1)
    {
      pi0 = 1.0;
      std::cerr << "\nThere was an error estimating the Protein FDR..\n" << std::endl;
    }
    else
    {	
      pi0 = (fptol/(double)TPandPF.first.size());
      
      if(pi0 <= 0 || pi0 >= 1.0) pi0 = 1.0;
      
      if(VERB > 1)
	std::cerr << "\nEstimated Protein FDR at ( " << psmThresholdMayu << ") PSM FDR is : " << pi0 << " with " 
	<< fptol << " expected number of false positives proteins\n" << std::endl;
    }
    
    /** MAYUS method for estimationg of Protein FDR**/
  }
  
  if(dogridSearch) {
    if(VERB > 1) {
      std::cerr << "\nThe parameters for the model will be estimated by grid search.\n"
          << endl;
    }
    gridSearch(alpha,gamma,beta);
    time_t procStart;
    clock_t procStartClock = clock();
    time(&procStart);
    double diff = difftime(procStart, startTime);
    if (VERB > 1) cerr << "\nEstimating the parameters took : "
      << ((double)(procStartClock - startClock)) / (double)CLOCKS_PER_SEC
      << " cpu seconds or " << diff << " seconds wall time\n" << endl;
  }
  /*else
  {
      alpha = default_alpha;
      beta = default_beta;
      gamma = default_gamma;
  }*/
  if(VERB > 1) {
      cerr << "\nThe following parameters have been chosen;\n";
      cerr << "gamma = " << gamma << endl;
      cerr << "alpha = " << alpha << endl;
      cerr << "beta  = " << beta << endl;
      cerr << "\nProtein level probabilities will now be calculated\n";
  }

  proteinGraph->setAlphaBetaGamma(alpha,beta,gamma);
  proteinGraph->getProteinProbs();
  pepProteins.clear();
  pepProteins = proteinGraph->getProteinProbsPercolator();
  
  estimateQValues();
  
  if(usePi0 && !mayufdr)
  {
    estimatePValues();
    pi0 = estimatePi0();
    if(pi0 <= 0.0 || pi0 > 1.0) pi0 = *qvalues.rbegin();
  }
  
  //if(ProteinProbEstimator::getOutputEmpirQval())
  estimateQValuesEmp();
  updateProteinProbabilities();
  proteinGraph->printProteinWeights();
  
  if(VERB > 1)
  {
    std::cerr << "\nThe number of Proteins idenfified below q=0.01 is : " << getQvaluesBelowLevel(0.01) << std::endl;
  }
  
  time_t procStart;
  clock_t procStartClock = clock();
  time(&procStart);
  double diff = difftime(procStart, startTime);
  if (VERB > 1) cerr << "Estimating Protein Probabilities took : "
    << ((double)(procStartClock - startClock)) / (double)CLOCKS_PER_SEC
    << " cpu seconds or " << diff << " seconds wall time" << endl;
}


void ProteinProbEstimator::estimatePValues()
{
  // assuming combined sorted in best hit first order
  std::vector<std::pair<double , bool> > combined;
  for (std::multimap<double,std::vector<std::string> >::const_iterator it = pepProteins.begin();
       it != pepProteins.end(); it++)
  {
     double prob = it->first;
     std::vector<std::string> proteinList = it->second;
     for(std::vector<std::string>::const_iterator itP = proteinList.begin();
	itP != proteinList.end(); itP++)
      {
	std::string proteinName = *itP;
	bool isdecoy = proteins[proteinName]->getIsDecoy();
	combined.push_back(std::make_pair<double,bool>(prob,isdecoy));
      }
  }
  pvalues.clear();
  vector<pair<double, bool> >::const_iterator myPair = combined.begin();
  size_t nDecoys = 0, posSame = 0, negSame = 0;
  double prevScore = -4711.4711; // number that hopefully never turn up first in sequence
  while (myPair != combined.end()) {
    if (myPair->first != prevScore) {
      for (size_t ix = 0; ix < posSame; ++ix) {
        pvalues.push_back((double)nDecoys + (((double)negSame)
            / (double)(posSame + 1)) * (ix + 1));
      }
      nDecoys += negSame;
      negSame = 0;
      posSame = 0;
      prevScore = myPair->first;
    }
    if (myPair->second) {
      ++negSame;
    } else {
      ++posSame;
    }
    ++myPair;
  }
  transform(pvalues.begin(), pvalues.end(), pvalues.begin(), bind2nd(divides<double> (),
                                                   (double)nDecoys));
}

std::pair<std::set<std::string>,std::set<std::string> > ProteinProbEstimator::getTPandPFfromPeptides(double threshold)
{
  //NOTE I am extracting the proteins from the unique peptides not the psms as in MAYU
  std::set<std::string> numberTP;
  std::set<std::string> numberFP;
  for (std::map<std::string,Protein*>::const_iterator it = proteins.begin();
       it != proteins.end(); it++)
  {
     std::string protname = it->first;
     std::vector<Protein::Peptide*> peptides = it->second->getPeptides();
     for(std::vector<Protein::Peptide*>::const_iterator itP = peptides.begin();
	itP != peptides.end(); itP++)
      {
	Protein::Peptide *p = *itP;
	if(p->q <= threshold)
	{
	  if(it->second->getIsDecoy())numberFP.insert(protname);
	  else numberTP.insert(protname);
	  break;
	}
      }

  }
  
  return std::make_pair<std::set<std::string>,std::set<std::string> >(numberTP,numberFP);
}


/*double ProteinProbEstimator::estimatePi0Bin(unsigned FP,unsigned TP)
{
    unsigned tempPi0 = Bin((FP + TP), (FP / (FP + TP)), FP);
    std::cerr << "Temp Pi0 : " << tempPi0;
    if(tempPi0 >= 0.0 && tempPi0 <= 1.0) return tempPi0;
    else return -1.0;
}*/


double ProteinProbEstimator::estimatePi0(const unsigned int numBoot) 
{
  vector<double> pBoot, lambdas, pi0s, mse;
  vector<double>::iterator start;
  int numLambda = 100;
  double maxLambda = 0.5;
  size_t n = pvalues.size();
  // Calculate pi0 for different values for lambda
  // N.B. numLambda and maxLambda are global variables.
  for (unsigned int ix = 0; ix <= numLambda; ++ix) {
    double lambda = ((ix + 1) / (double)numLambda) * maxLambda;
    // Find the index of the first element in p that is < lambda.
    // N.B. Assumes p is sorted in ascending order.
    start = lower_bound(pvalues.begin(), pvalues.end(), lambda);
    // Calculates the difference in index between start and end
    double Wl = (double)distance(start, pvalues.end());
    double pi0 = Wl / n / (1 - lambda);
    if (pi0 > 0.0) {
      lambdas.push_back(lambda);
      pi0s.push_back(pi0);
    }
  }
  if(pi0s.size()==0){
    cerr << "Error in the input data: too good separation between target "
        << "and decoy PSMs.\nImpossible to estimate pi0. Taking the highest estimated q value as pi0.\n";
    return -1;
  }
  double minPi0 = *min_element(pi0s.begin(), pi0s.end());
  // Initialize the vector mse with zeroes.
  fill_n(back_inserter(mse), pi0s.size(), 0.0);
  // Examine which lambda level that is most stable under bootstrap
  for (unsigned int boot = 0; boot < numBoot; ++boot) {
    // Create an array of bootstrapped p-values, and sort in ascending order.
    bootstrap<double> (pvalues, pBoot);
    n = pBoot.size();
    for (unsigned int ix = 0; ix < lambdas.size(); ++ix) {
      start = lower_bound(pBoot.begin(), pBoot.end(), lambdas[ix]);
      double Wl = (double)distance(start, pBoot.end());
      double pi0Boot = Wl / n / (1 - lambdas[ix]);
      // Estimated mean-squared error.
      mse[ix] += (pi0Boot - minPi0) * (pi0Boot - minPi0);
    }
  }
  // Which index did the iterator get?
  unsigned int minIx = distance(mse.begin(), min_element(mse.begin(),
                                                         mse.end()));
  double pi0 = max(min(pi0s[minIx], 1.0), 0.0);
  return pi0;
}

unsigned ProteinProbEstimator::getQvaluesBelowLevel(double level)
{   
    unsigned nP = 0;
    for (std::map<const std::string,Protein*>::const_iterator myP = proteins.begin(); 
	 myP != proteins.end(); ++myP) {
	 if(myP->second->getQ() <= level && !myP->second->getIsDecoy()) nP++;
    }
    return nP;
}

unsigned ProteinProbEstimator::getQvaluesBelowLevelDecoy(double level)
{   
    unsigned nP = 0;
    for (std::map<const std::string,Protein*>::const_iterator myP = proteins.begin(); 
	 myP != proteins.end(); ++myP) {
	 if(myP->second->getQ() <= level && myP->second->getIsDecoy()) nP++;
    }
    return nP;
}


void ProteinProbEstimator::estimateQValues()
{
  unsigned nP = 0;
  double sum = 0.0;
  double qvalue = 0.0;
  qvalues.clear();

  for (std::multimap<double,std::vector<std::string> >::const_iterator it = pepProteins.begin(); 
       it != pepProteins.end(); it++) 
  {
    
    if(tiesAsOneProtein)
    {
      int ntargets = countTargets(it->second);
      sum += (double)(it->first * ntargets);
      nP += ntargets;
      
      //NOTE in case I want to count target and decoys while estimateing qvalue from PEP
      /*int ntargets = countTargets(it->second);
      int ndecoys = countDecoys(it->second);
      sum += (double)(it->first * (ntargets + ndecoys));
      nP += (ntargets + ndecoys);*/
      
      qvalue = (sum / (double)nP);
      if(isnan(qvalue) || isinf(qvalue) || qvalue > 1.0) qvalue = 1.0;
      qvalues.push_back(qvalue);
    }    
    else
    {
      std::vector<std::string> proteins = it->second;
      for(std::vector<std::string>::const_iterator it2 = proteins.begin(); it2 != proteins.end(); it2++)
      {
	std::string protein = *it2;
	if(countTargets(protein))
	{
	  sum += it->first;
	  nP++;
	}
	//NOTE in case I want to count target and decoys while estimateing qvalue from PEP
	/*sum += it->first;
	nP++;*/
	
	qvalue = (sum / (double)nP);
	if(isnan(qvalue) || isinf(qvalue) || qvalue > 1.0) qvalue = 1.0;
	qvalues.push_back(qvalue);
      }
    }

  }
  std::partial_sum(qvalues.rbegin(),qvalues.rend(),qvalues.rbegin(),myminfunc);
}

void ProteinProbEstimator::estimateQValuesEmp()
{
    // assuming combined sorted in decending order
  unsigned nDecoys = 0;
  unsigned numTarget = 0;
  unsigned nTargets = 0;
  double qvalue = 0.0;
  unsigned numDecoy = 0;
  pvalues.clear();
  qvaluesEmp.clear();
  double TargetDecoyRatio = (double)numberTargetProteins / (double)numberDecoyProteins;
 
  for (std::multimap<double,std::vector<std::string> >::const_iterator it = pepProteins.begin(); 
       it != pepProteins.end(); it++) 
  {

    if(tiesAsOneProtein)
    {
      numTarget = countTargets(it->second);
      numDecoy = countDecoys(it->second);
      nDecoys += numDecoy;
      nTargets += numTarget;
      
      if(nTargets) qvalue = (double)(nDecoys * pi0 * TargetDecoyRatio) / (double)nTargets;
      if(isnan(qvalue) || isinf(qvalue) || qvalue > 1.0) qvalue = 1.0;
      
      qvaluesEmp.push_back(qvalue);
      
      if(numDecoy > 0)
        pvalues.push_back((nDecoys)/(double)(numberDecoyProteins));
      else 
        pvalues.push_back((nDecoys+(double)1)/(numberDecoyProteins+(double)1));
    }
    else
    {
      std::vector<std::string> proteins = it->second;
      for(std::vector<std::string>::const_iterator it2 = proteins.begin(); it2 != proteins.end(); it2++)
      {
	 std::string protein = *it2;
	 if(countDecoys(protein))
	 {  
	   nDecoys++;
	   pvalues.push_back((nDecoys)/(double)(numberDecoyProteins));
	 }
	 else
	 {
	   nTargets++;
	   pvalues.push_back((nDecoys+(double)1)/(numberDecoyProteins+(double)1));
	 }
	 
	 if(nTargets) qvalue = (double)(nDecoys * pi0 * TargetDecoyRatio) / (double)nTargets;
	 if(isnan(qvalue) || isinf(qvalue) || qvalue > 1.0) qvalue = 1.0;
	 qvaluesEmp.push_back(qvalue);
      }
    }
  }
  std::partial_sum(qvaluesEmp.rbegin(), qvaluesEmp.rend(), qvaluesEmp.rbegin(), myminfunc);
}

void ProteinProbEstimator::updateProteinProbabilities()
{
  std::vector<double> peps;
  std::vector<std::vector<std::string> > proteinNames;
  transform(pepProteins.begin(), pepProteins.end(), back_inserter(peps), RetrieveKey());
  transform(pepProteins.begin(), pepProteins.end(), back_inserter(proteinNames), RetrieveValue());
  unsigned qindex = 0;
  for (unsigned i = 0; i < peps.size(); i++) 
  {
    double pep = peps[i];
    std::vector<std::string> proteinlist = proteinNames[i];
    for(unsigned j = 0; j < proteinlist.size(); j++)
    { 
      std::string proteinName = proteinlist[j];
      if(tiesAsOneProtein)
      {
	proteins[proteinName]->setPEP(pep);
	proteins[proteinName]->setQ(qvalues[i]);
	proteins[proteinName]->setQemp(qvaluesEmp[i]);
	proteins[proteinName]->setP(pvalues[i]);
      }
      else
      {	
	proteins[proteinName]->setPEP(pep);
	proteins[proteinName]->setQ(qvalues[qindex]);
	proteins[proteinName]->setQemp(qvaluesEmp[qindex]);
	proteins[proteinName]->setP(pvalues[qindex]);
      }
      qindex++;
    }
  }
  
  for(unsigned i = 0; i < proteinNames.size(); i++)
    FreeAll(proteinNames[i]);
  FreeAll(proteinNames);
  FreeAll(peps);
}



std::map<const std::string,Protein*> ProteinProbEstimator::getProteins()
{
  return this->proteins;
}


void ProteinProbEstimator::setTargetandDecoysNames()
{
  vector<ScoreHolder>::iterator psm = peptideScores->begin();
  
  for (; psm!= peptideScores->end(); ++psm) {
    
    set<string>::iterator protIt = psm->pPSM->proteinIds.begin();
    // for each protein
    for(; protIt != psm->pPSM->proteinIds.end(); protIt++)
    {
      Protein::Peptide *peptide = new Protein::Peptide(psm->pPSM->getPeptideSequence(),psm->isDecoy(),
									       psm->pPSM->pep,psm->pPSM->q,psm->pPSM->p);
      if(proteins.find(*protIt) == proteins.end())
      {
	Protein *newprotein = new Protein(*protIt,0.0,0.0,0.0,0.0,psm->isDecoy(),peptide);
	proteins.insert(std::make_pair<std::string,Protein*>(*protIt,newprotein));
	
	if(psm->isDecoy())
	{
	  falsePosSet.insert(*protIt);
	}
	else
	{
	  truePosSet.insert(*protIt);
	}
      }
      else
      {
	proteins[*protIt]->setPeptide(peptide);
      }
    }
  }
  numberDecoyProteins = falsePosSet.size();
  numberTargetProteins = truePosSet.size();
}

void ProteinProbEstimator::gridSearch(double __alpha,double __gamma,double __beta)
{
 
  double gamma_best, alpha_best, beta_best;
  gamma_best = alpha_best = beta_best = -1.0;
  double best_objective = -100000000;

  double gamma_search[] = {0.5};
  double beta_search[] = {0.0, 0.01, 0.15, 0.025, 0.05};
  double alpha_search[] = {0.01, 0.04, 0.16, 0.25, 0.36};
  
  if(deepness == 0)
  {
    double gamma_search[] = {0.1,0.25, 0.5, 075, 0.9};
    double beta_search[] = {0.0, 0.01, 0.15, 0.025,0.35,0.05,0.1};
    double alpha_search[] = {0.01, 0.04,0.09, 0.16, 0.25, 0.36,0.5};
  }
  else if(deepness == 1)
  {
    double gamma_search[] = {0.1, 0.25, 0.5, 075};
    double beta_search[] = {0.0, 0.01, 0.15, 0.020, 0.025, 0.05};
    double alpha_search[] = {0.01, 0.04, 0.09, 0.16, 0.25, 0.36};
  }
  else if(deepness == 2)
  {
    double gamma_search[] = {0.1, 0.5, 0.75};
    double beta_search[] = {0.0, 0.01, 0.15, 0.025, 0.05};
    double alpha_search[] = {0.01, 0.04, 0.16, 0.25, 0.36};
  }
  else if(deepness == 3)
  {
    double gamma_search[] = {0.5};
    double beta_search[] = {0.0, 0.01, 0.15, 0.025, 0.05};
    double alpha_search[] = {0.01, 0.04, 0.16, 0.25, 0.36};
    
  }

  if(__alpha != -1)
    double alpha_search[] = {__alpha};
  if(__beta != -1)
    double beta_search[] = {__beta};
  if(__gamma != -1)
    double gamma_search[] = {__gamma};
  
  for (unsigned int i=0; i<sizeof(gamma_search)/sizeof(double); i++)
  {
    for (unsigned int j=0; j<sizeof(alpha_search)/sizeof(double); j++)
    {
      for (unsigned int k=0; k<sizeof(beta_search)/sizeof(double); k++)
      {
	gamma = gamma_search[i];
	alpha = alpha_search[j];
	beta = beta_search[k];
	
	if(VERB > 2)
	  std::cerr << "Grid searching : " << alpha << " " << beta << " " << gamma << std::endl;
	
	proteinGraph->setAlphaBetaGamma(alpha, beta, gamma);
	proteinGraph->getProteinProbs();
	std::pair< std::vector< std::vector< std::string > >, std::vector< double > > NameProbs;
	NameProbs = proteinGraph->getProteinProbsAndNames();
	std::vector<double> prot_probs = NameProbs.second;
	std::vector<std::vector<std::string> > prot_names = NameProbs.first;
	
	std::pair<std::vector<double>,std::vector<double> > EstEmp = getEstimated_and_Empirical_FDR(prot_names,prot_probs);
	std::pair<std::vector<unsigned>, std::vector<unsigned> > roc = getROC(prot_names);
	
	double rocR = getROC_N(roc.first, roc.second, rocN);
	double fdr_mse = getFDR_divergence(EstEmp.first, EstEmp.second, threshold);
	double current_objective = lambda * rocR - ((1-lambda) * (fdr_mse));
	
	if (current_objective > best_objective)
	{
	  best_objective = current_objective;
	  gamma_best = gamma;
	  alpha_best = alpha;
	  beta_best = beta;
	}
	
	if(VERB > 2)
	  std::cerr << "Roc " << rocN <<" , MSE and objective function value " << " : " << rocR << " " << fdr_mse << " " << current_objective << std::endl;
	
	FreeAll(prot_names);
	FreeAll(prot_probs);
	FreeAll(roc.first);
	FreeAll(roc.second);
	FreeAll(EstEmp.first);
	FreeAll(EstEmp.second);
      }
    }
  }
  
  //if(gpb)delete gpb;
  alpha = alpha_best;
  beta = beta_best;
  gamma = gamma_best;
}


/**
 * output protein level probabilites results in xml format
 */
void ProteinProbEstimator::writeOutputToXML(string xmlOutputFN){
  
  
  std::vector<std::pair<std::string,Protein*> > myvec(proteins.begin(), proteins.end());
  std::sort(myvec.begin(), myvec.end(), IntCmpProb());

  ofstream os;
  os.open(xmlOutputFN.data(), ios::app);
  // append PROTEINs tag
  os << "  <proteins>" << endl;
  for (std::vector<std::pair<std::string,Protein*> > ::const_iterator myP = myvec.begin(); 
	 myP != myvec.end(); myP++) {

        os << "    <protein p:protein_id=\"" << myP->second->getName() << "\"";
  
        if (Scores::isOutXmlDecoys()) {
          if(myP->second->getIsDecoy()) os << " p:decoy=\"true\"";
          else  os << " p:decoy=\"false\"";
        }
        os << ">" << endl;
        os << "      <pep>" << myP->second->getPEP() << "</pep>" << endl;
        if(ProteinProbEstimator::getOutputEmpirQval())
          os << "      <q_value_emp>" << myP->second->getQemp() << "</q_value_emp>\n";
        os << "      <q_value>" << myP->second->getQ() << "</q_value>\n";
	os << "      <p_value>" << myP->second->getP() << "</p_value>\n";
	std::vector<Protein::Peptide*> peptides = myP->second->getPeptides();
	for(std::vector<Protein::Peptide*>::const_iterator peptIt = peptides.begin(); peptIt != peptides.end(); peptIt++)
	{
	  if((*peptIt)->name != "")
	  {
	    os << "      <peptide_seq seq=\"" << (*peptIt)->name << "\"/>"<<endl;
	  }
	    
	}
        os << "    </protein>" << endl;
  }
    
  os << "  </proteins>" << endl << endl;
  os.close();
  
  FreeAll(myvec);
}


string ProteinProbEstimator::printCopyright(){
  ostringstream oss;
  oss << "Copyright (c) 2008-9 University of Washington. All rights reserved.\n"
      << "Written by Oliver R. Serang (orserang@u.washington.edu) in the\n"
      << "Department of Genome Sciences at the University of Washington.\n" << std::endl;
  return oss.str();
}


double ProteinProbEstimator::getROC_N(const std::vector<unsigned> &fpArray, const std::vector<unsigned> &tpArray, int N)
{
  double rocNvalue = 0.0;

  if ( fpArray.back() < N )
    {
      std::cerr << "There are not enough false positives; needed " << N << " and was only given " << fpArray.back() << std::endl << std::endl;
      exit(1);
    }

  for (int k=0; k<fpArray.size()-1; k++)
    {
      if ( fpArray[k] >= N )
	break;
      
      if ( fpArray[k] != fpArray[k+1] )
	{
	  double currentArea = area(fpArray[k], tpArray[k], fpArray[k+1], tpArray[k+1], N);
	  rocNvalue += currentArea;
	}
    }
  return rocNvalue / (N * tpArray.back());
}

pair<std::vector<double>, std::vector<double> > ProteinProbEstimator::getEstimated_and_Empirical_FDR(const std::vector<std::vector<string> > &names, 
								   const std::vector<double> &probabilities)
{
  std::vector<double> estFDR_array, empFDR_array;
  double fpCount = 0.0, tpCount = 0.0;
  double totalFDR = 0.0, estFDR = 0.0, empFDR = 0.0;
  double TargetDecoyRatio = (double)numberTargetProteins / (double)numberDecoyProteins;
  double previousEmpQ = 0.0;
  double previousEstQ = 0.0;
  
  for (int k=0; k<names.size(); k++)
    {
      double prob = probabilities[k];

      if(tiesAsOneProtein)
      {
	unsigned fpChange = countDecoys(names[k]);
	unsigned tpChange = countTargets(names[k]);
      
	fpCount += (double)fpChange;
	tpCount += (double)tpChange;
	
	totalFDR += (prob) * (double)(tpChange);
	estFDR = totalFDR / (tpCount);
	
	//NOTE in case I want to count target and decoys while estimateing qvalue from PEP
	/*totalFDR += (prob) * (double)(tpChange + fpChange);
	estFDR = totalFDR / (tpCount + fpCount);*/
	
	if(tpCount) empFDR = (fpCount * pi0 * TargetDecoyRatio) / tpCount; 
	
	if(empFDR > 1.0 || isnan(empFDR) || isinf(empFDR)) empFDR = 1.0;
	if(estFDR > 1.0 || isnan(estFDR) || isinf(estFDR)) estFDR = 1.0;
	    
	if(estFDR < previousEstQ) estFDR = previousEstQ;
	else previousEstQ = estFDR;
	    
	if(empFDR < previousEmpQ) empFDR = previousEmpQ;
	else previousEmpQ = empFDR;
	
	if(estFDR <= thresholdRoc && updateRocN) rocN = (unsigned)std::max(rocN,(unsigned)std::max(50,std::min((int)fpCount,1000)));
	
	estFDR_array.push_back(estFDR);
	empFDR_array.push_back(empFDR);
	
	//NOTE no need to store more q values since they will not be taken into account while estimating MSE FDR divergence
	if(estFDR > threshold) break;
      }
      else
      {
	for(unsigned i=0; i<names[k].size(); i++)
	{
	    std::string protein = names[k][i];
	    if(countDecoys(protein))
	    {  
	      fpCount++;
	    }
	    else
	    {
	      tpCount++;
	      totalFDR += (prob);
	      estFDR = totalFDR / (tpCount);
	    }
	    
	    //NOTE in case I want to count target and decoys while estimateing qvalue from PEP
	    /*totalFDR += (prob);
	    estFDR = totalFDR / (tpCount + fpCount);*/
	    
	    if(tpCount) empFDR = (fpCount * pi0 * TargetDecoyRatio) / tpCount; 
	    
	    if(empFDR > 1.0 || isnan(empFDR) || isinf(empFDR)) empFDR = 1.0;
	    if(estFDR > 1.0 || isnan(estFDR) || isinf(estFDR)) estFDR = 1.0;
	    
	    if(estFDR < previousEstQ) estFDR = previousEstQ;
	    else previousEstQ = estFDR;
	    
	    if(empFDR < previousEmpQ) empFDR = previousEmpQ;
	    else previousEmpQ = empFDR;
	    
	    if(estFDR <= thresholdRoc && updateRocN) rocN = (unsigned)std::max(rocN,(unsigned)std::max(50,std::min((int)fpCount,1000)));
	    
	    estFDR_array.push_back(estFDR);
	    empFDR_array.push_back(empFDR);
	    
	    //NOTE no need to store more q values since they will not be taken into account while estimating MSE FDR divergence
	    if(estFDR > threshold) break;
	 }
      }
	
    }
   
  //NOTE more elegant that checking the previous Q values but it consumes more time 
  //std::partial_sum(estFDR_array.rbegin(),estFDR_array.rend(),estFDR_array.rbegin(),myminfunc);
  //std::partial_sum(empFDR_array.rbegin(),empFDR_array.rend(),empFDR_array.rbegin(),myminfunc);
  
  return std::pair<std::vector<double>, std::vector<double> >(estFDR_array, empFDR_array);
}

std::vector<double> diffVector(const std::vector<double> &a, 
			       const std::vector<double> &b)
{
  std::vector<double> result = std::vector<double>(a.size());

  for (int k=0; k<result.size(); k++)
    {
      result[k] = a[k] - b[k];
    }
  
  return result;
}

double ProteinProbEstimator::getFDR_divergence(const std::vector<double> &estFDR, const std::vector<double> &empFDR, double THRESH)
{
  //std::vector<double> diff = diffVector(estFDR,empFDR);
  Vector diff = Vector(estFDR) - Vector(empFDR);
  double tot = 0.0;
  for (int k=0; k<diff.size()-1; k++)
    {
      // stop if no part of the estFDR is < threshold
      if ( estFDR[k] >= THRESH )
      {
	  if ( k == 0 )
	    tot = 1.0 / 0.0;

	  break;
      }
 
      if(conservative)
	tot += area(estFDR[k], diff[k], estFDR[k+1], diff[k+1], estFDR[k+1]);
      else
	tot += areaSq(estFDR[k], diff[k], estFDR[k+1], diff[k+1], estFDR[k+1]);
      
    }

  double xRange = min(THRESH, estFDR[k]) - estFDR[0];

  if ( isinf(tot) )
    return tot;

  return tot / xRange;
}


std::pair<std::vector<unsigned>, std::vector<unsigned> > ProteinProbEstimator::getROC(const std::vector<std::vector<string> > &names)
{
  std::vector<unsigned> fps, tps;
  unsigned fpCount, tpCount;
  fpCount = tpCount = 0;
  
  for (int k=0; k<names.size(); k++)
    {
      unsigned fpChange = countDecoys(names[k]);
      unsigned tpChange = countTargets(names[k]);
      //NOTE possible alternative is to only sum up when the new prob is different that the previous one
      fpCount += fpChange;
      tpCount += tpChange;
      
      fps.push_back( fpCount );
      tps.push_back( tpCount );
      
      //NOTE no need to store more fp since they will not be taken into account while estimating the ROC curve divergence
      if(fpCount > rocN) break;
    }

  fps.push_back( fpCount );
  tps.push_back( tpCount );	  
  fps.push_back( falsePosSet.size() );
  tps.push_back( truePosSet.size() );
  
  return std::pair<std::vector<unsigned>, std::vector<unsigned> >(fps, tps);
}


void ProteinProbEstimator::setOutputEmpirQval(bool outputEmpirQVal)
{
  this->outputEmpirQVal = outputEmpirQVal;
}

void ProteinProbEstimator::setTiesAsOneProtein(bool tiesAsOneProtein)
{
  this->tiesAsOneProtein = tiesAsOneProtein;
}

void ProteinProbEstimator::setUsePio(bool usePi0)
{
  this->usePi0 = usePi0;
}

void ProteinProbEstimator::setGroupProteins(bool groupProteins)
{
  this->groupProteins = groupProteins;
}

void ProteinProbEstimator::setPruneProteins(bool noprune)
{
  this->noprune = noprune;
}

void ProteinProbEstimator::setSeparateProteins(bool noseparate)
{
  this->noseparate = noseparate;
}

bool ProteinProbEstimator::getOutputEmpirQval()
{
  return this->outputEmpirQVal;
}

bool ProteinProbEstimator::getTiesAsOneProtein()
{
  return this->tiesAsOneProtein;
}

bool ProteinProbEstimator::getUsePio()
{
  return this->usePi0;
}

double ProteinProbEstimator::getPi0()
{
  return this->pi0;
}

bool ProteinProbEstimator::getGroupProteins()
{
  return this->groupProteins;
}


bool ProteinProbEstimator::getPruneProteins()
{
  return this->noprune;
}

bool ProteinProbEstimator::getSeparateProteins()
{
  return this->noseparate;
}


unsigned ProteinProbEstimator::countTargets(const std::vector<std::string> &proteinList)
{
  int count = 0;
  
  for(int i = 0; i < proteinList.size(); i++)
  {
    /*if(truePosSet.find(proteinList[i]) != truePosSet.end())
    {
      count++;
    }*/
    
    //NOTE an alternative faster version, forcing the user to submit the decoy prefix
    if(proteinList[i].find(decoyPattern) == std::string::npos)
    {
      count++;
    }
  }
   
  return count;
}

unsigned ProteinProbEstimator::countDecoys(const std::vector<std::string> &proteinList)
{
  int count = 0;
  
  for(int i = 0; i < proteinList.size(); i++)
  {
    /*if(falsePosSet.find(proteinList[i]) != falsePosSet.end())
    {
      count++;
    }*/
    
    //NOTE an alternative faster version, forcing the user to submit the decoy prefix
    if(proteinList[i].find(decoyPattern) != std::string::npos)
    {
      count++;
    }
  }
  
  return count;
}

unsigned ProteinProbEstimator::countTargets(std::string protein)
{
  return (unsigned)(protein.find(decoyPattern) == string::npos);
}

unsigned ProteinProbEstimator::countDecoys(std::string protein)
{
  return (unsigned)(protein.find(decoyPattern) != string::npos);
}

double ProteinProbEstimator::getAlpha()
{
 return alpha;
}

double ProteinProbEstimator::getBeta()
{
  return beta;
}

double ProteinProbEstimator::getGamma()
{
  return gamma;
}

string ProteinProbEstimator::getDecoyDB()
{
  return decoyDB;
}

string ProteinProbEstimator::getDecoyPatter()
{
  return decoyPattern;
}

bool ProteinProbEstimator::getDeepness()
{
  return deepness;
}

double ProteinProbEstimator::getLambda()
{
  return lambda;
}

bool ProteinProbEstimator::getMayuFdr()
{
  return mayufdr;
}

unsigned int ProteinProbEstimator::getROCN()
{
  return rocN;
}

bool ProteinProbEstimator::getGridSearch()
{
  return dogridSearch;
}

string ProteinProbEstimator::getTargetDB()
{
  return targetDB;
}

double ProteinProbEstimator::getThreshold()
{
  return threshold;
}

void ProteinProbEstimator::setDecoyDb(string decoyDB)
{
  this->decoyDB = decoyDB;
}

void ProteinProbEstimator::setDeepness(unsigned int deepness)
{
  this->deepness = deepness;
}

void ProteinProbEstimator::setLambda(double lambda)
{
  this->lambda = lambda;
}

void ProteinProbEstimator::setROCN(double rocn)
{
  this->rocN = rocn;
}

void ProteinProbEstimator::setGridSearch(bool dogridSearch)
{
  this->dogridSearch = dogridSearch;
}

void ProteinProbEstimator::setMayusFDR(bool mayufdr)
{
  this->mayufdr = mayufdr;
}

void ProteinProbEstimator::setTargetDb(string targetDB)
{
  this->targetDB = targetDB;
}

void ProteinProbEstimator::setThreshold(double threshold)
{
  this->threshold = threshold;
}
