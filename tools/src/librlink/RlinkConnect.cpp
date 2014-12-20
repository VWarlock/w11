// $Id: RlinkConnect.cpp 611 2014-12-10 23:23:58Z mueller $
//
// Copyright 2011-2014 by Walter F.J. Mueller <W.F.J.Mueller@gsi.de>
//
// This program is free software; you may redistribute and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 2, or at your option any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for complete details.
// 
// Revision History: 
// Date         Rev Version  Comment
// 2014-12-10   611   2.0    re-organize for rlink v4
// 2014-08-26   587   1.5    start accept rlink v4 protocol (partially...)
// 2014-08-15   583   1.4    rb_mreq addr now 16 bit
// 2014-07-27   575   1.3.3  ExecPart(): increase packet tout from 5 to 15 sec
// 2013-04-21   509   1.3.2  add SndAttn() method
// 2013-03-01   493   1.3.1  add Server(Active..|SignalAttn)() methods
// 2013-02-23   492   1.3    use scoped_ptr for Port; Close allways allowed
//                           use RlinkContext, add Context(), Exec(..., cntx)
// 2013-02-22   491   1.2    use new RlogFile/RlogMsg interfaces
// 2013-02-03   481   1.1.2  use Rexception
// 2013-01-13   474   1.1.1  add PollAttn() method
// 2011-04-25   380   1.1    use boost::(mutex&lock), implement Lockable IF
// 2011-04-22   379   1.0.1  add Lock(), Unlock(), lock connect in Exec()
// 2011-04-02   375   1.0    Initial version
// 2011-01-15   356   0.1    First draft
// ---------------------------------------------------------------------------

/*!
  \file
  \version $Id: RlinkConnect.cpp 611 2014-12-10 23:23:58Z mueller $
  \brief   Implemenation of RlinkConnect.
*/

#include <iostream>

#include "boost/thread/locks.hpp"

#include "RlinkPortFactory.hpp"
#include "librtools/RosFill.hpp"
#include "librtools/RosPrintf.hpp"
#include "librtools/RosPrintBvi.hpp"
#include "librtools/Rtools.hpp"
#include "librtools/Rexception.hpp"
#include "librtools/RlogMsg.hpp"
#include "RlinkServer.hpp"

#include "RlinkConnect.hpp"

using namespace std;

/*!
  \class Retro::RlinkConnect
  \brief FIXME_docs
*/

