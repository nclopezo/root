// @(#)root/meta:$
// Author: Axel Naumann 2014-05-02

/*************************************************************************
 * Copyright (C) 1995-2014, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// Persistent version of a TClass.                                      //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TProtoClass.h"

#include "TBaseClass.h"
#include "TClass.h"
#include "TDataMember.h"
#include "TEnum.h"
#include "TInterpreter.h"
#include "TList.h"
#include "TListOfDataMembers.h"
#include "TListOfEnums.h"
#include "TRealData.h"

//______________________________________________________________________________
TProtoClass::TProtoClass(TClass* cl):
   TNamed(*cl), fBase(cl->GetListOfBases()), fData(cl->GetListOfDataMembers()),
   fEnums(cl->GetListOfEnums()), fSizeof(cl->Size()), fCanSplit(cl->fCanSplit),
   fStreamerType(cl->fStreamerType), fProperty(cl->fProperty),
   fClassProperty(cl->fClassProperty)
{
   // Initialize a TProtoClass from a TClass.

   if (cl->Property() & kIsNamespace){
      fData=new TListOfDataMembers();
      fEnums=nullptr;
      fPRealData=nullptr;
      fOffsetStreamer=0;
      return;
   }

   fPRealData = new TList();

   if (!cl->GetCollectionProxy()) {
      // Build the list of RealData before we access it:
      cl->BuildRealData(0, true /*isTransient*/);
      // The data members are ordered as follows:
      // - this class's data members,
      // - foreach base: base class's data members.
      // fPRealData encodes all TProtoRealData objects with a
      // TObjString to signal a new class.
      TClass* clCurrent = cl;
      TRealData* precRd = nullptr;
      for (auto realDataObj: *cl->GetListOfRealData()) {
         TRealData *rd = (TRealData*)realDataObj;
         if (!precRd) precRd = rd;
         TClass* clRD = rd->GetDataMember()->GetClass();
         if (clRD != clCurrent) {
            clCurrent = clRD;
            TObjString *clstr = new TObjString(clRD->GetName());
            if (precRd->TestBit(TRealData::kTransient)) {
               clstr->SetBit(TRealData::kTransient);
            }
            fPRealData->AddLast(clstr);
            precRd = rd;
         }
         fPRealData->AddLast(new TProtoRealData(rd));
      }

      if (gDebug > 2) {
         for (auto dataPtr : *fPRealData) {
            const auto classType = dataPtr->IsA();
            const auto dataPtrName = dataPtr->GetName();
            if (classType == TProtoRealData::Class())
               Info("TProtoClass","Data is a protorealdata: %s", dataPtrName);
            if (classType == TObjString::Class())
               Info("TProtoClass","Data is a objectstring: %s", dataPtrName);
            if (dataPtr->TestBit(TRealData::kTransient))
               Info("TProtoClass","And is transient");
         }
      }
   }

   cl->CalculateStreamerOffset();
   fOffsetStreamer = cl->fOffsetStreamer;
}

//______________________________________________________________________________
TProtoClass::~TProtoClass()
{
   // Destructor.
   Delete();
}

//______________________________________________________________________________
void TProtoClass::Delete(Option_t* opt /*= ""*/) {
   // Delete the containers that are usually owned by their TClass.
   if (fPRealData) fPRealData->Delete(opt);
   delete fPRealData; fPRealData = 0;
   if (fBase) fBase->Delete(opt);
   delete fBase; fBase = 0;
   if (fData) fData->Delete(opt);
   delete fData; fData = 0;
   if (fEnums) fEnums->Delete(opt);
   delete fEnums; fEnums = 0;
}

