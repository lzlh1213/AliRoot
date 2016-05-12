#include "AliLumiTools.h"
#include "AliCDBManager.h"
#include "AliCDBPath.h"
#include "AliCDBEntry.h"
#include "AliTriggerConfiguration.h"
#include "AliTriggerClass.h"
#include "AliTriggerBCMask.h"
#include "AliTriggerRunScalers.h"
#include "AliTriggerScalersRecord.h"
#include "AliTriggerScalers.h"
#include "AliTriggerCluster.h"
#include "AliLog.h"
#include "AliLHCData.h"
#include "AliLHCDipValT.h"
#include <TGraphErrors.h>
#include <TAxis.h>
#include <TMath.h>

//___________________________________________________________________
TGraph* AliLumiTools::GetLumiGraph(Int_t tp, Int_t run, const char * ocdbPathDef)
{
  // get lumi graph of requested type, relying on preconfigured CDB
  TGraph* gr = 0;
  switch(tp) {
  case kLumiCTP: gr = GetLumiFromCTP(run,ocdbPathDef); break;
  case kLumiDIP: gr = GetLumiFromDIP(run,ocdbPathDef); break;
  default: AliFatalClassF("Unknown luminosity type %d",tp);
  };
  return gr;
}

//___________________________________________________________________
TGraph* AliLumiTools::GetLumiFromDIP(Int_t run, const char * ocdbPathDef)
{
  //  Get TGraph with luminosity vs time using LHC DIP data stored in the GRP/GRP/LHCData object
  //
  const Int_t kMinDelta=30; // use minimum kMinDelta  seconds difference
  AliCDBManager* man = AliCDBManager::Instance();
  if (!man->IsDefaultStorageSet()) {
    man->SetDefaultStorage(ocdbPathDef);
    if (run>=0) man->SetRun(run);
    else {
      AliErrorClass("OCDB cannot be configured since run number is not provided"); return 0;
    }
  }
  if (run<0) run = man->GetRun();
  //
  // use explicit run number since we may query for run other than in CDB cache
  AliLHCData* lhcData = (AliLHCData*)(man->Get(AliCDBPath("GRP/GRP/LHCData"),run)->GetObject());

  Int_t nRec = lhcData->GetNLumiAliceSBDelivered();
  Double_t vecIntLuminosity[nRec];
  Double_t vecRate[nRec];
  Double_t vecTime[nRec];
  Double_t vecRateT[nRec];

  int nRecAcc = 0;

  for (int iRec=0;iRec<nRec;iRec++) {
    AliLHCDipValF *value=lhcData->GetLumiAliceSBDelivered(iRec);
    if (TMath::Abs(value->GetValue())<1e-9) {
      AliWarningClassF("Skipping empty record %d : ",iRec);
      value->Print();
      continue;
    }
    vecIntLuminosity[nRecAcc]=value->GetValue();
    vecTime[nRecAcc]=value->GetTimeStamp();
    nRecAcc++;
  }
  //
  int nRateAcc = 0;
  Long64_t tref = vecTime[0];
  Long64_t t0 = Long64_t(vecTime[0])-tref;
  double rate0 = vecIntLuminosity[0];
  for (int iRec=1;iRec<nRecAcc;iRec++) {
    Long64_t t1 = Long64_t(vecTime[iRec])-tref;
    Long64_t dt = t1-t0;
    if (dt<kMinDelta) {
      AliWarningClassF("Time interval too small: %f from %lld %lld",double(dt),t1,t0);
      continue;
    }
    double rate1 = vecIntLuminosity[iRec];
    double t = tref + t0 + dt/2;
    if (dt&0x1) t += 0.5;
    vecRateT[nRateAcc] = t;
    vecRate[nRateAcc] = (rate1-rate0)/dt*1e6; // convert from Hz/b to Hz/ub
    //    printf("%lld %lld -> %lld %lld %e\n",t0,t1, tref + t0 + dt/2,dt,rate1);
    t0 = t1;
    rate0 = rate1;
    nRateAcc++;
  }
  TGraph* grLumi=new TGraph(nRateAcc,vecRateT, vecRate);
  grLumi->SetTitle(Form("Rate estimator Run %d",run));
  grLumi->GetXaxis()->SetTitle("time");
  grLumi->GetXaxis()->SetTimeDisplay(1);
  grLumi->GetYaxis()->SetTitle("Inst Lumi (Hz/ub)");
  grLumi->SetMarkerStyle(25);
  grLumi->SetMarkerSize(0.4);
  grLumi->SetUniqueID(run);
  return grLumi;
}

