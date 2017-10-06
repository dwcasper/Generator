//____________________________________________________________________________
/*
 Copyright (c) 2003-2017, GENIE Neutrino MC Generator Collaboration
 For the full text of the license visit http://copyright.genie-mc.org
 or see $GENIE/LICENSE

 Author: Costas Andreopoulos <costas.andreopoulos \at stfc.ac.uk>
         University of Liverpool & STFC Rutherford Appleton Lab - March 09, 2006

 For the class documentation see the corresponding header file.

 Important revisions after version 2.0.0 :

*/
//____________________________________________________________________________

#include <TMath.h>

#include "Algorithm/AlgConfigPool.h"
#include "BaryonResonance/BaryonResUtils.h"
#include "Conventions/Constants.h"
#include "Interaction/SppChannel.h"
#include "Messenger/Messenger.h"
#include "PDG/PDGUtils.h"
#include "eresMAID/eresMAIDSPPXSec.h"
#include "Utils/MathUtils.h"
#include "Utils/KineUtils.h"
#include "Utils/Cache.h"
#include "Utils/CacheBranchFx.h"

using namespace genie;
using namespace genie::constants;

//____________________________________________________________________________
eresMAIDSPPXSec::eresMAIDSPPXSec() :
eresMAIDRESXSecWithCache("genie::eresMAIDSPPXSec")
{

}
//____________________________________________________________________________
eresMAIDSPPXSec::eresMAIDSPPXSec(string config) :
eresMAIDRESXSecWithCache("genie::eresMAIDSPPXSec", config)
{

}
//____________________________________________________________________________
eresMAIDSPPXSec::~eresMAIDSPPXSec()
{

}
//____________________________________________________________________________
double eresMAIDSPPXSec::Integrate(
          const XSecAlgorithmI * model, const Interaction * interaction) const
{
  if(! model->ValidProcess(interaction) ) return 0.;

  const KPhaseSpace & kps = interaction->PhaseSpace();
  if(!kps.IsAboveThreshold()) {
     LOG("COHXSec", pDEBUG)  << "*** Below energy threshold";
     return 0;
  }

  fSingleResXSecModel = model;

  //-- Get 1pi exclusive channel
  SppChannel_t spp_channel = SppChannel::FromInteraction(interaction);

  //-- Get cache
  Cache * cache = Cache::Instance();

  const InitialState & init_state = interaction->InitState();
  const ProcessInfo &  proc_info  = interaction->ProcInfo();
  const Target &       target     = init_state.Tgt();

  InteractionType_t it = proc_info.InteractionTypeId();
  int nucleon_pdgc = target.HitNucPdg();
  int nu_pdgc      = init_state.ProbePdg();

  // Get neutrino energy in the struck nucleon rest frame
  double Ev = init_state.ProbeE(kRfHitNucRest);

  double xsec = 0;

  unsigned int nres = fResList.NResonances();
  for(unsigned int ires = 0; ires < nres; ires++) {

     //-- Get next resonance from the resonance list
     Resonance_t res = fResList.ResonanceId(ires);

     //-- Build a unique name for the cache branch
     string key = this->CacheBranchName(res, it, nu_pdgc, nucleon_pdgc);
     LOG("eresMAIDSpp", pINFO) 
                            << "Finding cache branch with key: " << key;
     CacheBranchFx * cache_branch =
            dynamic_cast<CacheBranchFx *> (cache->FindCacheBranch(key));

     if(!cache_branch) {
       LOG("eresMAIDSpp", pWARN)  
         << "No cached RES v-production data for input neutrino"
         << " (pdgc: " << nu_pdgc << ")";
       LOG("eresMAIDSpp", pWARN)  
         << "Wait while computing/caching RES production xsec first...";

       this->CacheResExcitationXSec(interaction); 

       LOG("eresMAIDSpp", pINFO) << "Done caching resonance xsec data";
       LOG("eresMAIDSpp", pINFO) 
               << "Finding newly created cache branch with key: " << key;
       cache_branch =
              dynamic_cast<CacheBranchFx *> (cache->FindCacheBranch(key));
       assert(cache_branch);
     }
     const CacheBranchFx & cbranch = (*cache_branch);

     //-- Get cached resonance neutrinoproduction xsec
     //   (If E>Emax, assume xsec = xsec(Emax) - but do not evaluate the
     //    cross section spline at the end of its energy range-)
     double rxsec = (Ev<fEMax-1) ? cbranch(Ev) : cbranch(fEMax-1);

     //-- Get the BR for the (resonance) -> (exclusive final state)
     double br = SppChannel::BranchingRatio(spp_channel, res);

     //-- Get the Isospin Clebsch-Gordon coefficient for the given resonance
     //   and exclusive final state
     double igg = SppChannel::IsospinWeight(spp_channel, res);

     //-- Compute the weighted xsec
     //  (total weight = Breit-Wigner * BR * isospin Clebsch-Gordon)
     double res_xsec_contrib = rxsec*br*igg;

     SLOG("eresMAIDSpp", pINFO)
       << "Contrib. from [" << utils::res::AsString(res) << "] = "
       << "<Clebsch-Gordon = " << igg
       << "> * <BR(->1pi) = " << br
       << "> * <Breit-Wigner * d^2xsec/dQ^2dW = " << rxsec
       << "> = " << res_xsec_contrib;
   
     //-- Add contribution of this resonance to the cross section
     xsec += res_xsec_contrib;

  }//res

  SLOG("eresMAIDSpp", pNOTICE)  
         << "XSec[SPP/" << SppChannel::AsString(spp_channel)
                               << "/free] (Ev = " << Ev << " GeV) = " << xsec;

  //-- If requested return the free nucleon xsec even for input nuclear tgt
  if( interaction->TestBit(kIAssumeFreeNucleon) ) return xsec;

  //-- number of scattering centers in the target
  int NNucl = (pdg::IsProton(nucleon_pdgc)) ? target.Z() : target.N();

  xsec*=NNucl; // nuclear xsec 

  return xsec;
}
//____________________________________________________________________________
void eresMAIDSPPXSec::Configure(const Registry & config)
{
  Algorithm::Configure(config);
  this->LoadConfig();
}
//____________________________________________________________________________
void eresMAIDSPPXSec::Configure(string config)
{
  Algorithm::Configure(config);
  this->LoadConfig();
}
//____________________________________________________________________________
void eresMAIDSPPXSec::LoadConfig(void)
{
  AlgConfigPool * confp = AlgConfigPool::Instance();
  const Registry * gc = confp->GlobalParameterList();

  // Get GSL integration type & relative tolerance
  fGSLIntgType = fConfig->GetStringDef("gsl-integration-type",  "adaptive");
  fGSLRelTol   = fConfig->GetDoubleDef("gsl-relative-tolerance", 0.01);
  fGSLNCalls   = fConfig->GetIntDef("gsl-ncalls", 100000);
  fGSLThreshold= fConfig->GetDoubleDef("gsl-threshold", 50);
  fGSLNCallsFactor= fConfig->GetDoubleDef("gsl-ncalls-factor", 1);
  
  // get upper E limit on res xsec spline (=f(E)) before assuming xsec=const
  fEMax = fConfig->GetDoubleDef("ESplineMax", 100);
  fEMax = TMath::Max(fEMax,20.); // don't accept user Emax if less than 20 GeV

  // create the baryon resonance list specified in the config.
  fResList.Clear();
  string resonances = fConfig->GetStringDef(
                     "ResonanceNameList", gc->GetString("ResonanceNameList"));
  fResList.DecodeFromNameList(resonances);

  //-- Use algorithm within a DIS/RES join scheme. If yes get Wcut
  fUsingDisResJoin = fConfig->GetBoolDef(
                           "UseDRJoinScheme", gc->GetBool("UseDRJoinScheme"));
  fWcut = 999999;
  if(fUsingDisResJoin) {
    fWcut = fConfig->GetDoubleDef("Wcut",gc->GetDouble("Wcut"));
  }
}
//____________________________________________________________________________
