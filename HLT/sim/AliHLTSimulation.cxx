// $Id$

//**************************************************************************
//* This file is property of and copyright by the ALICE HLT Project        * 
//* ALICE Experiment at CERN, All rights reserved.                         *
//*                                                                        *
//* Primary Authors: Matthias Richter <Matthias.Richter@ift.uib.no>        *
//*                  for The ALICE HLT Project.                            *
//*                                                                        *
//* Permission to use, copy, modify and distribute this software and its   *
//* documentation strictly for non-commercial purposes is hereby granted   *
//* without fee, provided that the above copyright notice appears in all   *
//* copies and that both the copyright notice and this permission notice   *
//* appear in the supporting documentation. The authors make no claims     *
//* about the suitability of this software for any purpose. It is          *
//* provided "as is" without express or implied warranty.                  *
//**************************************************************************

/** @file   AliHLTSimulation.cxx
    @author Matthias Richter
    @date   
    @brief  Binding class for HLT simulation in AliRoot. */

#include <cassert>
#include <cerrno>
#include "TObjArray.h"
#include "TObjString.h"
#include "AliHLTSimulation.h"
#include "AliSimulation.h"
#include "AliLog.h"
#include "AliRun.h"
#include "AliRunLoader.h"
#include "AliHeader.h"
#include "AliCDBManager.h"
#include "AliCDBEntry.h"
#include "AliCDBPath.h"
#include "AliCDBId.h"
#include "AliCDBMetaData.h"
#include "AliCDBStorage.h"
#include "AliGRPObject.h"
#include "AliHLTSystem.h"
#include "AliHLTPluginBase.h"
#include "AliRawReaderFile.h"
#include "AliRawReaderDate.h"
#include "AliRawReaderRoot.h"
#include "AliESDEvent.h"
#include "AliHLTOUTComponent.h"
#include "AliMagF.h"
#include "TGeoGlobalMagField.h"
#include "TSystem.h"
#include "TMath.h"

#if ALIHLTSIMULATION_LIBRARY_VERSION != LIBHLTSIM_VERSION
#error library version in header file and lib*.pkg do not match
#endif

/** ROOT macro for the implementation of ROOT specific class methods */
ClassImp(AliHLTSimulation);

AliHLTSimulation::AliHLTSimulation()
  :
  fOptions(),
  fpPluginBase(new AliHLTPluginBase),
  fpRawReader(NULL)
{
  // see header file for class documentation
  // or
  // refer to README to build package
  // or
  // visit http://web.ift.uib.no/~kjeks/doc/alice-hlt
}

AliHLTSimulation::~AliHLTSimulation()
{
  // see header file for function documentation
  if (fpPluginBase) delete fpPluginBase;
  fpPluginBase=NULL;

  if (fpRawReader) {
    delete fpRawReader;
  }
  fpRawReader=NULL;
}

AliHLTSimulation* AliHLTSimulation::CreateInstance()
{
  // see header file for function documentation
  return new AliHLTSimulation;
}

int AliHLTSimulation::DeleteInstance(AliHLTSimulation* pSim)
{
  // see header file for function documentation
  assert(pSim!=NULL);
  delete pSim;
  return 0;
}

