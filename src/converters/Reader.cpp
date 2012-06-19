#include "Reader.h"
#include <sys/types.h>
#include <sys/stat.h>

#if defined (__WIN32__) || defined (__MINGW__) 
#include <direct.h>
#include <io.h>
#include <stdio.h>
#define  mkdir( D, M )   _mkdir( D )
#include <fcntl.h>
#include <errno.h>
#endif


Reader::Reader()
{
  tmpDirs = std::vector<char*>();
  tmpFNs = std::vector<std::string>();
  aaAlphabet = std::string("ACDEFGHIKLMNPQRSTVWY");
  ambiguousAA = std::string("BZJX");
  modifiedAA = std::string("#@*");
}


Reader::~Reader()
{
  //NOTE do this with boost?
  for(int i=0; i<tmpDirs.size(); i++)
    rmdir(tmpDirs[i]);
}


void Reader::setFile(std::string file)
{
  tmpFNs.push_back(file);
}

void Reader::translateSqtFileToXML(const std::string fn,
    ::percolatorInNs::featureDescriptions & fds,
     ::percolatorInNs::experiment::fragSpectrumScan_sequence & fsss, bool isDecoy,
      const ParseOptions & po, int * maxCharge,  int * minCharge, parseType pType,
      vector<FragSpectrumScanDatabase*>& databases, unsigned int lineNumber_par
      /*,std::vector<char*>& DBtDirs, std::vector<std::string>& DBTmpFNs*/){
  
  std::ifstream fileIn(fn.c_str(), std::ios::in);
  if (!fileIn) {
    std::cerr << "Could not open file " << fn << std::endl;
    exit(-1);
  }
  std::string line;
  if (!getline(fileIn, line)) {
    std::cerr << "Could not read file " << fn << std::endl;
    exit(-1);
  }
  fileIn.close();
  if (line.size() > 1 && line[0]=='H' && (line[1]=='\t' || line[1]==' ')) {
    if (line.find("SQTGenerator") == std::string::npos) {
      std::cerr << "SQT file not generated by SEQUEST: " << fn << std::endl;
      exit(-1);
    }
    // there must be as many databases as lines in the metafile containing sqt
    // files. If this is not the case, add a new one
    if(databases.size()==lineNumber_par){
      // create temporary directory to store the pointer to the database
      string tcf = "";
      char * tcd;
      string str;
      //NOTE this is actually not needed in case we compile with the boost-serialization scheme
      try
      {
        boost::filesystem::path ph = boost::filesystem::unique_path();
	boost::filesystem::path dir = boost::filesystem::temp_directory_path() / ph;
	boost::filesystem::path file("percolator-tmp.tcb");
	tcf = std::string((dir / file).string()); 
	str =  dir.string();
	tcd = new char[str.size() + 1];
	std::copy(str.begin(), str.end(), tcd);
	tcd[str.size()] = '\0';
	if(boost::filesystem::is_directory(dir))
	{
	  boost::filesystem::remove_all(dir);
	}
	
	boost::filesystem::create_directory(dir);
      } 
      catch (boost::filesystem::filesystem_error &e)
      {
	std::cerr << e.what() << std::endl;
      }	

      tmpDirs.resize(lineNumber_par+1);
      tmpDirs[lineNumber_par]=tcd;
      tmpFNs.resize(lineNumber_par+1);
      tmpFNs[lineNumber_par]=tcf;
      // initialize databese     
      FragSpectrumScanDatabase* database = new FragSpectrumScanDatabase(fn);
      database->init(tmpFNs[lineNumber_par]);
      databases.resize(lineNumber_par+1);
      databases[lineNumber_par]=database;
      assert(databases.size()==lineNumber_par+1);
    }
    if (VERB>1 && pType == Reader::fullParsing){
      std::cerr << "reading " << fn << std::endl;
    }
    read(fn,fds, fsss, isDecoy, po, maxCharge, minCharge, pType,
        databases[lineNumber_par]);
  } else {
    // we hopefully found a meta file
    unsigned int lineNumber=0;
    std::string line2;
    std::ifstream meta(fn.data(), std::ios::in);
    while (getline(meta, line2)) {
      if (line2.size() > 0 && line2[0] != '#') {
	//NOTE remove the whitespaces
	line2.erase(std::remove(line2.begin(),line2.end(),' '),line2.end());
        translateSqtFileToXML(line2, fds, fsss, isDecoy, po, maxCharge,
            minCharge, pType, databases, lineNumber/*, tmpDirs,tmpFNs*/);
        lineNumber++;
      }
    }
    meta.close();
  }
}



