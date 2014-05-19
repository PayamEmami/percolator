/*******************************************************************************
 Copyright 2006-2012 Lukas Käll <lukas.kall@scilifelab.se>

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

#include "Caller.h"
#ifndef _WIN32
  #include "unistd.h"
#endif
#include <iomanip>
#include <set>
#include <sys/types.h>
#include <sys/stat.h>
#include <boost/filesystem.hpp>

using namespace std;
      
Caller::Caller() :
        pNorm(NULL), pCheck(NULL), protEstimator(NULL),
        forwardTabInputFN(""), tabFN(""), psmResultFN(""), 
        peptideResultFN(""), proteinResultFN(""), decoyPsmResultFN(""), 
        decoyPeptideResultFN(""), decoyProteinResultFN(""),
        weightFN(""), tabInput(false), readStdIn(false),
        reportUniquePeptides(true), target_decoy_competition(false),
        test_fdr(0.01), threshTestRatio(0.3), trainRatio(0.6) {
}

Caller::~Caller() {
  if (pNorm) {
    delete pNorm;
  }
  pNorm = NULL;
  if (pCheck) {
    delete pCheck;
  }
  pCheck = NULL;
  if (protEstimator) {
    delete protEstimator;
  }
  protEstimator = NULL;
  if(readStdIn) {
    boost::filesystem::remove_all(xmlInputDir);
    delete xmlInputDir;
  }
  xmlInputDir = NULL;
}

string Caller::extendedGreeter() {
  ostringstream oss;
  char* host = getenv("HOSTNAME");
  oss << greeter();
  oss << "Issued command:" << endl << call << endl;
  oss << "Started " << ctime(&startTime) << endl;
  oss.seekp(-1, ios_base::cur);
  if(host) oss << " on " << host << endl;
  crossValidation.printParameters(oss);
  return oss.str();
}

string Caller::greeter() {
  ostringstream oss;
  oss << "Percolator version " << VERSION << ", ";
  oss << "Build Date " << __DATE__ << " " << __TIME__ << endl;
  oss << "Copyright (c) 2006-9 University of Washington. All rights reserved.\n"
      << "Written by Lukas Käll (lukall@u.washington.edu) in the\n"
      << "Department of Genome Sciences at the University of Washington.\n";
  return oss.str();
}

bool Caller::parseOptions(int argc, char **argv) {
  ostringstream callStream;
  callStream << argv[0];
  for (int i = 1; i < argc; i++) {
    callStream << " " << argv[i];
  }
  callStream << endl;
  call = callStream.str();
  call = call.substr(0,call.length()-1); // trim ending carriage return
  ostringstream intro, endnote;
  intro << greeter() << endl << "Usage:" << endl;
  intro << "   percolator [-X pout.xml] [other options] pin.tsv" << endl;
  intro << "pin.tsv is the tab delimited output file generated by e.g. sqt2pin;" << endl;
  intro << "  The tab delimited fields should be id <tab> label <tab> scannr <tab> feature1" << endl;
  intro << "  <tab> ... <tab> featureN <tab> peptide <tab> proteinId1 <tab> .. <tab> proteinIdM" << endl;
  intro << "  Labels are interpreted as 1 -- positive set and test set, -1 -- negative set." << endl;
  intro << "  When the --doc option the first and second feature (fourth and fifth column) should contain" << endl;
  intro << "  the retention time and difference between observed and calculated mass;" << endl;
  intro << "pout.xml is where the output will be written (ensure to have read and write access on the file)." << endl;
  // init
  CommandLineParser cmd(intro.str());
  // available lower case letters: c, h, l, o, y, z
  // available upper case letters: L
  // N.B.: "W" is used twice, once for Fido and once for Percolator
  cmd.defineOption("X",
      "xmloutput",
      "path to file in xml-output format (pout)",
      "filename");
  cmd.defineOption("e",
      "stdinput",
      "read xml-input format (pin) from standard input",
      "",
      TRUE_IF_SET);
  cmd.defineOption("Z",
      "decoy-xml-output",
      "Include decoys (PSMs, peptides and/or proteins) in the xml-output. Only available if -X is used.",
      "",
      TRUE_IF_SET);
  cmd.defineOption("p",
      "Cpos",
      "Cpos, penalty for mistakes made on positive examples. Set by cross validation if not specified.",
      "value");
  cmd.defineOption("n",
      "Cneg",
      "Cneg, penalty for mistakes made on negative examples. Set by cross validation if not specified or -p not specified.",
      "value");
  cmd.defineOption("F",
      "trainFDR",
      "False discovery rate threshold to define positive examples in training. Set by cross validation if 0. Default is 0.01.",
      "value");
  cmd.defineOption("t",
      "testFDR",
      "False discovery rate threshold for evaluating best cross validation result and the reported end result. Default is 0.01.",
      "value"); 
  cmd.defineOption("i",
      "maxiter",
      "Maximal number of iterations",
      "number");
  cmd.defineOption("x",
      "quick-validation",
      "Quicker execution by reduced internal cross-validation.",
      "",
      TRUE_IF_SET);
  cmd.defineOption("f",
      "train-ratio",
      "Fraction of the negative data set to be used as train set when only providing one negative set, \
      remaining examples will be used as test set. Set to 0.6 by default.",
      "value");
  cmd.defineOption("J",
      "tab-out",
      "Output the computed features to the given file in tab-delimited format. A file with the features with the given file name will be created",
      "filename");
  cmd.defineOption("j",
      "tab-in [default]",
      "Input files are given as a tab delimited file. This is the default setting, flag is only present for backwards compatibility.",
      "filename");
  cmd.defineOption("k",
      "xml-in",
      "Input file given in the deprecated pin-xml format generated by e.g. sqt2pin with the -k option",
      "filename");
  cmd.defineOption("w",
      "weights",
      "Output final weights to the given file",
      "filename");
  cmd.defineOption("W",
      "init-weights",
      "Read initial weights from the given file (one per line)",
      "filename");
  cmd.defineOption("V",
      "default-direction",
      "The most informative feature given as the feature name, can be negated to indicate that a lower value is better.",
      "[-]?featureName");
  cmd.defineOption("v",
      "verbose",
      "Set verbosity of output: 0=no processing info, 5=all, default is 2",
      "level");
  cmd.defineOption("u",
      "unitnorm",
      "Use unit normalization [0-1] instead of standard deviation normalization",
      "",
      TRUE_IF_SET);
  cmd.defineOption("R",
      "test-each-iteration",
      "Measure performance on test set each iteration",
      "",
      TRUE_IF_SET);
  cmd.defineOption("O",
      "override",
      "Override error check and do not fall back on default score vector in case of suspect score vector",
      "",
      TRUE_IF_SET);
  cmd.defineOption("S",
      "seed",
      "Setting seed of the random number generator. Default value is 1",
      "value");
  cmd.defineOption("K",
      "klammer",
      "Retention time features calculated as in Klammer et al.",
      "",
      TRUE_IF_SET);
  cmd.defineOption("D",
      "doc",
      "Include description of correct features.",
      "",
      MAYBE,
      "15");
  cmd.defineOption("r",
      "results-peptides",
      "Output tab delimited results of peptides to a file instead of stdout (will be ignored if used with -U option)",
      "filename");
  cmd.defineOption("m",
      "results-psms",
      "Output tab delimited results of PSMs to a file instead of stdout",
      "filename");
  cmd.defineOption("B",
      "decoy-results-peptides",
      "Output tab delimited results for decoy peptides into a file (will be ignored if used with -U option)",
      "filename");
  cmd.defineOption("M",
      "decoy-results-psms",
      "Output tab delimited results for decoy PSMs into a file",
      "filename");
  cmd.defineOption("U",
      "only-psms",
      "Do not remove redundant peptides, keep all PSMS and exclude peptide level probabilities.",
      "",
      FALSE_IF_SET);
  cmd.defineOption("s",
      "no-schema-validation",
      "skip validation of input file against xml schema.",
      "",
      TRUE_IF_SET);
  cmd.defineOption("A",
      "protein",
      "output protein level probabilities",
      "",
      TRUE_IF_SET);
  cmd.defineOption("a",
      "fido-alpha",
      "Probability with which a present protein emits an associated peptide (to be used jointly with the -A option) \
       Set by grid search if not specified.",
      "value");
  cmd.defineOption("b",
      "fido-beta",
      "Probability of the creation of a peptide from noise (to be used jointly with the -A option). Set by grid search if not specified",
      "value");
  cmd.defineOption("G",
      "fido-gamma",
      "Prior probability of that a protein is present in the sample ( to be used with the -A option). Set by grid search if not specified",
      "value");
  cmd.defineOption("g",
      "allow-protein-group",
      "treat ties as if it were one protein (Only valid if option -A is active).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("I",
      "protein-level-pi0",
      "use pi_0 value when calculating empirical q-values (no effect if option Q is activated) (Only valid if option -A is active).",
      "", 
      TRUE_IF_SET);
  cmd.defineOption("q",
      "empirical-protein-q", 		   
      "output empirical q-values and p-values (from target-decoy analysis) (Only valid if option -A is active).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("N",
      "fido-no-group-proteins", 		   
      "disactivates the grouping of proteins with similar connectivity, \
       for example if proteins P1 and P2 have the same peptides matching both of them, P1 and P2 will not be grouped as one protein \
       (Only valid if option -A is active).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("E",
      "fido-no-separate-proteins", 		   
      "Proteins graph will not be separated in sub-graphs (Only valid if option -A is active).",
      "",
      TRUE_IF_SET); 
  cmd.defineOption("C",
      "fido-no-prune-proteins", 		   
      "it does not prune peptides with a very low score (~0.0) which means that if a peptide with a very low score is matching two proteins,\
       when we prune the peptide,it will be duplicated to generate two new protein groups (Only valid if option -A is active).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("d",
      "fido-gridsearch-depth",
      "Setting depth 0 or 1 or 2 from low depth to high depth(less computational time) \
       of the grid search for the estimation Alpha,Beta and Gamma parameters for fido(Only valid if option -A is active). Default value is 0",
      "value");
  cmd.defineOption("P",
      "pattern",
      "Define the text pattern to identify the decoy proteins and/or PSMs, set this up if the label that idenfifies the decoys in the database \
       is not the default (by default : random) (Only valid if option -A  is active).",
      "value");
  cmd.defineOption("T",
      "fido-reduce-tree-in-gridsearch",
      "Reduce the tree of proteins (removing low scored proteins) in order to estimate alpha,beta and gamma faster.(Only valid if option -A is active).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("Y",
      "post-processing-tdcn",
      "Use target decoy competition to compute peptide probabilities.(recommended when using -A).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("H",
      "grid-search-mse-threshold",
      "Q-value threshold that will be used in the computation of the MSE and ROC AUC score in the grid search (recommended 0.05 for normal size datasets and 0.1 for big size datasets).(Only valid if option -A is active).",
      "",
      "value");
  cmd.defineOption("W",
      "fido-truncation",
      "Proteins with a very low score (< 0.001) will be truncated (assigned 0.0 probability).(Only valid if option -A is active).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("Q",
      "fido-protein-group-level-inference",
      "Uses protein group level inference, each cluster of proteins is either present or not, therefore when grouping proteins discard all possible combinations for each group.(Only valid if option -A is active and -N is inactive).",
      "",
      TRUE_IF_SET);
  cmd.defineOption("l",
      "results-proteins",
      "Output tab delimited results of proteins to a file instead of stdout (Only valid if option -A is active)",
      "filename");
  cmd.defineOption("L",
      "decoy-results-proteins",
      "Output tab delimited results for decoy proteins into a file (Only valid if option -A is active)",
      "filename");
  
  // finally parse and handle return codes (display help etc...)
  cmd.parseArgs(argc, argv);
  // now query the parsing results
  if (cmd.optionSet("X")) xmlInterface.setXmlOutputFN(cmd.options["X"]);
  
  // filenames for outputting results to file
  if (cmd.optionSet("m"))  psmResultFN = cmd.options["m"];
  if (cmd.optionSet("M"))  decoyPsmResultFN = cmd.options["M"];
  
  if (cmd.optionSet("U")) {
    if (cmd.optionSet("A")){
      cerr
      << "ERROR: The -U option cannot be used in conjunction with -A: peptide level statistics\n"
      << "are needed to calculate protein level ones.";
      return 0;
    }
    if (cmd.optionSet("r")) {
      cerr
      << "WARNING: The -r option cannot be used in conjunction with -U: no peptide level statistics\n"
      << "are calculated, ignoring -r option." << endl;
    }
    if (cmd.optionSet("B")) {
      cerr
      << "WARNING: The -B option cannot be used in conjunction with -U: no peptide level statistics\n"
      << "are calculated, ignoring -B option." << endl;
    }
    reportUniquePeptides = false;
  } else {
    if (cmd.optionSet("r"))  peptideResultFN = cmd.options["r"];
    if (cmd.optionSet("B"))  decoyPeptideResultFN = cmd.options["B"];
  }

  if (cmd.optionSet("A")) {
  
    ProteinProbEstimator::setCalcProteinLevelProb(true);
    
    /*fido parameters*/
    double fido_alpha = -1;
    double fido_beta = -1;
    double fido_gamma = -1;
    bool fido_nogroupProteins = false; 
    bool fido_trivialGrouping = false;
    bool fido_noprune = false;
    bool fido_noseparate = false;
    bool fido_reduceTree = false;
    bool fido_truncate = false;
    unsigned fido_depth = 0;
    double fido_mse_threshold = 0.1;
    /* general protein probabilities options */
    bool tiesAsOneProtein = false;
    bool usePi0 = false;
    bool outputEmpirQVal = false;
    std::string decoy_prefix = "random";
    
    tiesAsOneProtein = cmd.optionSet("g");
    usePi0 = cmd.optionSet("I");
    outputEmpirQVal = cmd.optionSet("q");
    fido_nogroupProteins = cmd.optionSet("N"); 
    fido_noprune = cmd.optionSet("C");
    fido_noseparate = cmd.optionSet("E");
    fido_reduceTree = cmd.optionSet("T");
    fido_truncate = cmd.optionSet("W");
    fido_trivialGrouping = cmd.optionSet("Q");
    if (cmd.optionSet("P"))  decoy_prefix = cmd.options["P"];
    if (cmd.optionSet("d"))  fido_depth = cmd.getInt("d", 0, 2);
    if (cmd.optionSet("a"))  fido_alpha = cmd.getDouble("a", 0.00, 1.0);
    if (cmd.optionSet("b"))  fido_beta = cmd.getDouble("b", 0.00, 1.0);
    if (cmd.optionSet("G"))  fido_gamma = cmd.getDouble("G", 0.00, 1.0);
    if (cmd.optionSet("H"))  fido_mse_threshold = cmd.getDouble("H",0.001,1.0);
    if (cmd.optionSet("l"))  proteinResultFN = cmd.options["l"];
    if (cmd.optionSet("L"))  decoyProteinResultFN = cmd.options["L"];
    
    protEstimator = new FidoInterface(fido_alpha,fido_beta,fido_gamma,fido_nogroupProteins,fido_noseparate,
				      fido_noprune,fido_depth,fido_reduceTree,fido_truncate,fido_mse_threshold,
				      tiesAsOneProtein,usePi0,outputEmpirQVal,decoy_prefix,fido_trivialGrouping);
  }
  
  if (cmd.optionSet("e")) {

    readStdIn = true;
    string str = "";
    try
    {
      boost::filesystem::path ph = boost::filesystem::unique_path();
      boost::filesystem::path dir = boost::filesystem::temp_directory_path() / ph;
      boost::filesystem::path file("pin-tmp.xml");
      xmlInterface.setXmlInputFN(std::string((dir / file).string())); 
      str =  dir.string();
      xmlInputDir = new char[str.size() + 1];
      std::copy(str.begin(), str.end(), xmlInputDir);
      xmlInputDir[str.size()] = '\0';
      if (boost::filesystem::is_directory(dir)) {
	      boost::filesystem::remove_all(dir);
      }
      boost::filesystem::create_directory(dir);
    } 
    catch (boost::filesystem::filesystem_error &e)
    {
      std::cerr << e.what() << std::endl;
      return 0;
    }
  }
  
  if (cmd.optionSet("p")) {
    crossValidation.setSelectedCpos(cmd.getDouble("p", 0.0, 1e127));
  }
  if (cmd.optionSet("n")) {
    crossValidation.setSelectedCneg(cmd.getDouble("n", 0.0, 1e127));
    if(crossValidation.getSelectedCpos() == 0)
    {
      std::cerr << "Warning : the positive penalty(cpos) is 0, therefore both the "  
		 << "positive and negative penalties are going "
		 << "to be cross-validated. The option --Cneg has to be used together "
		 << "with the option --Cpos" << std::endl;
    }
  }
  if (cmd.optionSet("J")) {
    tabFN = cmd.options["J"];
  }
  if (cmd.optionSet("j")) {
    tabInput = true;
    forwardTabInputFN = cmd.options["j"];
  }
  if (cmd.optionSet("k")) {
    tabInput = false;
    xmlInterface.setXmlInputFN(cmd.options["k"]);
  }
  if (cmd.optionSet("w")) {
    weightFN = cmd.options["w"];
  }
  if (cmd.optionSet("W")) {
    SanityCheck::setInitWeightFN(cmd.options["W"]);
  }
  if (cmd.optionSet("V")) {
    SanityCheck::setInitDefaultDirName(cmd.options["V"]);
  }
  if (cmd.optionSet("f")) {
    double frac = cmd.getDouble("f", 0.0, 1.0);
    trainRatio = frac;
  }
  if (cmd.optionSet("u")) {
    Normalizer::setType(Normalizer::UNI);
  }
  if (cmd.optionSet("O")) {
    SanityCheck::setOverrule(true);
  }
  if (cmd.optionSet("R")) {
    crossValidation.setReportPerformanceEachIteration(true);
  }
  if (cmd.optionSet("x")) {
    crossValidation.setQuickValidation(true);
  }
  if (cmd.optionSet("v")) {
    Globals::getInstance()->setVerbose(cmd.getInt("v", 0, 10));
  }
  if (cmd.optionSet("F")) {
    crossValidation.setSelectionFdr(cmd.getDouble("F", 0.0, 1.0));
  }
  if (cmd.optionSet("t")) {
    test_fdr = cmd.getDouble("t", 0.0, 1.0);
    crossValidation.setTestFdr(test_fdr);
  }
  if (cmd.optionSet("i")) {
    crossValidation.setNiter(cmd.getInt("i", 0, 1000));
  }
  if (cmd.optionSet("S")) {
    Scores::setSeed(cmd.getInt("S", 1, 20000));
  }
  if (cmd.optionSet("K")) {
    DescriptionOfCorrect::setKlammer(true);
  }
  if (cmd.optionSet("D")) {
    DataSet::setCalcDoc(true);
    DescriptionOfCorrect::setDocType(cmd.getInt("D", 0, 15));
  }
  if (cmd.optionSet("Z")) {
    Scores::setOutXmlDecoys(true);
  }
  if (cmd.optionSet("s")) {
    xmlInterface.setSchemaValidation(false);
  }
  Scores::setShowExpMass(true);
  if (cmd.optionSet("Y")) {
    target_decoy_competition = true; 
  }
  // if there are no arguments left...
  if (cmd.arguments.size() == 0) {
    if(!cmd.optionSet("j") && !cmd.optionSet("k") && !cmd.optionSet("e") ){ // unless the input comes from -j, -k or -e option
      cerr << "Error: too few arguments.";
      cerr << "\nInvoke with -h option for help\n";
      return 0; // ...error
    }
  }
  // if there is one argument left...
  if (cmd.arguments.size() == 1) {
    tabInput = true;
    forwardTabInputFN = cmd.arguments[0]; // then it's the pin input
    if(cmd.optionSet("k")){ // and if the tab input is also present
      cerr << "Error: use one of either pin-xml or tab-delimited input format.";
      cerr << "\nInvoke with -h option for help.\n";
      return 0; // ...error
    }
    if(cmd.optionSet("e")){ // if stdin pin file is present
      cerr << "Error: the pin file has already been given as stdinput argument.";
      cerr << "\nInvoke with -h option for help.\n";
      return 0; // ...error
    }
  }
  // if there is more then one argument left...
  if (cmd.arguments.size() > 1) {
    cerr << "Error: too many arguments.";
    cerr << "\nInvoke with -h option for help\n";
    return 0; // ...error
  }

  return true;
}