int AliHLTSimulation::Init(AliRunLoader* pRunLoader, const char* options)
{
  // init the simulation
  fOptions=options;
  TString sysOp;

  if(!fpPluginBase) {
    AliError("internal initialization failed");
    return -EINVAL;
  }

  AliHLTSystem* pSystem=fpPluginBase->GetInstance();
  if (!pSystem) {
    AliError("can not get AliHLTSystem instance");
    return -ENOMEM;
  }
  if (pSystem->CheckStatus(AliHLTSystem::kError)) {
    AliError("HLT system in error state");
    return -EFAULT;
  }

  // scan options for specific entries
  TObjArray* pTokens=fOptions.Tokenize(" ");
  if (pTokens) {
    int iEntries=pTokens->GetEntries();
    for (int i=0; i<iEntries; i++) {
      TString token=(((TObjString*)pTokens->At(i))->GetString());
      if (token.Contains("rawfile=")) {
	TString param=token.ReplaceAll("rawfile=", "");
	if (param.EndsWith("/")) {
	  AliInfo(Form("creating AliRawReaderFile (%s)", param.Data()));
	  fpRawReader = new AliRawReaderFile(param);
	} else if (param.EndsWith(".root")) {
	  AliInfo(Form("creating AliRawReaderRoot (%s)", param.Data()));
	  fpRawReader = new AliRawReaderRoot(param);
	} else if (!param.IsNull()) {
	  AliInfo(Form("creating AliRawReaderDate (%s)", param.Data()));
	  fpRawReader = new AliRawReaderDate(param);
	}
	if (fpRawReader) {
	    fpRawReader->RewindEvents();
	    int count=0;
	    for ( ; fpRawReader->NextEvent(); count++) {/* empty body */};
	    if (count!=pRunLoader->GetNumberOfEvents()) {
	      AliError(Form("mismatch in event count: runloader %d, rawreader %d; ignoring rawreader", 
			    pRunLoader->GetNumberOfEvents(), count));
	      count=0;
	    }
	    if (count>0) {
	      fpRawReader->RewindEvents();
	      fpRawReader->NextEvent();
	    } else {
	      delete fpRawReader;
	      fpRawReader=NULL;
	    }
	}
      } else if (token.Contains("writerawfiles=")) {
	if (!token.ReplaceAll("writerawfiles=", "").Contains("HLT")) {
	  AliHLTOUTComponent::ClearGlobalOption(AliHLTOUTComponent::kWriteRawFiles);
	}
      } else {
	if (sysOp.Length()>0) sysOp+=" ";
	sysOp+=token;
      }
    }
    delete pTokens;
  }

  AliCDBManager* man = AliCDBManager::Instance();
  if (man && man->IsDefaultStorageSet())
  {
    int runNo=pRunLoader->GetHeader()->GetRun();

    // init solenoid field
    Double_t solenoidBz=0;
    AliMagF *field = (AliMagF*)TGeoGlobalMagField::Instance()->GetField();
    if (field) {
      // this field definition is rather awkward: AliMagF::SolenoidField returns
      // a signed value, the amazing thing is that the sign is opposite to that
      // one in the factor. So the abs value has to be used. Lets assume, there
      // is a reason for that confusing implementation ...
      solenoidBz=TMath::Abs(field->SolenoidField())*field->Factor();
      AliDebug(0,Form("magnetic field: %f %f", field->SolenoidField(),field->Factor()));
    } else {
      // workaround for bug #51285
      AliError("can not get the AliMagF instance, falling back to GRP entry");
      AliCDBEntry *pGRPEntry = man->Get("GRP/GRP/Data", runNo);
      if (pGRPEntry) {
	AliGRPObject* pGRPData=dynamic_cast<AliGRPObject*>(pGRPEntry->GetObject());
	assert(pGRPData!=NULL);
	if (pGRPData) {
	  // this is just a workaround at the moment, common functionality in AliReconstruction
	  // is needed to reconstruct the magnetic field in a common way
	  // the code is partly taken from AliReconstruction::InitGRP
	  Bool_t ok = kTRUE;
	  Float_t l3Current = pGRPData->GetL3Current((AliGRPObject::Stats)0);
	  if (l3Current == AliGRPObject::GetInvalidFloat()) {
	    AliError("GRP/GRP/Data entry:  missing value for the L3 current !");
	    ok = kFALSE;
	  }
    
	  Char_t l3Polarity = pGRPData->GetL3Polarity();
	  if (l3Polarity == AliGRPObject::GetInvalidChar()) {
	    AliError("GRP/GRP/Data entry:  missing value for the L3 polarity !");
	    ok = kFALSE;
	  }
	  
	  if (ok) {
	    solenoidBz=l3Current/6000;
	    if (l3Polarity) solenoidBz*=-1;
	  } else {
	    AliError("invalid L3 field information in GRP entry");
	  }
	}
      }
    }
    const char* cdbSolenoidPath="HLT/ConfigHLT/SolenoidBz";
    TString cdbSolenoidParam;
    cdbSolenoidParam.Form("-solenoidBz %f", solenoidBz);

    // check if the entry is already there
    AliCDBEntry *pEntry = man->Get(cdbSolenoidPath, runNo);
    TObjString* pString=NULL;
    if (pEntry) pString=dynamic_cast<TObjString*>(pEntry->GetObject());

    if (!pEntry || !pString || pString->GetString().CompareTo(cdbSolenoidParam)!=0) {
      TObjString obj(cdbSolenoidParam);
      AliCDBPath cdbSolenoidEntry(cdbSolenoidPath);
      AliCDBId cdbSolenoidId(cdbSolenoidEntry, runNo, runNo, 0, 0);
      AliCDBMetaData cdbMetaData;
      cdbMetaData.SetResponsible("Matthias.Richter@cern.ch");
      cdbMetaData.SetComment("Automatically produced GRP entry (AliHLTSimulation) for the magnetic field initialization of HLT components");
      man->Put(&obj, cdbSolenoidId, &cdbMetaData);

      // unload the cache due to bug #51281
      man->UnloadFromCache(cdbSolenoidPath);
    }
  } else if (man) {
    AliError("OCDB default storage not yet set, can not prepare OCDB entries");    
  } else {
    AliError("unable to get instance of AliCDBMetaData, can not prepare OCDB entries");    
  }

  // scan options
  if (pSystem->ScanOptions(sysOp.Data())<0) {
    AliError("error setting options for HLT system");
    return -EINVAL;	
  }

  if (!pSystem->CheckStatus(AliHLTSystem::kReady)) {
    if ((pSystem->Configure(fpRawReader, pRunLoader))<0) {
      AliError("error during HLT system configuration");
      return -EFAULT;
    }
  }

  return 0;
}