//___________________________________________________________________
TGraph* AliLumiTools::GetLumiFromCTP(Int_t run, const char * ocdbPathDef, TString refClassName, Double_t refSigma) 
{
  /*
    Get TGraph with luminosity vs time using reference trigger from the CTP scalers
    If ref.trigger or ref. x-section is not provided, it is taken from the lookup table.
    Source code adapted from from E.Kryshen's
    //
    Example:
    int run=244918;
    const char* ocdbPath="local:///cvmfs/alice.cern.ch/calibration/data/2015/OCDB/";
    TString refClassName="C0V0M-B-NOPF-CENTNOTRD";
    Double_t refSigma=4.6;

   */
  //
  AliCDBManager* man = AliCDBManager::Instance();
  if (!man->IsDefaultStorageSet()) {
    man->SetDefaultStorage(ocdbPathDef);
    if (run>=0) man->SetRun(run);
    else {
      AliErrorClass("OCDB cannot be configured since run number is not provided"); return 0;
    }
  }
  if (run<0) run = man->GetRun();
  //
  // use explicit run number since we may query for run other than in CDB cache  
  // Get trigger config 
  AliTriggerConfiguration* cfg = (AliTriggerConfiguration*) man->Get(AliCDBPath("GRP/CTP/Config"),run)->GetObject();
  //
  TString refClassAuto="";
  double refSigmaAuto=-1;
  if ((refClassName.IsNull() || refSigma<0)) {
    if (!GetLumiCTPRefClass(run,refClassAuto,refSigmaAuto)) exit(1);
    if (refClassName.IsNull()) refClassName = refClassAuto;
    if (refSigma<0) refSigma = refSigmaAuto;
  }
  AliInfoClassF("Getting CTP lumi for run:%d | using refClass: %s, refSigma: %e",run,refClassName.Data(),refSigma);
  //  
  TObjArray classes = cfg->GetClasses();
  AliTriggerClass* cl = (AliTriggerClass*) classes.FindObject(refClassName.Data());
  if (!cl) {
    AliErrorClassF("Did not find reference trigger %s",refClassName.Data());
    return 0;
  }
  AliTriggerBCMask* bcmask = (AliTriggerBCMask*) cl->GetBCMask();
  Int_t nBCs = bcmask->GetNUnmaskedBCs();

  TString activeDetectorsString = cfg->GetActiveDetectors();
  // use explicit run number since we may query for run other than in CDB cache  
  AliTriggerRunScalers* scalers = (AliTriggerRunScalers*) man->Get("GRP/CTP/Scalers",run)->GetObject();
  TString refCluster = cl->GetCluster()->GetName();
  Bool_t useLM = activeDetectorsString.Contains("TRD") && (refCluster.EqualTo("CENT") || refCluster.EqualTo("ALL") || refCluster.EqualTo("FAST"));

  Int_t nEntries = scalers->GetScalersRecords()->GetEntriesFast();
  Double_t orbitRate = 11245.;
  
  Double_t* vtime = new Double_t[nEntries];
  Double_t* vlumi = new Double_t[nEntries-1];
  Double_t* vlumiErr = new Double_t[nEntries-1];
  for (Int_t r=0;r<nEntries-1;r++){
    // Get scaler records
    AliTriggerScalersRecord* record1 = scalers->GetScalersRecord(r);
    AliTriggerScalersRecord* record2 = scalers->GetScalersRecord(r+1);
    Int_t classId = cfg->GetClassIndexFromName(refClassName.Data());
    const AliTriggerScalers* scaler1 = record1->GetTriggerScalersForClass(classId);
    const AliTriggerScalers* scaler2 = record2->GetTriggerScalersForClass(classId);
    UInt_t counts1 = useLM ? scaler1->GetLMCB() : scaler1->GetLOCB();
    UInt_t counts2 = useLM ? scaler2->GetLMCB() : scaler2->GetLOCB();
    UInt_t refCounts = (counts2>counts1) ?counts2-counts1 :  counts2+(0xffffffff-counts1)+1;
    Double_t t1 = record1->GetTimeStamp()->GetSeconds()+1e-6*record1->GetTimeStamp()->GetMicroSecs();
    Double_t t2 = record2->GetTimeStamp()->GetSeconds()+1e-6*record2->GetTimeStamp()->GetMicroSecs();
    Double_t duration = t2-t1;
    Double_t totalBCs = duration*orbitRate*nBCs;
    Double_t refMu = -TMath::Log(1-Double_t(refCounts)/totalBCs);
    Double_t refRate = refMu*orbitRate*nBCs;
    Double_t refLumi = refRate/refSigma;
    //printf("%f %f\n",t2,refLumi);
    if (r==0) vtime[0]=Double_t(t1);
    vtime[r+1]=Double_t(t2);
    vlumi[r]=refLumi; 
    vlumiErr[r]=refLumi/TMath::Sqrt(refCounts);
  }
  
  TGraphErrors* grLumi=new TGraphErrors(nEntries-1, vtime,vlumi,0,vlumiErr);
  grLumi->SetName(TString::Format("InstLuminosityEstimator%s",refClassName.Data()).Data());
  grLumi->SetTitle(TString::Format("Inst. luminosity. Run=%d Estimator: %s",run, refClassName.Data()).Data());
  grLumi->GetYaxis()->SetTitle("Inst lumi (Hz/b)");
  grLumi->GetXaxis()->SetTitle("time");
  grLumi->GetXaxis()->SetTimeDisplay(1);
  grLumi->SetMarkerStyle(25);
  grLumi->SetMarkerSize(0.4);
  //
  grLumi->SetUniqueID(run);
  return grLumi;
}


