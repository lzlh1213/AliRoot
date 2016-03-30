/**************************************************************************
 * This file is property of and copyright by the ALICE HLT Project        * 
 * ALICE Experiment at CERN, All rights reserved.                         *
 *                                                                        *
 * Primary Author: Mikolaj Krzewicki, mikolaj.krzewicki@cern.ch           *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

#include "AliHLTZMQsink.h"
#include "AliHLTErrorGuard.h"
#include "TDatime.h"
#include "TRandom3.h"
#include <TObject.h>
#include <TPRegexp.h>
#include "zmq.h"
#include "AliZMQhelpers.h"

using namespace std;

ClassImp(AliHLTZMQsink)

//______________________________________________________________________________
AliHLTZMQsink::AliHLTZMQsink() :
  AliHLTComponent()
  , fZMQcontext(NULL)
  , fZMQout(NULL)
  , fZMQsocketType(-1)
  , fZMQoutConfig("PUB")
  , fZMQpollIn(kFALSE)
  , fPushbackDelayPeriod(-1)
  , fIncludePrivateBlocks(kFALSE)
  , fZMQneverBlock(kTRUE)
  , fSendRunNumber(kTRUE)
  , fNskippedErrorMessages(0)
  , fZMQerrorMsgSkip(100)
  , fSendECSparamString(kFALSE)
  , fECSparamString()
{
  //ctor
}

//______________________________________________________________________________
AliHLTZMQsink::~AliHLTZMQsink()
{
  //dtor
  zmq_close(fZMQout);
  zmq_ctx_destroy(fZMQcontext);
}

//______________________________________________________________________________
const Char_t* AliHLTZMQsink::GetComponentID()
{
  //id
  return "ZMQsink";
}

//______________________________________________________________________________
AliHLTComponentDataType AliHLTZMQsink::GetOutputDataType()
{
  // default method as sink components do not produce output
  AliHLTComponentDataType dt =
    {sizeof(AliHLTComponentDataType),
     kAliHLTVoidDataTypeID,
     kAliHLTVoidDataOrigin};
  return dt;
}

//______________________________________________________________________________
void AliHLTZMQsink::GetInputDataTypes( vector<AliHLTComponentDataType>& list)
{
  //what data types do we accept
  list.clear();
  list.push_back(kAliHLTAllDataTypes);
}

//______________________________________________________________________________
void AliHLTZMQsink::GetOutputDataSize( unsigned long& constBase, double& inputMultiplier )
{
  // default method as sink components do not produce output
  constBase=0;
  inputMultiplier=0;
}

//______________________________________________________________________________
AliHLTComponent* AliHLTZMQsink::Spawn()
{
  //Spawn a new instance
  return new AliHLTZMQsink();
}

//______________________________________________________________________________
Int_t AliHLTZMQsink::DoInit( Int_t /*argc*/, const Char_t** /*argv*/ )
{
  // see header file for class documentation
  Int_t retCode=0;

  //process arguments
  if (ProcessOptionString(GetComponentArgs())<0) 
  {
    HLTFatal("wrong config string! %s", GetComponentArgs().c_str());
    return -1;
  }

  int rc = 0;
  //init ZMQ context
  fZMQcontext = alizmq_context();
  HLTMessage(Form("ctx create ptr %p %s",fZMQcontext,(rc<0)?zmq_strerror(errno):""));
  if (!fZMQcontext) return -1;

  //init ZMQ socket
  rc = alizmq_socket_init(fZMQout, fZMQcontext, fZMQoutConfig.Data(), 0, 10 ); 
  if (!fZMQout || rc<0) 
  {
    HLTError("cannot initialize ZMQ socket %s, %s",fZMQoutConfig.Data(),zmq_strerror(errno));
    return -1;
  }
  
  HLTMessage(Form("socket create ptr %p %s",fZMQout,(rc<0)?zmq_strerror(errno):""));
  HLTImportant(Form("ZMQ connected to: %s (%s(id %i)) rc %i %s",fZMQoutConfig.Data(),alizmq_socket_name(fZMQsocketType),fZMQsocketType,rc,(rc<0)?zmq_strerror(errno):""));
  
  return retCode;
}

//______________________________________________________________________________
Int_t AliHLTZMQsink::DoDeinit()
{
  // see header file for class documentation
  return 0;
}

