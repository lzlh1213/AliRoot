#ifndef ALITPCSPACECHARGE3D_H
#define ALITPCSPACECHARGE3D_H

/* Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 * See cxx source for full Copyright notice                               */

////////////////////////////////////////////////////////////////////////////
//                                                                        //
// AliTPCSpaceCharge3D class                                           //
// date: 19/06/2010                                                       //
// Authors: Stefan Rossegger                                              //
////////////////////////////////////////////////////////////////////////////

#include "AliTPCCorrection.h"
class TH3F;

class AliTPCSpaceCharge3D : public AliTPCCorrection {
public:
  AliTPCSpaceCharge3D();
  virtual ~AliTPCSpaceCharge3D();

  // initialization and update functions
  virtual void Init();
  virtual void Update(const TTimeStamp &timeStamp);

  // common setters and getters for tangled ExB effect
  virtual void SetOmegaTauT1T2(Float_t omegaTau,Float_t t1,Float_t t2) {
    fT1=t1; fT2=t2;
    const Double_t wt0=t2*omegaTau;     fC0=1./(1.+wt0*wt0);
    const Double_t wt1=t1*omegaTau;     fC1=wt1/(1.+wt1*wt1);
  };
  void SetC0C1(Float_t c0,Float_t c1) {fC0=c0;fC1=c1;} // CAUTION: USE WITH CARE
  Float_t GetC0() const {return fC0;}
  Float_t GetC1() const {return fC1;}

  // setters and getters
  void SetCorrectionFactor(Float_t correctionFactor) {fCorrectionFactor=correctionFactor;}
  Float_t GetCorrectionFactor() const {return fCorrectionFactor;}

  void  SetSCDataFileName(char *const fname);
  char* GetSCDataFileName() const { return fSCDataFileName; }

  void InitSpaceCharge3DDistortion();

  Float_t GetSpaceChargeDensity(Float_t r, Float_t phi, Float_t z);
  TH2F* CreateHistoSCinXY(Float_t z, Int_t nx=100, Int_t ny=100);
  TH2F* CreateHistoSCinZR(Float_t phi, Int_t nz=100, Int_t nr=100);


  void WriteChargeDistributionToFile(const char* fname = "SC-Alice.root");

  virtual void Print(const Option_t* option="") const;

protected:
  virtual void GetCorrection(const Float_t x[],const Short_t roc,Float_t dx[]);

private:

  AliTPCSpaceCharge3D(const AliTPCSpaceCharge3D &);               // not implemented
  AliTPCSpaceCharge3D &operator=(const AliTPCSpaceCharge3D &);    // not implemented

  Float_t fC0; // coefficient C0                 (compare Jim Thomas's notes for definitions)
  Float_t fC1; // coefficient C1                 (compare Jim Thomas's notes for definitions)
  Float_t fCorrectionFactor;       // Space Charge Correction factor in comparison to initialized
                                   // look up table which was created for M_mb = 900 and IR = 3000
                                   // compare Internal Note Nr: ???

  Bool_t fInitLookUp;                 // flag to check it the Look Up table was created

  TMatrixD *fLookUpErOverEz[kNPhi];   // Array to store electric field integral (int Er/Ez)
  TMatrixD *fLookUpEphiOverEz[kNPhi]; // Array to store electric field integral (int Er/Ez)
  TMatrixD *fLookUpDeltaEz[kNPhi];    // Array to store electric field integral (int Er/Ez)

  char *fSCDataFileName;         // file which contains the space charge distribution
  char *fSCLookUpPOCsFileName;   // filename of the precalculated lookup tables (for individual voxels)

  TMatrixD *fSCdensityDistribution[kNPhi]; // space charge distribution
 
  ClassDef(AliTPCSpaceCharge3D,1); 
};

#endif