/**
 * Reads in the files from XML (must be enabled at compile time) or tab format
 */
int Caller::readFiles() { 
  int error = 0;
  if (xmlInterface.getXmlInputFN().size() != 0) {    
    error = xmlInterface.readPin(setHandler, pCheck, protEstimator);
  } else if (tabInput) {
    error = setHandler.readTab(forwardTabInputFN, pCheck);
  }
  return error;
}


/** 
 * Fills in the features previously read from file and normalizes them
 */
void Caller::fillFeatureSets() {
  fullset.fillFeatures(setHandler, reportUniquePeptides);
  if (VERB > 1) {
    cerr << "Train/test set contains " << fullset.posSize()
        << " positives and " << fullset.negSize()
        << " negatives, size ratio=" << fullset.getTargetDecoySizeRatio()
        << " and pi0=" << fullset.getPi0() << endl;
  }
  
  //check for the minimum recommended number of positive and negative hits
  if(fullset.posSize() <= (unsigned)(FeatureNames::getNumFeatures() * 5)) {
    std::cerr << "Warning : the number of positive samples read is too small to perform a correct classification.\n" << std::endl;
  }
  if(fullset.negSize() <= (unsigned)(FeatureNames::getNumFeatures() * 5)) {
    std::cerr << "Warning : the number of negative samples read is too small to perform a correct classification.\n" << std::endl;
  }
  
  
  if (DataSet::getCalcDoc()) {
    BOOST_FOREACH (DataSet * subset, setHandler.getSubsets()) {
      subset->setRetentionTime(scan2rt);
    }
  }
  if (tabFN.length() > 0) {
    setHandler.writeTab(tabFN, pCheck);
  }
  
  //Normalize features
  vector<double*> featuresV, rtFeaturesV;
  BOOST_FOREACH (DataSet * subset, setHandler.getSubsets()) {
    subset->fillFeatures(featuresV);
    subset->fillRtFeatures(rtFeaturesV);
  }
  pNorm = Normalizer::getNormalizer();

  pNorm->setSet(featuresV,
      rtFeaturesV,
      FeatureNames::getNumFeatures(),
      DataSet::getCalcDoc() ? RTModel::totalNumRTFeatures() : 0);
  pNorm->normalizeSet(featuresV, rtFeaturesV);
}