//______________________________________________________________________________
int AliHLTZMQsink::DoProcessing( const AliHLTComponentEventData& evtData,
                                const AliHLTComponentBlockData* blocks, 
                                AliHLTComponentTriggerData& /*trigData*/,
                                AliHLTUInt8_t* /*outputPtr*/, 
                                AliHLTUInt32_t& /*size*/,
                                AliHLTComponentBlockDataList& outputBlocks,
                                AliHLTComponentEventDoneData*& /*edd*/ )
{ 
  // see header file for class documentation
  Int_t retCode=0;
  
  //create a default selection of any data:
  int requestTopicSize=-1;
  char requestTopic[kAliHLTComponentDataTypeTopicSize];
  memset(requestTopic, '*', kAliHLTComponentDataTypeTopicSize);
  int requestSize=-1;
  char request[kAliHLTComponentDataTypeTopicSize];
  memset(request, '*', kAliHLTComponentDataTypeTopicSize);

  int rc = 0;
  Bool_t doSend = kTRUE;
  Bool_t doSendECSparamString = kFALSE;
  
  //cache an ECS param topic
  char ecsParamTopic[kAliHLTComponentDataTypeTopicSize];
  DataType2Topic(kAliHLTDataTypeECSParam, ecsParamTopic);

  //in case we reply to requests instead of just pushing/publishing
  //we poll for requests
  if (fZMQpollIn)
  {
    zmq_pollitem_t items[] = { { fZMQout, 0, ZMQ_POLLIN, 0 } };
    zmq_poll(items, 1, 0);

    if (items[0].revents & ZMQ_POLLIN)
    {
      int64_t more=0;
      size_t moreSize=sizeof(more);
      do //request could be multipart, get all parts
      {
        requestTopicSize = zmq_recv (fZMQout, requestTopic, kAliHLTComponentDataTypeTopicSize, 0);
        zmq_getsockopt(fZMQout, ZMQ_RCVMORE, &more, &moreSize);
        if (more) {
          requestSize = zmq_recv(fZMQout, request, kAliHLTComponentDataTypeTopicSize, 0);
          zmq_getsockopt(fZMQout, ZMQ_RCVMORE, &more, &moreSize);
        }
        //if request is for ECS params, set the flag
        if (Topicncmp(requestTopic, ecsParamTopic, requestTopicSize))
        //if (*reinterpret_cast<const AliHLTUInt64_t*>(requestTopic) ==
        //    *reinterpret_cast<const AliHLTUInt64_t*>(kAliHLTDataTypeECSParam.fID))
        {
          doSendECSparamString = kTRUE;
        }
      } while (more==1);
    }
    else { doSend = kFALSE; }
  }
 
  //if enabled (option -pushback-period), send at most so often
  if (fPushbackDelayPeriod>0)
  {
    TDatime time;
    if ((Int_t)time.Get()-fLastPushbackDelayTime<fPushbackDelayPeriod) 
    {
      doSend=kFALSE;
    }
  }  

  if (doSend)
  {
    //set the time of current push
    if (fPushbackDelayPeriod>0)
    {
      TDatime time;
      fLastPushbackDelayTime=time.Get();
    }

    //first make a map of selected blocks, and identify the last one
    //so we can properly mark the last block for multipart ZMQ sending later
    const AliHLTComponentBlockData* inputBlock = NULL;
    std::vector<int> selectedBlockIdx;
    for (int iBlock = 0;
         iBlock < evtData.fBlockCnt;
         iBlock++) 
    {
      inputBlock = &blocks[iBlock];
      //cache the ECS param string
      if (*reinterpret_cast<const AliHLTUInt64_t*>(inputBlock->fDataType.fID) ==
          *reinterpret_cast<const AliHLTUInt64_t*>(kAliHLTDataTypeECSParam.fID))
      {
        const char* ecsparamstr = reinterpret_cast<const char*>(inputBlock->fPtr);
        int ecsparamsize = inputBlock->fSize;
        if (ecsparamstr[ecsparamsize-1]!=0)
        {
          fECSparamString.Insert(0, ecsparamstr, ecsparamsize);
          fECSparamString += "";
        }
        else
        {
          fECSparamString = ecsparamstr;
        }
        //if we requested the ECS param, we can continue here, will be sent anyway
        if (doSendECSparamString) continue;
      }

      //don't include provate data unless explicitly asked to
      if (!fIncludePrivateBlocks && 
          *reinterpret_cast<const AliHLTUInt32_t*>(inputBlock->fDataType.fOrigin) ==
          *reinterpret_cast<const AliHLTUInt32_t*>(kAliHLTDataOriginPrivate))
      {
        continue;
      }

      //check if the data type matches the request
      char blockTopic[kAliHLTComponentDataTypeTopicSize];
      DataType2Topic(inputBlock->fDataType, blockTopic);
      if (Topicncmp(requestTopic, blockTopic, requestTopicSize))
      {
        selectedBlockIdx.push_back(iBlock);
      }
    }
    int nSelectedBlocks = selectedBlockIdx.size();
    int nSentBlocks = 0;

    //only send the INFO block if there is something to send
    if (fSendRunNumber && nSelectedBlocks>0)
    {
      string runNumberString = "run=";
      char tmp[34];
      snprintf(tmp,34,"%i",GetRunNo()); 
      runNumberString+=tmp;
      rc = zmq_send(fZMQout, "INFO", 4, ZMQ_SNDMORE);
      rc = zmq_send(fZMQout, runNumberString.data(), runNumberString.size(), ZMQ_SNDMORE);
      if (rc>=0) nSentBlocks++;
    }

    //maybe send the ECS param string
    //once if requested or always if so configured
    if (fSendECSparamString || doSendECSparamString)
    {
      AliHLTDataTopic topic = kAliHLTDataTypeECSParam;
      rc = zmq_send(fZMQout, &topic, sizeof(topic), ZMQ_SNDMORE);
      int flags = (nSelectedBlocks==0)?0:ZMQ_SNDMORE;
      rc = zmq_send(fZMQout, fECSparamString.Data(), fECSparamString.Length(), flags);
      if (rc>=0) nSentBlocks++;
      doSendECSparamString = kFALSE;
    }

    //send the selected blocks
    for (int iSelectedBlock = 0;
         iSelectedBlock < selectedBlockIdx.size();
         iSelectedBlock++) 
    {
      inputBlock = &blocks[selectedBlockIdx[iSelectedBlock]];
      AliHLTDataTopic blockTopic = *inputBlock;

      //send:
      //  first part : AliHLTComponentDataType in string format
      //  second part: Payload
      rc = zmq_send(fZMQout, &blockTopic, sizeof(blockTopic), ZMQ_SNDMORE);
      HLTMessage(Form("send topic rc %i %s",rc,(rc<0)?zmq_strerror(errno):""));
      int flags = 0;
      if (fZMQneverBlock) flags = ZMQ_DONTWAIT;
      if (iSelectedBlock < (selectedBlockIdx.size()-1)) flags = ZMQ_SNDMORE;
      rc = zmq_send(fZMQout, inputBlock->fPtr, inputBlock->fSize, flags);
      if (rc<0 && (fNskippedErrorMessages++ >= fZMQerrorMsgSkip))
      {
        fNskippedErrorMessages=0;
        HLTWarning("error sending data frame %s, %s",
                   blockTopic.Description().c_str(),
                   zmq_strerror(errno));
      }
      else
      {
        nSentBlocks++;
      }
      HLTMessage(Form("send data rc %i %s",rc,(rc<0)?zmq_strerror(errno):""));
    }
    
    //send an empty message if we really need a reply (ZMQ_REP mode)
    //only in case no blocks were sent
    if (nSentBlocks == 0 && fZMQsocketType==ZMQ_REP)
    { 
      rc = zmq_send(fZMQout, 0, 0, ZMQ_SNDMORE);
      HLTMessage(Form("send endframe rc %i %s",rc,(rc<0)?zmq_strerror(errno):""));
      if (rc<0) HLTWarning("error sending dummy REP topic");
      rc = zmq_send(fZMQout, 0, 0, 0);
      HLTMessage(Form("send endframe rc %i %s",rc,(rc<0)?zmq_strerror(errno):""));
      if (rc<0) HLTWarning("error sending dummy REP data");
    }
  }

  outputBlocks.clear();
  return retCode;
}