//___________________________________
Bool_t AliLumiTools::GetLumiCTPRefClass(int run, TString& refClass, double &refSigma)
{
  // get lumi ref class and sigma for given run
  // at the moment use lookup table, in the future will query OCDB
  refClass = "";
  refSigma = -1;
  if      (               run<=118501) { refSigma=  62.;  refClass = "CINT1B-ABCE-NOPF-ALL";   } // pp_7.00: 62mb=54.3mb*1.15=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=118502 && run<=118561) { refSigma=  47.;  refClass = "CINT1B-ABCE-NOPF-ALL";   } // pp_0.90: 47mb=52 mb *0.91=sigma(INEL)*R(INT1/INEL) (arxiv: 1208.4968, fig.10 + table 3)
  else if (run>=118903 && run<=120829) { refSigma=  62.;  refClass = "CINT1B-ABCE-NOPF-ALL";   } // pp_7.00: 62mb=54.3mb*1.15=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=121039 && run<=121040) { refSigma=  47.;  refClass = "CINT1B-ABCE-NOPF-ALL";   } // pp_0.90: 47mb=52 mb *0.91=sigma(INEL)*R(INT1/INEL) (arxiv: 1208.4968, fig.10 + table 3)
  else if (run>=121041 && run<=126437) { refSigma=  62.;  refClass = "CINT1B-ABCE-NOPF-ALL";   } // pp_7.00: 62mb=54.3mb*1.15=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=126438 && run<=127718) { refSigma=  62.;  refClass = "CINT1-B-NOPF-ALLNOTRD";  } // pp_7.00: 62mb=54.3mb*1.15=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=127719 && run<=127730) { refSigma=  62.;  refClass = "CINT1B-ABCE-NOPF-ALL";   } // pp_7.00: 62mb=54.3mb*1.15=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=127731 && run<=136848) { refSigma=  62.;  refClass = "CINT1-B-NOPF-ALLNOTRD";  } // pp_7.00: 62mb=54.3mb*1.15=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=136849 && run<=139316) { refSigma=5970.;  refClass = "C0SMH-B-NOPF-ALL";       } // PbPb_2.76: (Oyama,2011-05-20,RunCond), sigma_hardronic = 7.64 b
  else if (run>=139328 && run<=139517) { refSigma=5970.;  refClass = "C0SMH-B-NOPF-ALLNOTRD";  } // PbPb_2.76: (Oyama,2011-05-20,RunCond), sigma_hardronic = 7.64 b
  else if (run>=145289 && run<=146860) { refSigma=  57.;  refClass = "CINT1-B-NOPF-ALLNOTRD";  } // pp_2.76: 57mb=47.7mb*1.20=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=146808 && run<=146814) { refSigma=  57.;  refClass = "CINT1-B-NOPF-ALL";       } // pp_2.76: 57mb=47.7mb*1.20=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=145815 && run<=146856) { refSigma=  57.;  refClass = "CINT1-B-NOPF-ALLNOTRD";  } // pp_2.76: 57mb=47.7mb*1.20=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=146857 && run<=146857) { refSigma=  57.;  refClass = "CINT1-B-NOPF-ALL";       } // pp_2.76: 57mb=47.7mb*1.20=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=146858 && run<=146860) { refSigma=  57.;  refClass = "CINT1-B-NOPF-ALLNOTRD";  } // pp_2.76: 57mb=47.7mb*1.20=sigma(VBAND)*R(INT1/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=148370 && run<=157078) { refSigma=  54.;  refClass = "CVBAND-B-NOPF-ALLNOTRD"; } // pp_7.00: 54.3mb (Martino,2012-03-12,RunCond)
  else if (run>=157079 && run<=165746) { refSigma=  24.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_7.00: 24mb=54.3mb*0.44=sigma(VBAND)*R(0TVX/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=166477 && run<=170593) { refSigma=4100.;  refClass = "CVLN-B-NOPF-ALLNOTRD";   } // PbPb_2.76: (Martino,2013-03-15,RunCond)
  else if (run>=176658 && run<=177143) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177146 && run<=177147) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177148 && run<=177149) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177150 && run<=177165) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177166 && run<=177166) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177167 && run<=177167) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177168 && run<=177168) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177169 && run<=177172) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177173 && run<=177173) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177174 && run<=177506) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=177507 && run<=178017) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=178018 && run<=178029) { refSigma=  67.;  refClass = "CINT1-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), CINT1/C0TVX=2.7 from 178052
  else if (run>=178030 && run<=178053) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=178055 && run<=178062) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALL";       } // pp_8.00: (Artem, 2013-10-04,RunCond), vdM
  else if (run>=178062 && run<=178220) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=179444 && run<=180715) { refSigma=  25.;  refClass = "C0TVX-S-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=180716 && run<=180720) { refSigma=  56.;  refClass = "CINT7-S-NOPF-ALLNOTRD";  } // no C0TVX in these runs, taking VBAND cross section
  else if (run>=180721 && run<=184844) { refSigma=  25.;  refClass = "C0TVX-S-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=184845 && run<=184990) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=184991 && run<=188229) { refSigma=  25.;  refClass = "C0TVX-S-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=188230 && run<=188366) { refSigma=1590.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pPb_5.02: pilot. arxiv:1405.1849
  else if (run>=188367 && run<=193692) { refSigma=  25.;  refClass = "C0TVX-S-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=193693 && run<=193766) { refSigma=  25.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_8.00: (Artem, 2013-10-04,RunCond), TOTEM INEL = 74.7+/-1.7 mb
  else if (run>=195344 && run<=197388) { refSigma=1590.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pPb_5.02: arxiv:1405.1849
  else if (run>=197470 && run<=197692) { refSigma=  18.;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // pp_2.76: 18mb=47.7mb*0.39=sigma(VBAND)*R(0TVX/VBAND) (Martino,2012-03-12,RunCond)
  else if (run>=221835 && run<=223669) { refSigma= 16.8;  refClass = "CADAND-B-NOPF-ALLNOTRD"; } // estimates from Martino
  else if (run>=221670 && run<=223983) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // estimates from Martino and MC
  else if (run>=223984 && run<=223984) { refSigma= 50.0;  refClass = "CADAND-B-NOPF-ALLNOTRD"; } // estimates from Martino and MC
  else if (run>=223985 && run<=226110) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // estimates from Martino and MC
  else if (run>=226111 && run<=226115) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=226116 && run<=228909) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // estimates from Martino and MC
  else if (run>=228910 && run<=229376) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=229386 && run<=229398) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-MUON";      } // estimates from Martino and MC
  else if (run>=229409 && run<=229410) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=229416 && run<=229893) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-MUON";      } // estimates from Martino and MC
  else if (run>=229894 && run<=229899) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // estimates from Martino and MC
  else if (run>=229942 && run<=231321) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-MUON";      } // estimates from Martino and MC
  else if (run>=232914 && run<=233858) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENT";      } // estimates from Martino and MC
  else if (run>=233910 && run<=234050) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-ALLNOTRD";  } // estimates from Martino and MC
  else if (run>=234051 && run<=238669) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENT";      } // estimates from Martino and MC
  else if (run>=238670 && run<=240150) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=240151 && run<=240151) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-MUON";      } // estimates from Martino and MC
  else if (run>=240152 && run<=243373) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=243374 && run<=243398) { refSigma= 21.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=243399 && run<=243984) { refSigma=6700.;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=243985 && run<=244912) { refSigma= 21.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Martino and MC
  else if (run>=244913 && run<=246994) { refSigma=4600.;  refClass = "C0V0M-B-NOPF-CENTNOTRD"; } // estimates from Cvetan and Alberica
  else if (run>=246995               ) { refSigma= 30.0;  refClass = "C0TVX-B-NOPF-CENTNOTRD"; } // estimates from Cvetan and Alberica
  //
  if (refClass.IsNull() || refSigma<1) {
    AliErrorClassF("Did not find reference class for run %d",run);
    return kFALSE;
  }
  return kTRUE;
}