// all method definitions in namespace Retro
namespace Retro {

//------------------------------------------+-----------------------------------
// constants definitions

const uint16_t RlinkConnect::kRbaddr_RLCNTL;
const uint16_t RlinkConnect::kRbaddr_RLSTAT;
const uint16_t RlinkConnect::kRbaddr_RLID1;
const uint16_t RlinkConnect::kRbaddr_RLID0;

const uint16_t RlinkConnect::kRLCNTL_M_AnEna;
const uint16_t RlinkConnect::kRLCNTL_M_AtoEna;
const uint16_t RlinkConnect::kRLCNTL_M_AtoVal;

const uint16_t RlinkConnect::kRLSTAT_V_LCmd;
const uint16_t RlinkConnect::kRLSTAT_B_LCmd;
const uint16_t RlinkConnect::kRLSTAT_M_BAbo;
const uint16_t RlinkConnect::kRLSTAT_M_RBSize;

const uint16_t RlinkConnect::kSBCNTL_V_RLMON;
const uint16_t RlinkConnect::kSBCNTL_V_RLBMON;
const uint16_t RlinkConnect::kSBCNTL_V_RBMON;  

//------------------------------------------+-----------------------------------
//! Default constructor

RlinkConnect::RlinkConnect()
  : fpPort(),
    fpServ(0),
    fSndPkt(),
    fRcvPkt(),
    fContext(),
    fAddrMap(),
    fStats(),
    fLogOpts(),
    fspLog(new RlogFile(&cout, "<cout>")),
    fConnectMutex(),
    fAttnNotiPatt(0),
    fTsLastAttnNoti(-1)
{
  for (size_t i=0; i<8; i++) fSeqNumber[i] = 0;
 
  // Statistic setup
  fStats.Define(kStatNExec,     "NExec",     "Exec() calls");
  fStats.Define(kStatNExecPart, "NExecPart", "ExecPart() calls");
  fStats.Define(kStatNCmd,      "NCmd",      "commands executed");
  fStats.Define(kStatNRreg,     "NRreg",     "rreg commands");
  fStats.Define(kStatNRblk,     "NRblk",     "rblk commands");
  fStats.Define(kStatNWreg,     "NWreg",     "wreg commands");
  fStats.Define(kStatNWblk,     "NWblk",     "wblk commands");
  fStats.Define(kStatNLabo,     "NLabo",     "labo commands");
  fStats.Define(kStatNAttn,     "NAttn",     "attn commands");
  fStats.Define(kStatNInit,     "NInit",     "init commands");
  fStats.Define(kStatNRblkWord, "NRblkWord", "words rcvd with rblk");
  fStats.Define(kStatNWblkWord, "NWblkWord", "words send with wblk");
  fStats.Define(kStatNTxPktByt, "NTxPktByt", "Tx packet bytes send");
  fStats.Define(kStatNRxPktByt, "NRxPktByt", "Rx packet bytes rcvd");
  fStats.Define(kStatNExpData,  "NExpData",  "Expect() for data defined");
  fStats.Define(kStatNExpStat,  "NExpStat",  "Expect() for stat defined");
  fStats.Define(kStatNChkData,  "NChkData",  "expect data failed");
  fStats.Define(kStatNChkStat,  "NChkStat",  "expect stat failed");
  fStats.Define(kStatNSndOob,   "NSndOob",   "SndOob() calls");
  fStats.Define(kStatNErrMiss,  "NErrMiss",  "decode: missing data");
  fStats.Define(kStatNErrCmd,   "NErrCmd",   "decode: command mismatch");
  fStats.Define(kStatNErrLen,   "NErrLen",   "decode: length mismatch");
  fStats.Define(kStatNErrCrc,   "NErrCrc",   "decode: crc mismatch");
}

//------------------------------------------+-----------------------------------
//! Destructor

RlinkConnect::~RlinkConnect()
{
  Close();
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::Open(const std::string& name, RerrMsg& emsg)
{
  Close();

  fpPort.reset(RlinkPortFactory::Open(name, emsg));
  if (!fpPort) return false;

  fpPort->SetLogFile(fspLog);
  fpPort->SetTraceLevel(fLogOpts.tracelevel);
  return true;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::Close()
{
  if (!fpPort) return;

  if (fpServ) fpServ->Stop();               // stop server in case still running

  if (fpPort->Url().FindOpt("keep")) {
    RerrMsg emsg;
    fSndPkt.SndKeep(fpPort.get(), emsg);
  }

  fpPort.reset();
    
  return;
}

//------------------------------------------+-----------------------------------
//! Indicates whether server is active.
/*!
  \returns \c true if server active.
 */


bool RlinkConnect::ServerActive() const
{
  return fpServ && fpServ->IsActive();
}

//------------------------------------------+-----------------------------------
//! Indicates whether server is active and caller is inside server thread.
/*!
  \returns \c true if server active and method is called from server thread.
 */

bool RlinkConnect::ServerActiveInside() const
{
  return fpServ && fpServ->IsActiveInside();
}

//------------------------------------------+-----------------------------------
//! Indicates whether server is active and caller is outside server thread.
/*!
  \returns \c true if server active and method is called from a thread
           other than the server thread.
 */

bool RlinkConnect::ServerActiveOutside() const
{
  return fpServ && fpServ->IsActiveOutside();
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::lock()
{
  fConnectMutex.lock();
  return;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::try_lock()
{
  return fConnectMutex.try_lock();
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::unlock()
{
  fConnectMutex.unlock();
  return;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::Exec(RlinkCommandList& clist, RlinkContext& cntx, 
                        RerrMsg& emsg)
{  
  if (clist.Size() == 0)
    throw Rexception("RlinkConnect::Exec()", "Bad state: clist empty");
  if (! IsOpen())
    throw Rexception("RlinkConnect::Exec()", "Bad state: port not open");

  boost::lock_guard<RlinkConnect> lock(*this);

  fStats.Inc(kStatNExec);

  clist.ClearLaboIndex();

  size_t size = clist.Size();

  for (size_t i=0; i<size; i++) {
    RlinkCommand& cmd = clist[i];
   if (!cmd.TestFlagAny(RlinkCommand::kFlagInit))
     throw Rexception("RlinkConnect::Exec()", 
                      "BugCheck: command not initialized");
    if (cmd.Command() > RlinkCommand::kCmdInit)
      throw Rexception("RlinkConnect::Exec()", 
                       "BugCheck: invalid command code");
    // trap attn command when server running and outside server thread
    if (cmd.Command() == RlinkCommand::kCmdAttn && ServerActiveOutside())
      throw Rexception("RlinkConnect::Exec()", 
                       "attn command not allowed outside active server");
    
    cmd.ClearFlagBit(RlinkCommand::kFlagSend   | 
                     RlinkCommand::kFlagDone   |
                     RlinkCommand::kFlagLabo   |
                     RlinkCommand::kFlagPktBeg | 
                     RlinkCommand::kFlagPktEnd |
                     RlinkCommand::kFlagErrNak | 
                     RlinkCommand::kFlagErrDec);
  }
  
  // old split volative logic. Currently dormant
  // may be later used for rtbuf size prot
#ifdef NEVER
  while (ibeg < size) {
    size_t iend = ibeg;
    for (size_t i=ibeg; i<size; i++) {
      iend = i;
      if (clist[i].TestFlagAll(RlinkCommand::kFlagVol)) {
        fStats.Inc(kStatNSplitVol);
        break;
      }
    }
    bool rc = ExecPart(clist, ibeg, iend, emsg, cntx);
    if (!rc) return rc;
    ibeg = iend+1;
  }
#endif

  bool rc = ExecPart(clist, 0, size-1, emsg, cntx);
  if (!rc) return rc;

  bool checkseen = false;
  bool errorseen = false;

  for (size_t i=0; i<size; i++) {
    RlinkCommand& cmd = clist[i];
    
    bool checkfound = cmd.TestFlagAny(RlinkCommand::kFlagChkStat | 
                                      RlinkCommand::kFlagChkData);
    bool errorfound = cmd.TestFlagAny(RlinkCommand::kFlagErrNak | 
                                      RlinkCommand::kFlagErrDec);
    checkseen |= checkfound;
    errorseen |= errorfound;
    if (checkfound | errorfound) cntx.IncErrorCount();
  }

  size_t loglevel = 3;
  if (checkseen) loglevel = 2;
  if (errorseen) loglevel = 1;
  if (loglevel <= fLogOpts.printlevel) {
    RlogMsg lmsg(*fspLog);
    clist.Print(lmsg(), cntx, &AddrMap(), fLogOpts.baseaddr, fLogOpts.basedata,
                fLogOpts.basestat);
  }
  if (loglevel <= fLogOpts.dumplevel) {
    RlogMsg lmsg(*fspLog);
    clist.Dump(lmsg(), 0);
  }
  
  return true;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::Exec(RlinkCommandList& clist, RlinkContext& cntx)
{
  RerrMsg emsg;
  bool rc = Exec(clist, cntx, emsg);
  if (!rc) {
    RlogMsg lmsg(*fspLog, 'E');
    lmsg << emsg << endl;
    lmsg << "Dump of failed clist:" << endl;
    clist.Dump(lmsg(), 0);    
  }
  return rc;
}

//------------------------------------------+-----------------------------------
//! Wait for an attention notify.
/*!
  First checks whether there are received and not yet harvested notifies.
  In that case the cummulative pattern of these pending notifies is returned
  in \a apat, and a 0. return value.

  If a positive \a timeout is specified the method waits this long for a
  valid and non-zero attention notify.

  \param      timeout  maximal time to wait for input in sec. Must be >= 0.
                       A zero \a timeout can be used to only harvest pending
                       notifies without waiting for new ones.
  \param[out] apat     cummulative attention pattern
  \param[out] emsg     contains error description (mainly from port layer)

  \returns wait time, or a negative value indicating an error:
    - =0.  if there was already a received and not yet harvested notify
    - >0   the wait time till the nofity was received
    - -1.  indicates timeout (\a apat will be 0)
    - -2.  indicates port IO error (\a emsg will contain information)

  \throws Rexception if called outside of an active server

  \pre ServerActiveOutside() must be \c false.

 */

double RlinkConnect::WaitAttn(double timeout, uint16_t& apat, RerrMsg& emsg)
{
  if (ServerActiveOutside())
    throw Rexception("RlinkConnect::WaitAttn()", 
                     "not allowed outside active server");

  apat = 0;

  boost::lock_guard<RlinkConnect> lock(*this);

  // harvest pending notifiers
  if (fAttnNotiPatt != 0) {
    apat = fAttnNotiPatt;
    fAttnNotiPatt = 0;
    return 0.;
  }

  // quit if poll only (zero timeout) 
  if (timeout == 0.) return -1.;

  // wait for new notifier
  double tnow = Rtools::TimeOfDayAsDouble();
  double tend = tnow + timeout;
  double tbeg = tnow;

  while (tnow < tend) {
    int irc = fRcvPkt.ReadData(fpPort.get(), tend-tnow, emsg);
    if (irc == RlinkPort::kTout) return -1.;
    if (irc == RlinkPort::kErr)  return -2.;
    tnow = Rtools::TimeOfDayAsDouble();    
    while (fRcvPkt.ProcessData()) {
      int irc = fRcvPkt.PacketState();
      if (irc == RlinkPacketBufRcv::kPktPend) break;
      if (irc == RlinkPacketBufRcv::kPktAttn) {
        ProcessAttnNotify();
        if (fAttnNotiPatt != 0) {
          apat = fAttnNotiPatt;
          fAttnNotiPatt = 0;
          return tnow - tbeg;
        }
      } else {
        RlogMsg lmsg(*fspLog, 'E');
        lmsg << "WaitAttn: dropped spurious packet";
        fRcvPkt.AcceptPacket();
      }

    } // while (fRcvPkt.ProcessData())
  } // while (tnow < tend)

  return -1;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::SndOob(uint16_t addr, uint16_t data, RerrMsg& emsg)
{
  boost::lock_guard<RlinkConnect> lock(*this);
  fStats.Inc(kStatNSndOob);
  return fSndPkt.SndOob(fpPort.get(), addr, data, emsg);
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::SndAttn(RerrMsg& emsg)
{
  boost::lock_guard<RlinkConnect> lock(*this);
  return fSndPkt.SndAttn(fpPort.get(), emsg);
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::SetLogOpts(const LogOpts& opts)
{
  if (opts.baseaddr!=2 && opts.baseaddr!=8 && opts.baseaddr!=16)
    throw Rexception("RlinkConnect::SetLogOpts()",
                     "Bad args: baseaddr != 2,8,16");
  if (opts.basedata!=2 && opts.basedata!=8 && opts.basedata!=16)
    throw Rexception("RlinkConnect::SetLogOpts()",
                     "Bad args: basedata != 2,8,16");
  if (opts.basestat!=2 && opts.basestat!=8 && opts.basestat!=16)
    throw Rexception("RlinkConnect::SetLogOpts()",
                     "Bad args: basestat != 2,8,16");

  fLogOpts = opts;
  if (fpPort) fpPort->SetTraceLevel(opts.tracelevel);
  return;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::LogOpen(const std::string& name)
{
  if (!fspLog->Open(name)) {
    fspLog->UseStream(&cout, "<cout>");
    return false;
  }
  return true;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::LogUseStream(std::ostream* pstr, const std::string& name)
{
  fspLog->UseStream(pstr, name);
  return;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::Print(std::ostream& os) const
{
  os << "RlinkConnect::Print(std::ostream& os)" << endl;
  return;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::Dump(std::ostream& os, int ind, const char* text) const
{
  RosFill bl(ind);
  os << bl << (text?text:"--") << "RlinkConnect @ " << this << endl;

  if (fpPort) {
    fpPort->Dump(os, ind+2, "fpPort: ");
  } else {
    os << bl << "  fpPort:          " <<  fpPort.get() << endl;
  }

  os << bl << "  fpServ:          " << fpServ << endl;
  os << bl << "  fSeqNumber:      ";
  for (size_t i=0; i<8; i++) os << RosPrintBvi(fSeqNumber[i],16) << " ";
  os << endl;
  
  fSndPkt.Dump(os, ind+2, "fSndPkt: ");
  fRcvPkt.Dump(os, ind+2, "fRcvPkt: ");
  fContext.Dump(os, ind+2, "fContext: ");
  fAddrMap.Dump(os, ind+2, "fAddrMap: ");
  fStats.Dump(os, ind+2, "fStats: ");
  os << bl << "  fLogOpts.baseaddr   " << fLogOpts.baseaddr << endl;
  os << bl << "          .basedata   " << fLogOpts.basedata << endl;
  os << bl << "          .basestat   " << fLogOpts.basestat << endl;
  os << bl << "          .printlevel " << fLogOpts.printlevel << endl;
  os << bl << "          .dumplevel  " << fLogOpts.dumplevel << endl;
  os << bl << "          .tracelevel " << fLogOpts.tracelevel << endl;
  fspLog->Dump(os, ind+2, "fspLog: ");
  os << bl << "  fAttnNotiPatt: " << RosPrintBvi(fAttnNotiPatt,16) << endl;
  //FIXME_code: fTsLastAttnNoti not yet in Dump (get formatter...)

  return;
}

//------------------------------------------+-----------------------------------
//! Handle unsolicited data from port.
/*!
  Called by RlinkServer to process unsolicited input data. Will read all
  pending data from input port and process it with ProcessUnsolicitedData().

  \throws Rexception if not called from inside of an active server

  \pre ServerActiveInside() must be \c true.
 */

void RlinkConnect::HandleUnsolicitedData()
{
  if (!ServerActiveInside())
    throw Rexception("RlinkConnect::HandleUnsolicitedData()", 
                     "only allowed inside active server");

  boost::lock_guard<RlinkConnect> lock(*this);
  RerrMsg emsg;
  int irc = fRcvPkt.ReadData(fpPort.get(), 0., emsg);
  if (irc == 0) return;
  if (irc < 0) {
    RlogMsg lmsg(*fspLog, 'E');
    lmsg << "HandleUnsolicitedData: IO error: " << emsg;
  }
  ProcessUnsolicitedData();
  return;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

bool RlinkConnect::ExecPart(RlinkCommandList& clist, size_t ibeg, size_t iend,
                            RerrMsg& emsg, RlinkContext& cntx)
{  
  if (ibeg>iend || iend>=clist.Size())
    throw Rexception("RlinkConnect::ExecPart()",
                     "Bad args: ibeg or iend invalid");
  if (!IsOpen())
    throw Rexception("RlinkConnect::ExecPart()","Bad state: port not open");

  fStats.Inc(kStatNExecPart);
  EncodeRequest(clist, ibeg, iend);

  // FIXME_code: handle send fail properly;
  if (!fSndPkt.SndPacket(fpPort.get(), emsg)) return false;
  fStats.Inc(kStatNTxPktByt, double(fSndPkt.PktSize()));

  // FIXME_code: handle recoveries
  // FIXME_code: use proper value for timeout
  bool ok = ReadResponse(15., emsg);
  if (!ok) Rexception("RlinkConnect::ExecPart()","faulty response");

  int ncmd = DecodeResponse(clist, ibeg, iend, cntx);
  if (ncmd != int(iend-ibeg+1)) {
    clist.Dump(cout);
    throw Rexception("RlinkConnect::ExecPart()","incomplete response");
  }

  AcceptResponse();

  return true;
}

//------------------------------------------+-----------------------------------
//! FIXME_docs

void RlinkConnect::EncodeRequest(RlinkCommandList& clist, size_t ibeg, 
                                 size_t iend)
{
  fSndPkt.Init();

  for (size_t i=ibeg; i<=iend; i++) {
    RlinkCommand& cmd = clist[i];
    uint8_t   ccode = cmd.Command();
    size_t    ndata = cmd.BlockSize();
    uint16_t* pdata = cmd.BlockPointer();

    fStats.Inc(kStatNCmd);

    cmd.SetSeqNumber(fSeqNumber[ccode]++);
    cmd.ClearFlagBit(RlinkCommand::kFlagPktBeg | RlinkCommand::kFlagPktEnd);

    fSndPkt.PutWithCrc(cmd.Request());

    switch (ccode) {
      case RlinkCommand::kCmdRreg:          // rreg command ---------------
        fStats.Inc(kStatNRreg);
        cmd.SetRcvSize(1+2+1+2);            // rcv: cmd+data+stat+crc
        fSndPkt.PutWithCrc(cmd.Address());
        break;

      case RlinkCommand::kCmdRblk:          // rblk command ---------------
        fStats.Inc(kStatNRblk);
        fStats.Inc(kStatNRblkWord, (double) ndata);
        cmd.SetRcvSize(1+2+2*ndata+2+1+2); // rcv: cmd+cnt+n*data+dcnt+stat+crc
        fSndPkt.PutWithCrc(cmd.Address());
        fSndPkt.PutWithCrc((uint16_t)ndata);
        break;

      case RlinkCommand::kCmdWreg:          // wreg command ---------------
        fStats.Inc(kStatNWreg);
        cmd.SetRcvSize(1+1+2);              // rcv: cmd+stat+crc
        fSndPkt.PutWithCrc(cmd.Address());
        fSndPkt.PutWithCrc(cmd.Data());
        break;

      case RlinkCommand::kCmdWblk:          // wblk command ---------------
        fStats.Inc(kStatNWblk);
        fStats.Inc(kStatNWblkWord, (double) ndata);
        cmd.SetRcvSize(1+2+1+2);              // rcv: cmd+dcnt+stat+crc
        fSndPkt.PutWithCrc(cmd.Address());
        fSndPkt.PutWithCrc((uint16_t)ndata);
        fSndPkt.PutCrc();
        fSndPkt.PutWithCrc(pdata, ndata);
        break;

      case RlinkCommand::kCmdLabo:          // labo command ---------------
        fStats.Inc(kStatNLabo);
        cmd.SetRcvSize(1+1+1+2);            // rcv: cmd+babo+stat+crc
        break;
      case RlinkCommand::kCmdAttn:          // attn command ---------------
        fStats.Inc(kStatNAttn);
        cmd.SetRcvSize(1+2+1+2);            // rcv: cmd+data+stat+crc
        break;

      case RlinkCommand::kCmdInit:          // init command ---------------
        fStats.Inc(kStatNInit);
        cmd.SetRcvSize(1+1+2);              // rcv: cmd+stat+crc
        fSndPkt.PutWithCrc(cmd.Address());
        fSndPkt.PutWithCrc(cmd.Data());
        break;

      default:
        throw Rexception("RlinkConnect::Exec()", "BugCheck: invalid command");
    } // switch (ccode)

    fSndPkt.PutCrc();
    cmd.SetFlagBit(RlinkCommand::kFlagSend);
  } // for (size_t i=ibeg; i<=iend; i++)

  // FIXME_code: do we still need kFlagPktBeg,kFlagPktEnd ?
  clist[ibeg].SetFlagBit(RlinkCommand::kFlagPktBeg);
  clist[iend].SetFlagBit(RlinkCommand::kFlagPktEnd);

  return;
}
  
//------------------------------------------+-----------------------------------
//! FIXME_docs

int RlinkConnect::DecodeResponse(RlinkCommandList& clist, size_t ibeg, 
                                 size_t iend, RlinkContext& cntx)
{
  size_t ncmd = 0;
  
  for (size_t i=ibeg; i<=iend; i++) {
    RlinkCommand& cmd = clist[i];
    uint8_t   ccode = cmd.Command();
    uint16_t  rdata;
    uint8_t   rdata8;

    // handle commands after an active labo
    if (clist.LaboActive()) {
      ncmd += 1;
      cmd.SetFlagBit(RlinkCommand::kFlagDone|RlinkCommand::kFlagLabo);
      continue;
    }

    // FIXME_code: handle NAK properly !!

    if (!fRcvPkt.CheckSize(cmd.RcvSize())) {   // not enough data for cmd
      cmd.SetFlagBit(RlinkCommand::kFlagErrDec);
      fStats.Inc(kStatNErrMiss);
      RlogMsg lmsg(*fspLog, 'E');
      lmsg << "DecodeResponse: not enough data for cmd";
      return -1;
    }
    
    fRcvPkt.GetWithCrc(rdata8);
    if (rdata8 != cmd.Request()) { // command mismatch
      cmd.SetFlagBit(RlinkCommand::kFlagErrDec);
      fStats.Inc(kStatNErrCmd);
      RlogMsg lmsg(*fspLog, 'E');
      lmsg << "DecodeResponse: command mismatch";
      return -1;
    }

    switch (ccode) {
      case RlinkCommand::kCmdRreg:          // rreg command ---------------
        fRcvPkt.GetWithCrc(rdata);
        cmd.SetData(rdata);
        break;

      case RlinkCommand::kCmdRblk:          // rblk command ---------------
        fRcvPkt.GetWithCrc(rdata);
        if (rdata != (uint16_t)cmd.BlockSize()) {    // length mismatch
          cmd.SetFlagBit(RlinkCommand::kFlagErrDec);
          fStats.Inc(kStatNErrLen);
          RlogMsg lmsg(*fspLog, 'E');
          lmsg << "DecodeResponse: rblk length mismatch";
          return -1;
        }
        fRcvPkt.GetWithCrc(cmd.BlockPointer(), cmd.BlockSize());
        fRcvPkt.GetWithCrc(rdata);
        cmd.SetBlockDone(rdata);
       break;

      case RlinkCommand::kCmdWreg:          // wreg command ---------------
        break;

      case RlinkCommand::kCmdWblk:          // wblk command ---------------
        fRcvPkt.GetWithCrc(rdata);
        cmd.SetBlockDone(rdata);
       break;

      case RlinkCommand::kCmdLabo:          // labo command ---------------
        fRcvPkt.GetWithCrc(rdata8);
        cmd.SetData((uint16_t)rdata8);
        break;

      case RlinkCommand::kCmdAttn:          // attn command ---------------
        fRcvPkt.GetWithCrc(rdata);
        cmd.SetData(rdata);
        break;

      case RlinkCommand::kCmdInit:          // init command ---------------
        break;
    } // switch (ccode)

    // crc handling
    fRcvPkt.GetWithCrc(rdata8);
    cmd.SetStatus(rdata8);
    if (!fRcvPkt.CheckCrc()) {              // crc mismatch
      cmd.SetFlagBit(RlinkCommand::kFlagErrDec);
      fStats.Inc(kStatNErrCrc);
      RlogMsg lmsg(*fspLog, 'E');
      lmsg << "DecodeResponse: crc mismatch";
      return -1;
    }

    ncmd += 1;
    cmd.SetFlagBit(RlinkCommand::kFlagDone);

    // handle active labo command, here we know that crc is ok
    if (ccode==RlinkCommand::kCmdLabo && cmd.Data()) {    // labo active ?
      clist.SetLaboIndex(i);                  // set index
    }

    // expect handling
    if (cmd.Expect()) {                     // expect object attached ?
      RlinkCommandExpect& expect = *cmd.Expect();
      if (expect.DataIsChecked() || 
          expect.BlockValue().size()>0) fStats.Inc(kStatNExpData);
      if (expect.StatusIsChecked())     fStats.Inc(kStatNExpStat);

      if (ccode==RlinkCommand::kCmdRreg || 
          ccode==RlinkCommand::kCmdLabo ||
          ccode==RlinkCommand::kCmdAttn) {
        if (!expect.DataCheck(cmd.Data())) {
          fStats.Inc(kStatNChkData);
          cmd.SetFlagBit(RlinkCommand::kFlagChkData);
        }
      } else if (ccode==RlinkCommand::kCmdRblk) {
        size_t nerr = expect.BlockCheck(cmd.BlockPointer(), cmd.BlockSize());
        if (nerr != 0) {
          fStats.Inc(kStatNChkData);
          cmd.SetFlagBit(RlinkCommand::kFlagChkData);
        }
      }
      if (!expect.StatusCheck(cmd.Status())) {
        fStats.Inc(kStatNChkStat);
        cmd.SetFlagBit(RlinkCommand::kFlagChkStat);
      }

    } else {                                // no expect, use context
      if (!cntx.StatusCheck(cmd.Status())) {
        fStats.Inc(kStatNChkStat);
        cmd.SetFlagBit(RlinkCommand::kFlagChkStat);
      }
    } // if (cmd.Expect())

  } // for (size_t i=ibeg; i<=iend; i++)

  // FIXME_code: check that all data is consumed !!

  return ncmd;
}

//------------------------------------------+-----------------------------------
//! Decodes an attention notify packet.
/*!
  \param[out]  apat  attention pattern, can be zero
  \returns  \c true if decode without errors, \c false otherwise
 */

bool RlinkConnect::DecodeAttnNotify(uint16_t& apat)
{
  apat = 0;
  
  if (!fRcvPkt.CheckSize(2+2)) {            // not enough data for data+crc
    fStats.Inc(kStatNErrMiss); 
    RlogMsg lmsg(*fspLog, 'E');
    lmsg << "DecodeAttnNotify: not enough data for data+crc";
    return false;
  }

  fRcvPkt.GetWithCrc(apat);

  if (!fRcvPkt.CheckCrc()) {                // crc mismatch
    fStats.Inc(kStatNErrCrc);
    RlogMsg lmsg(*fspLog, 'E');
    lmsg << "DecodeAttnNotify: crc mismatch";
    return false;
  }

  // FIXME_code: check for extra data

  return true;
}

//------------------------------------------+-----------------------------------
//! Read data from port until complete response packet seen.
/*!
  Any spurious data or corrupt packages, e.g. with framing errors, 
  are logged and discarded.

  If an attention notify packet is detected it will handled with
  ProcessAttnNotify().

  The method returns \c true if a complete response packet is received.
  The caller will usually use DecodeResponse() and must accept the packet
  with AcceptResponse() afterwards.

  The method returns \c false if
    - no valid response packet is seen within the time given by \a timeout
    - an IO error occurred
    .
  An appropriate log message is generated, any partial input packet discarded.

  \param      timeout  maximal time to wait for input in sec. Must be > 0.
  \param[out] emsg     contains error description (mainly from port layer)

  \returns \c true if complete response packet received

  \pre a previous response must have been accepted with AcceptResponse().
 */ 

bool RlinkConnect::ReadResponse(double timeout, RerrMsg& emsg)
{
  double tnow = Rtools::TimeOfDayAsDouble();
  double tend = tnow + timeout;

  while (tnow < tend) {
    int irc = fRcvPkt.ReadData(fpPort.get(), tend-tnow, emsg);
    if (irc <= 0) {
      RlogMsg lmsg(*fspLog, 'E');
      lmsg << "ReadResponse: IO error or timeout: " << emsg;
      return false;
    }

    while (fRcvPkt.ProcessData()) {
      int irc = fRcvPkt.PacketState();
      if (irc == RlinkPacketBufRcv::kPktPend) break;
      if (irc == RlinkPacketBufRcv::kPktAttn) {
        ProcessAttnNotify();
      } else if (irc == RlinkPacketBufRcv::kPktResp) {
        return true;
      } else {
        RlogMsg lmsg(*fspLog, 'E');
        lmsg << "ReadResponse: dropped spurious packet";
        fRcvPkt.AcceptPacket();
      }
    } //while (fRcvPkt.ProcessData())

    tnow = Rtools::TimeOfDayAsDouble();

  } // while (tnow < tend)

  { 
    RlogMsg lmsg(*fspLog, 'E');
    lmsg << "ReadResponse: timeout";
  }
  fRcvPkt.AcceptPacket();

  return false;
}

//------------------------------------------+-----------------------------------
//! Accept response packet received with ReadResponse().
/*!
  The packet buffer is cleared, and any still buffered input data is processed
  with ProcessUnsolicitedData(). 
 */

void RlinkConnect::AcceptResponse()
{
  fRcvPkt.AcceptPacket();
  ProcessUnsolicitedData();
  return;
}
  
//------------------------------------------+-----------------------------------
//! Process data still pending in the input buffer.
/*!
  If an attention notify packet is detected it will handled with
  ProcessAttnNotify(). If a response or corrupted packet is seen
  it will be logged and discarded.
 */

void RlinkConnect::ProcessUnsolicitedData()
{
  while (fRcvPkt.ProcessData()) {
    int irc = fRcvPkt.PacketState();
    if (irc == RlinkPacketBufRcv::kPktPend) break;
    if (irc == RlinkPacketBufRcv::kPktAttn) {
      ProcessAttnNotify();
    } else {
      fRcvPkt.AcceptPacket();
      RlogMsg lmsg(*fspLog, 'E');
      lmsg << "ProcessUnsolicitedData: dropped spurious packet";
    }
  }  
  return;
}

//------------------------------------------+-----------------------------------
//! Process attention notify packets.
/*!
  The packets is decoded with DecodeAttnNotify(). If the packet is valid and 
  contains a non-zero attention pattern the pattern is ored to the attention 
  notify pattern which can later be inquired with HarvestAttnNotifies().
  Corrupt packets are logged and discarded. Notifies with a zero pattern
  are silently ignored.
 */

void RlinkConnect::ProcessAttnNotify()
{
  uint16_t apat;
  bool ok = DecodeAttnNotify(apat);
  fRcvPkt.AcceptPacket();

  if (ok) {
    if (apat) {
      if (ServerActive()) {                   // if server active
        fpServ->SignalAttnNotify(apat);       //   handle in RlinkServer
      } else {                                // otherwise
        fAttnNotiPatt |= apat;                //   handle in RlinkConnect
      }
    } else {
      RlogMsg lmsg(*fspLog, 'W');
      lmsg << "ProcessAttnNotify: zero attn notify received";
    }
  }

  if (ok && fLogOpts.printlevel == 3) {
     RlogMsg lmsg(*fspLog, 'I');
     lmsg << "ATTN notify apat = " << RosPrintf(apat,"x0",4)
          << "  lams =";
     if (apat) {
       char sep = ' ';
       for (int i=15; i>=0; i--) {
         if (apat & (uint16_t(1)<<i) ) {
           lmsg << sep << i;
           sep = ',';
         }
       }
     } else {
       lmsg << " !NONE!";
     }
     double now = Rtools::TimeOfDayAsDouble();
     if (fTsLastAttnNoti > 0.) 
       lmsg << "  dt=" << RosPrintf(now-fTsLastAttnNoti,"f",8,6);
     fTsLastAttnNoti = now;
  }
  return;
}


} // end namespace Retro