/** Calculates the PSM and/or peptide probabilities
 * @param isUniquePeptideRun boolean indicating if we want peptide or PSM probabilities
 * @param procStart clock time when process started
 * @param procStartClock clock associated with procStart
 * @param w list of normal vectors
 * @param diff runtime of the calculations
 * @param targetDecoyCompetition boolean for target decoy competition
 */
void Caller::calculatePSMProb(bool isUniquePeptideRun,Scores *fullset, time_t& procStart,
    clock_t& procStartClock, double& diff, bool targetDecoyCompetition){
  // write output (cerr or xml) if this is the unique peptide run and the
  // reportUniquePeptides option was switched on OR if this is not the unique
  // peptide run and the option was switched off
  bool writeOutput = (isUniquePeptideRun == reportUniquePeptides);
  
  if (reportUniquePeptides && VERB > 0 && writeOutput) {
    cerr << "Tossing out \"redundant\" PSMs keeping only the best scoring PSM "
        "for each unique peptide." << endl;
  }
  
  if (isUniquePeptideRun) {
    fullset->weedOutRedundant();
  } else if (targetDecoyCompetition) {
    fullset->weedOutRedundantTDC();
    if(VERB > 0) {
      std::cerr << "Target Decoy Competition yielded " << fullset->posSize() 
        << " target PSMs and " << fullset->negSize() << " decoy PSMs" << std::endl;
    }
  }
  
  if (VERB > 0 && writeOutput) {
    std::cerr << "Selecting pi_0=" << fullset->getPi0() << endl;
  }
  if (VERB > 0 && writeOutput) {
    cerr << "Calibrating statistics - calculating q values" << endl;
  }
  int foundPSMs = fullset->calcQ(test_fdr);
  fullset->calcPep();
  if (VERB > 0 && DataSet::getCalcDoc() && writeOutput) {
    crossValidation.printDOC();
  }
  if (VERB > 0 && writeOutput) {
    cerr << "New pi_0 estimate on merged list gives " << foundPSMs
        << (reportUniquePeptides ? " peptides" : " PSMs") << " over q="
        << test_fdr << endl;
  }
  if (VERB > 0 && writeOutput) {
    cerr
    << "Calibrating statistics - calculating Posterior error probabilities (PEPs)"
    << endl;
  }
  time_t end;
  time(&end);
  diff = difftime(end, procStart);
  ostringstream timerValues;
  timerValues.precision(4);
  timerValues << "Processing took " << ((double)(clock() - procStartClock)) / (double)CLOCKS_PER_SEC
              << " cpu seconds or " << diff << " seconds wall time" << endl;
  if (VERB > 1 && writeOutput) {
    cerr << timerValues.str();
  }
  if (weightFN.size() > 0) {
    ofstream weightStream(weightFN.data(), ios::out);
    crossValidation.printAllWeights(weightStream, pNorm);
    weightStream.close();
  }
  if (isUniquePeptideRun) {
    if (peptideResultFN.empty()) {
      setHandler.print(*fullset, NORMAL);
    } else {
      ofstream targetStream(peptideResultFN.data(), ios::out);
      setHandler.print(*fullset, NORMAL, targetStream);
      targetStream.close();
    }
    if (!decoyPeptideResultFN.empty()) {
      ofstream decoyStream(decoyPeptideResultFN.data(), ios::out);
      setHandler.print(*fullset, SHUFFLED, decoyStream);
      decoyStream.close();
    }
    // set pi_0 value (to be outputted)
    xmlInterface.setPi0Peptides(fullset->getPi0());
  } else {
    if (psmResultFN.empty() && writeOutput) {
      setHandler.print(*fullset, NORMAL);
    } else if (!psmResultFN.empty()) {
      ofstream targetStream(psmResultFN.data(), ios::out);
      setHandler.print(*fullset, NORMAL, targetStream);
      targetStream.close();
    }
    if (!decoyPsmResultFN.empty()) {
      ofstream decoyStream(decoyPsmResultFN.data(), ios::out);
      setHandler.print(*fullset, SHUFFLED, decoyStream);
      decoyStream.close();
    }
    // set pi_0 value (to be outputted)
    xmlInterface.setPi0Psms(fullset->getPi0());
    xmlInterface.setNumberQpsms(fullset->getQvaluesBelowLevel(0.01));
  }
}