int AliHLTSimulation::Run(AliRunLoader* pRunLoader)
{
  // HLT reconstruction for simulated data  
  if(!fpPluginBase) {
    AliError("internal initialization failed");
    return -EINVAL;
  }

  if(!pRunLoader) {
    AliError("Missing RunLoader! 0x0");
    return -EINVAL;
  }

  int nEvents = pRunLoader->GetNumberOfEvents();
  int iResult=0;

  AliHLTSystem* pSystem=fpPluginBase->GetInstance();
  if (!pSystem) {
    AliError("can not get AliHLTSystem instance");
    return -ENOMEM;
  }

  if (pSystem->CheckStatus(AliHLTSystem::kError)) {
    AliError("HLT system in error state");
    return -EFAULT;
  }

  // Note: the rawreader is already placed at the first event
  if ((iResult=pSystem->Reconstruct(1, pRunLoader, fpRawReader))>=0) {
    pSystem->FillESD(0, pRunLoader, NULL);
    for (int i=1; i<nEvents; i++) {
      if (fpRawReader && !fpRawReader->NextEvent()) {
	AliError("mismatch in event count, rawreader corrupted");
	break;
      }
      pSystem->Reconstruct(1, pRunLoader, fpRawReader);
      pSystem->FillESD(i, pRunLoader, NULL);
    }
    // send specific 'event' to execute the stop sequence
    pSystem->Reconstruct(0, NULL, NULL);
  }
  return iResult;
}


AliHLTSimulation* AliHLTSimulationCreateInstance()
{
  // see header file for function documentation
  return AliHLTSimulation::CreateInstance();
}

int AliHLTSimulationDeleteInstance(AliHLTSimulation* pSim)
{
  // see header file for function documentation
  return AliHLTSimulation::DeleteInstance(pSim);
}

int AliHLTSimulationInit(AliHLTSimulation* pSim, AliRunLoader* pRunLoader, const char* options)
{
  assert(pSim!=NULL);
  if (pSim) {
    return pSim->Init(pRunLoader, options);
  }
  return -ENODEV;
}

int AliHLTSimulationRun(AliHLTSimulation* pSim, AliRunLoader* pRunLoader)
{
  assert(pSim!=NULL);
  if (pSim) {
    return pSim->Run(pRunLoader);
  }
  return -ENODEV;
}

int AliHLTSimulationGetLibraryVersion()
{
  // see header file for function documentation
  return LIBHLTSIM_VERSION;
}

int AliHLTSimulationSetup(AliHLTSimulation* /*pHLTSim*/, AliSimulation* pSim, const char* specificObjects)
{
  // see header file for function documentation

  // this is an attempt to solve issue #48360
  // since there are many jobs running in parallel during the production,
  // all the jobs want to put entries into the OCDB. The solution is to
  // make them temporary, since they are only used to propagate information
  // from the simulation to the reconstruction.

  if (!pSim) return -EINVAL;
  const char* entries[]={
    "HLT/ConfigHLT/SolenoidBz",
    "HLT/ConfigHLT/esdLayout",
    NULL
  };

  TString specificStorage; 
  specificStorage.Form("local://%s",gSystem->pwd());
  for (const char** pEntry=entries; *pEntry!=NULL; pEntry++) {
    const char* pObject=specificObjects?strstr(specificObjects, *pEntry):NULL;
    if (pObject) {
      // skip this entry if it is found in the list and either
      // last one or separated by a blank
      pObject+=strlen(*pEntry);
      if (*pObject==0 || *pObject==' ') continue;
    }
    pSim->SetSpecificStorage(*pEntry, specificStorage.Data());
  }

  return 0;
}

#ifndef HAVE_COMPILEINFO
extern "C" void CompileInfo(const char*& date, const char*& time)
{
  // the fall back compile info of the HLTsim library
  // this is not up-to-date if other files have been changed and recompiled
  date=__DATE__; time=__TIME__;
  return;
}
#endif