//______________________________________________________________________________
int AliHLTZMQsink::ProcessOption(TString option, TString value)
{
  //process option
  //to be implemented by the user
  
  if (option.EqualTo("out"))
  {
    fZMQoutConfig = value;
    fZMQsocketType = alizmq_socket_type(value.Data());
    switch (fZMQsocketType)
    {
      case ZMQ_REP:
        fZMQpollIn=kTRUE;
        break;
      case ZMQ_PUSH:
        fZMQpollIn=kFALSE;
        break;
      case ZMQ_PUB:
        fZMQpollIn=kFALSE;
        break;
      default:
        HLTFatal("use of socket type %s for a sink is currently unsupported! (config: %s)", alizmq_socket_name(fZMQsocketType), fZMQoutConfig.Data());
        return -EINVAL;
    }
  }
  else if (option.EqualTo("SendRunNumber"))
  {
    fSendRunNumber=(value.EqualTo("0") || value.EqualTo("no") || value.EqualTo("false"))?kFALSE:kTRUE;
  }

  else if (option.EqualTo("SendECSparamString"))
  {
    fSendECSparamString=(value.EqualTo("0") || value.EqualTo("no") || value.EqualTo("false"))?kFALSE:kTRUE;
  }

  else if (option.EqualTo("pushback-period"))
  {
    HLTMessage(Form("Setting pushback delay to %i", atoi(value.Data())));
    fPushbackDelayPeriod = atoi(value.Data());
  }

  else if (option.EqualTo("IncludePrivateBlocks"))
  {
    fIncludePrivateBlocks=kTRUE;
  }

  else if (option.EqualTo("ZMQneverBlock"))
  {
    if (value.EqualTo("0") || value.EqualTo("no") || value.Contains("false",TString::kIgnoreCase))
      fZMQneverBlock = kFALSE;
    else if (value.EqualTo("1") || value.EqualTo("yes") || value.Contains("true",TString::kIgnoreCase) )
      fZMQneverBlock = kTRUE;
  }
  
  else if (option.EqualTo("ZMQerrorMsgSkip"))
  {
    fZMQerrorMsgSkip = value.Atoi();
  }

  return 1; 
}