/** Calculates the protein probabilites by calling Fido and directly writes the results to XML
 */
void Caller::calculateProteinProbabilitiesFido() {
  time_t startTime;
  clock_t startClock;
  time(&startTime);
  startClock = clock();  

  if (VERB > 0) {
    cerr << "\nCalculating protein level probabilities with Fido\n";
    cerr << protEstimator->printCopyright();
  }
  
  protEstimator->initialize(&fullset);
  protEstimator->run();
  protEstimator->computeProbabilities();
  protEstimator->computeStatistics();
  
  time_t procStart;
  clock_t procStartClock = clock();
  time(&procStart);
  double diff_time = difftime(procStart, startTime);
  
  if (VERB > 1) {  
    cerr << "Estimating Protein Probabilities took : "
    << ((double)(procStartClock - startClock)) / (double)CLOCKS_PER_SEC
    << " cpu seconds or " << diff_time << " seconds wall time" << endl;
  }
  
  protEstimator->printOut(proteinResultFN, decoyProteinResultFN);
  if (xmlInterface.getXmlOutputFN().size() > 0) {
      xmlInterface.writeXML_Proteins(protEstimator);
  }
}

/** 
 * Executes the flow of the percolator process:
 * 1. reads in the input file
 * 2. trains the SVM
 * 3. calculate PSM probabilities
 * 4. (optional) calculate peptide probabilities
 * 5. (optional) calculate protein probabilities
 */
