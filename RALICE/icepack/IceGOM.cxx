/*******************************************************************************
 * Copyright(c) 2003, IceCube Experiment at the South Pole. All rights reserved.
 *
 * Author: The IceCube RALICE-based Offline Project.
 * Contributors are mentioned in the code where appropriate.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation strictly for non-commercial purposes is hereby granted
 * without fee, provided that the above copyright notice appears in all
 * copies and that both the copyright notice and this permission notice
 * appear in the supporting documentation.
 * The authors make no claims about the suitability of this software for
 * any purpose. It is provided "as is" without express or implied warranty.
 *******************************************************************************/

// $Id$

///////////////////////////////////////////////////////////////////////////
// Class IceGOM
// Signal/Hit handling of a generic IceCube Optical Module (GOM).
// Basically this class provides an IceCube tailored user interface
// to the functionality of the class AliDevice.
// This class is meant to provide a base class for more specific OM's
// (i.e. Amanda analog OM's or IceCube digital OM's).
// To specifically address Amanda OM's, In-ice DOM's or IceTop DOM's
// please refer to the derived classes IceAOM, IceIDOM and IceTDOM resp.
//
// Example :
// =========
//
// Creation and filling of a generic Icecube module with fictituous data
// ---------------------------------------------------------------------
//
// For further functionality please refer to AliDevice, AliSignal and AliAttrib.
//
// IceGOM m;
// m.SetUniqueID(123);
// m.SetNameTitle("OM123","Generic IceCube module");
//
// // Indicate status (e.g. version of readout electronics)
// // via a user definable status word.
// Int_t stat=20031;
// m.SetStatus(stat);
//
// Float_t pos[3]={1,2,3};
// m.SetPosition(pos,"car");
//
// // The starting unique signal ID.
// // In this example it will be increased automatically
// // whenever a new signal is created.
// Int_t sid=10;
//
// AliSignal s;
//
// s.SetSlotName("ADC",1);
// s.SetSlotName("LE",2);
// s.SetSlotName("TOT",3);
//
// s.Reset();
// s.SetName("OM123 Hit 1");
// s.SetUniqueID(sid++);
// s.SetSignal(100,"ADC");
// s.SetSignal(-100,"LE");
// s.SetSignal(-1000,"TOT");
// m.AddHit(s);
//
// s.Reset();
// s.SetName("OM123 Hit 2");
// s.SetUniqueID(sid++);
// s.SetSignal(110,"ADC");
// s.SetSignal(-101,"LE");
// s.SetSignal(1001,"TOT");
// m.AddHit(s);
//
// s.Reset();
// s.SetName("OM123 Hit 3");
// s.SetUniqueID(sid++);
// s.SetSignal(120,"ADC");
// s.SetSignal(-102,"LE");
// s.SetSignal(-1002,"TOT");
// m.AddHit(s);
//
// // Provide module data overview
// m.Data();
//
// // Accessing the 3rd stored hit
// AliSignal* sx=m.GetHit(3);
// if (sx) sx->Data();
//
// // Explicit hit selection via unique ID
// AliSignal* sx=m.GetIdHit(12);
// if (sx) sx->Data();
//
// // Obtain the minimum and maximum recorded TOT value 
// Float_t vmin,vmax;
// m.GetExtremes(vmin,vmax,"TOT");
// cout << " Extreme values : vmin = " << vmin << " vmax = " << vmax << endl;
// 
// // Ordered hits w.r.t. decreasing TOT
// TObjArray* ordered=m.SortHits("TOT",-1);
// Int_t  nhits=0;
// if (ordered) nhits=ordered->GetEntries();
// for (Int_t i=0; i<nhits; i++)
// {
//  AliSignal* sx=(AliSignal*)ordered->At(i);
//  if (sx) sx->Data();
// }
//
//--- Author: Nick van Eijndhoven 23-jun-2004 Utrecht University
//- Modified: NvE $Date$ Utrecht University
///////////////////////////////////////////////////////////////////////////

#include "IceGOM.h"
#include "Riostream.h"
 
ClassImp(IceGOM) // Class implementation to enable ROOT I/O
 
IceGOM::IceGOM() : AliDevice()
{
// Default constructor.
}
///////////////////////////////////////////////////////////////////////////
IceGOM::~IceGOM()
{
// Default destructor.
}
///////////////////////////////////////////////////////////////////////////
IceGOM::IceGOM(const IceGOM& m) : AliDevice(m)
{
// Copy constructor.
}
///////////////////////////////////////////////////////////////////////////
Int_t IceGOM::GetString() const
{
// Provide the corresponding string number for this module.
// Note : Amanda string numbers will have negative values.

 Int_t omid=GetUniqueID();

 if (omid<=0) return 0;
 if (omid==681) return -18;

 Int_t string=0;
 if (InheritsFrom("IceAOM"))
 {
  if (omid<=20) return -1;
  if (omid>=21 && omid<=40) return -2;
  if (omid>=41 && omid<=60) return -3;
  if (omid>=61 && omid<=86) return -4;
  if (omid>=87 && omid<=122) return -5;
  if (omid>=123 && omid<=158) return -6;
  if (omid>=159 && omid<=194) return -7;
  if (omid>=195 && omid<=230) return -8;
  if (omid>=231 && omid<=266) return -9;
  if (omid>=267 && omid<=302) return -10;
  if (omid>=303 && omid<=344) return -11;
  if (omid>=345 && omid<=386) return -12;
  if (omid>=387 && omid<=428) return -13;
  if (omid>=429 && omid<=470) return -14;
  if (omid>=471 && omid<=512) return -15;
  if (omid>=513 && omid<=554) return -16;
  if (omid>=555 && omid<=596) return -17;
  if (omid>=597 && omid<=638) return -18;
  if (omid>=639 && omid<=680) return -19;
 }
 else
 {
  string=omid/100;
 }

 return string;
}
///////////////////////////////////////////////////////////////////////////
TObject* IceGOM::Clone(const char* name) const
{
// Make a deep copy of the current object and provide the pointer to the copy.
// This memberfunction enables automatic creation of new objects of the
// correct type depending on the object type, a feature which may be very useful
// for containers like AliEvent when adding objects in case the
// container owns the objects. This feature allows e.g. AliEvent
// to store either IceGOM objects or objects derived from IceGOM
// via tha AddDevice memberfunction, provided these derived classes also have
// a proper Clone memberfunction. 

 IceGOM* m=new IceGOM(*this);
 if (name)
 {
  if (strlen(name)) m->SetName(name);
 }
 return m;
}
///////////////////////////////////////////////////////////////////////////