//______________________________________________________________________________
Bool_t TProtoClass::FillTClass(TClass* cl) {
   // Move data from this TProtoClass into cl.
   if (cl->fRealData || cl->fBase || cl->fData || cl->fEnums
       || cl->fSizeof != -1 || cl->fCanSplit >= 0
       || cl->fProperty != (-1) ) {
      if (cl->fProperty & kIsNamespace){
         if (gDebug>0) Info("FillTClass", "Returning w/o doing anything. %s is a namespace.",cl->GetName());
         return kTRUE;
      }
      Error("FillTClass", "TClass %s already initialized!", cl->GetName());
      return kFALSE;
   }
   if (gDebug > 1) Info("FillTClass","Loading TProtoClass for %s - %s",cl->GetName(),GetName());

   if (fPRealData) {

      // A first loop to retrieve the mother classes before starting to
      // fill this TClass instance. This is done in order to avoid recursions
      // for example in presence of daughter and mother class present in two
      // dictionaries compiled in two different libraries which are not linked
      // one with each other.
      for (TObject* element: *fPRealData) {
         if (element->IsA() == TObjString::Class()) {
            if (gDebug > 1) Info("","Treating beforehand mother class %s",element->GetName());
            int autoparsingOldval=gInterpreter->SetClassAutoparsing(false);
            TClass::GetClass(element->GetName());
            gInterpreter->SetClassAutoparsing(autoparsingOldval);
         }
      }
   }


   // Copy only the TClass bits.
   // not bit 13 and below and not bit 24 and above, just Bits 14 - 23
   UInt_t newbits = TestBits(0x00ffc000);
   cl->ResetBit(0x00ffc000);
   cl->SetBit(newbits);

   cl->fName  = this->fName;
   cl->fTitle = this->fTitle;
   cl->fBase = fBase;
   cl->fData = (TListOfDataMembers*)fData;
   // We need to fill enums one by one to initialise the internal map which is
   // transient
   cl->fEnums = new TListOfEnums();
   for (TObject* enumAsTObj : *fEnums){
      cl->fEnums->Add((TEnum*) enumAsTObj);
   }
   cl->fRealData = new TList(); // FIXME: this should really become a THashList!

   cl->fSizeof = fSizeof;
   cl->fCanSplit = fCanSplit;
   cl->fProperty = fProperty;
   cl->fClassProperty = fClassProperty;
   cl->fStreamerType = fStreamerType;

   // Update pointers to TClass
   if (cl->fBase) {
      for (auto base: *cl->fBase) {
         ((TBaseClass*)base)->SetClass(cl);
      }
   }
   if (cl->fData) {
      for (auto dm: *cl->fData) {
         ((TDataMember*)dm)->SetClass(cl);
      }
      ((TListOfDataMembers*)cl->fData)->SetClass(cl);
   }
   if (cl->fEnums) {
      for (auto en: *cl->fEnums) {
         ((TEnum*)en)->SetClass(cl);
      }
      ((TListOfEnums*)cl->fEnums)->SetClass(cl);
   }

   TClass* currentRDClass = cl;
   if (fPRealData) {
      for (TObject* element: *fPRealData) {
         if (element->IsA() == TObjString::Class()) {
            // We now check for the TClass entry, w/o loading. Indeed we did that above.
            // If the class is not found, it means that really it was not selected and we
            // replace it with an empty placeholder with the status of kForwardDeclared.
            // Interactivity will be of course possible but if IO is attempted, a warning
            // will be issued.
            int autoparsingOldval=gInterpreter->SetClassAutoparsing(false);
            // Disable autoparsing which might be triggered by the use of ResolvedTypedef
            // and the fallback new TClass() below.
            currentRDClass = TClass::GetClass(element->GetName(), false /* Load */ );
            if (!currentRDClass && !element->TestBit(TRealData::kTransient)) {
               if (gDebug>1)
                  Info("FillTClass()",
                       "Cannot find TClass for %s; Creating an empty one in the kForwardDeclared state.",
                       element->GetName());
               currentRDClass = new TClass(element->GetName(),1,TClass::kForwardDeclared, true /*silent*/);
            }
            gInterpreter->SetClassAutoparsing(autoparsingOldval);
         } else {
            if (!currentRDClass) continue;
            TProtoRealData* prd = (TProtoRealData*)element;
            if (TRealData* rd = prd->CreateRealData(currentRDClass, cl)) {
               cl->fRealData->AddLast(rd);
            }
         }
      }
   }

   cl->SetStreamerImpl();

   fBase = 0;
   fData = 0;
   fEnums = 0;
   if (fPRealData) fPRealData->Delete();
   delete fPRealData;
   fPRealData = 0;

   return kTRUE;
}

//______________________________________________________________________________
TProtoClass::TProtoRealData::TProtoRealData(const TRealData* rd):
   TNamed(rd->GetDataMember()->GetName(), rd->GetName()),
   fOffset(rd->GetThisOffset())
{
   // Initialize this from a TRealData object.
   SetBit(kIsObject, rd->IsObject());
   SetBit(TRealData::kTransient, rd->TestBit(TRealData::kTransient));
}

//______________________________________________________________________________
TProtoClass::TProtoRealData::~TProtoRealData()
{
   // Destructor to pin vtable.
}

//______________________________________________________________________________
TRealData* TProtoClass::TProtoRealData::CreateRealData(TClass* dmClass,
                                                       TClass* parent) const
{
   // Create a TRealData from this, with its data member coming from dmClass.
   TDataMember* dm = (TDataMember*)dmClass->GetListOfDataMembers()->FindObject(GetName());
   if (!dm && dmClass->GetState()!=TClass::kForwardDeclared) {
      Error("CreateRealData",
            "Cannot find data member %s::%s for parent %s!", dmClass->GetName(),
            GetName(), parent->GetName());
      return nullptr;
   }
   TRealData* rd = new TRealData(GetTitle(), fOffset, dm);
   rd->SetIsObject(TestBit(kIsObject));
   return rd;
}