int Caller::run() {  

  time(&startTime);
  startClock = clock();
  if (VERB > 0) {
    cerr << extendedGreeter();
  }
  // populate tmp input file with cin information if option is enabled
  if(readStdIn){
    ofstream tmpInputFile;
    tmpInputFile.open(xmlInterface.getXmlInputFN().c_str());
    while(cin) {
      char buffer[1000];
      cin.getline(buffer, 1000);
      tmpInputFile << buffer << endl;
    }
    tmpInputFile.close();
  }
  
  // Reading input files (pin or temporary file)
  if(!readFiles()) {
    throw MyException("ERROR: Failed to read in file, check if the correct file-format was used.");
  }
  // Copy feature data to Scores object
  fillFeatureSets();
  
  // delete temporary file if reading from stdin
  if(readStdIn) {
    remove(xmlInterface.getXmlInputFN().c_str());
  }
  if(VERB > 2){
    std::cerr << "FeatureNames::getNumFeatures(): "<< FeatureNames::getNumFeatures() << endl;
  }
  int firstNumberOfPositives = crossValidation.preIterationSetup(fullset, pCheck, pNorm);
  if (VERB > 0) {
    cerr << "Estimating " << firstNumberOfPositives << " over q="
        << test_fdr << " in initial direction" << endl;
  }
  
  time_t procStart;
  clock_t procStartClock = clock();
  time(&procStart);
  double diff = difftime(procStart, startTime);
  if (VERB > 1) cerr << "Reading in data and feature calculation took "
      << ((double)(procStartClock - startClock)) / (double)CLOCKS_PER_SEC
      << " cpu seconds or " << diff << " seconds wall time" << endl;
  
  // Do the SVM training
  crossValidation.train(pNorm);
  crossValidation.postIterationProcessing(fullset, pCheck);
  // calculate psms level probabilities
  
  //PSM probabilities TDA or TDC
  calculatePSMProb(false, &fullset, procStart, procStartClock, diff, target_decoy_competition);
  if (xmlInterface.getXmlOutputFN().size() > 0){
    xmlInterface.writeXML_PSMs(fullset);
  }
  
  // calculate unique peptides level probabilities WOTE
  if(reportUniquePeptides){
    calculatePSMProb(true, &fullset, procStart, procStartClock, diff, target_decoy_competition);
    if (xmlInterface.getXmlOutputFN().size() > 0){
      xmlInterface.writeXML_Peptides(fullset);
    }
  }
  // calculate protein level probabilities with FIDO
  if(ProteinProbEstimator::getCalcProteinLevelProb()){
    calculateProteinProbabilitiesFido();
  }
  // write output to file
  xmlInterface.writeXML(fullset, protEstimator, call);  
  return 1;
}