void Reader::push_backFeatureDescription( percolatorInNs::featureDescriptions::featureDescription_sequence  & fd_sequence , const char * str) {

  std::auto_ptr< ::percolatorInNs::featureDescription > f_p( new ::percolatorInNs::featureDescription(str));
  assert(f_p.get());
  fd_sequence.push_back(f_p);
  return;
}

void Reader::addFeatureDescriptions( percolatorInNs::featureDescriptions & fe_des, int minC, int maxC, bool doEnzyme,
    bool calcPTMs, bool doPNGaseF,
    const std::string& aaAlphabet,
    bool calcQuadratic) {
  
  size_t numFeatures;
  int minCharge, maxCharge;
  int chargeFeatNum, enzFeatNum, numSPFeatNum, ptmFeatNum, pngFeatNum,
  aaFeatNum, intraSetFeatNum, quadraticFeatNum, docFeatNum;

  percolatorInNs::featureDescriptions::featureDescription_sequence  & fd_sequence =  fe_des.featureDescription();

  push_backFeatureDescription( fd_sequence, "lnrSp");
  push_backFeatureDescription( fd_sequence,"deltLCn");
  push_backFeatureDescription( fd_sequence,"deltCn");
  push_backFeatureDescription( fd_sequence,"Xcorr");
  push_backFeatureDescription( fd_sequence,"Sp");
  push_backFeatureDescription( fd_sequence,"IonFrac");
  push_backFeatureDescription( fd_sequence,"Mass");
  push_backFeatureDescription( fd_sequence,"PepLen");

  minCharge = minC;
  maxCharge = maxC;

  for (int charge = minCharge; charge <= maxCharge; ++charge) {
    std::ostringstream cname;
    cname << "Charge" << charge;
    push_backFeatureDescription( fd_sequence,cname.str().c_str());

  }
  if (doEnzyme) {
    enzFeatNum = fd_sequence.size();
    push_backFeatureDescription( fd_sequence,"enzN");
    push_backFeatureDescription( fd_sequence,"enzC");
    push_backFeatureDescription( fd_sequence,"enzInt");
  }
  numSPFeatNum = fd_sequence.size();
  push_backFeatureDescription( fd_sequence,"lnNumSP");
  push_backFeatureDescription( fd_sequence,"dM");
  push_backFeatureDescription( fd_sequence,"absdM");
  if (calcPTMs) {
    ptmFeatNum = fd_sequence.size();
    push_backFeatureDescription( fd_sequence,"ptm");
  }
  if (doPNGaseF) {
    pngFeatNum = fd_sequence.size();
    push_backFeatureDescription( fd_sequence,"PNGaseF");
  }
  if (!aaAlphabet.empty()) {
    aaFeatNum = fd_sequence.size();
    for (std::string::const_iterator it = aaAlphabet.begin(); it
    != aaAlphabet.end(); it++)
      push_backFeatureDescription( fd_sequence,*it + "-Freq");
  }
  if (calcQuadratic) {
    quadraticFeatNum = fd_sequence.size();
    for (int f1 = 1; f1 < quadraticFeatNum; ++f1) {
      for (int f2 = 0; f2 < f1; ++f2) {
        std::ostringstream feat;
        feat << "f" << f1 + 1 << "*" << "f" << f2 + 1;
        push_backFeatureDescription( fd_sequence,feat.str().c_str());
      }
    }
  }
}

/**
 * remove non ASCII characters from a string
 */
string Reader::getRidOfUnprintables(string inpString) {
  string outputs = "";
  for (int jj = 0; jj < inpString.size(); jj++) {
    signed char ch = inpString[jj];
    if (((int)ch) >= 32 && ((int)ch) <= 128) {
      outputs += ch;
    }
  }
  return outputs;
}



void Reader::computeAAFrequencies(const string& pep,   percolatorInNs::features::feature_sequence & f_seq ) {
  // Overall amino acid composition features

  assert(pep.size() >= 5);
  string::size_type aaSize = aaAlphabet.size();

  std::vector< double > doubleV;
  for ( int m = 0  ; m < aaSize ; m++ )  {
    doubleV.push_back(0.0);
  }
  int len = 0;
  for (string::const_iterator it = pep.begin() + 2; it != pep.end() - 2; it++) {
    string::size_type pos = aaAlphabet.find(*it);
    if (pos != string::npos) doubleV[pos]++;
    len++;
  }
  assert(len>0);
  for ( int m = 0  ; m < aaSize ; m++ )  {
    doubleV[m] /= len;
  }
  std::copy(doubleV.begin(), doubleV.end(), std::back_inserter(f_seq));
}
