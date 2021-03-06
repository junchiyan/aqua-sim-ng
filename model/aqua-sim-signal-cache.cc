/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 University of Connecticut
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Robert Martin <robert.martin@engr.uconn.edu>
 */

//#include ...
#include "aqua-sim-signal-cache.h"
#include "aqua-sim-header.h"

#include <queue>

#include "aqua-sim-phy.h"
#include "ns3/simulator.h"
#include "ns3/log.h"


//Aqua Sim Signal Cache

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("AquaSimSignalCache");
NS_OBJECT_ENSURE_REGISTERED(PktSubmissionTimer);


PktSubmissionTimer::PktSubmissionTimer(Ptr<AquaSimSignalCache> sC)
{
  m_sC = sC;
}

PktSubmissionTimer::~PktSubmissionTimer()
{
  NS_LOG_FUNCTION(this);
  m_sC=0;
}

TypeId
PktSubmissionTimer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PktSubmissionTimer")
    .SetParent<Object>()
  ;
  return tid;
}

void
PktSubmissionTimer::Expire(Ptr<IncomingPacket> inPkt)
{
  /*Ptr<IncomingPacket> inPkt = m_waitingList.top().inPkt;
  m_waitingList.pop();
  if(!m_waitingList.empty())
  {
    Simulator::Schedule(m_waitingList.top().endT, &PktSubmissionTimer::Expire, this);
  }
  */

  NS_LOG_DEBUG("Expire. time:" << Simulator::Now().ToDouble(Time::S) << " inPkt:" << inPkt);
  m_sC->SubmitPkt(inPkt);
}

void
PktSubmissionTimer::AddNewSubmission(Ptr<IncomingPacket> inPkt) {
  AquaSimHeader asHeader;
  (inPkt->packet)->PeekHeader(asHeader);
  /*Time transmissionDelay = Seconds(inPkt->packet->GetSize() * 8 *      //Byte conversion/
                           m_sC->m_phy->GetMac()->GetEncodingEff() /
                           m_sC->m_phy->GetMac()->GetBitRate() ); */

  /* Need to calcuate modulation here, aka how long until entire packet is received */
  Time transmissionDelay = m_sC->m_phy->CalcTxTime((inPkt->packet)->GetSize());

  NS_LOG_FUNCTION(this << "incomingPkt:" << inPkt << "txtime:" <<
                    asHeader.GetTxTime() << " transmissionDelay:" <<
                    transmissionDelay.ToDouble(Time::S));

  Simulator::Schedule(transmissionDelay,&PktSubmissionTimer::Expire, this, inPkt);

  /*if (m_waitingList.empty() || m_waitingList.top().endT > transmissionDelay)
  {
      Simulator::Schedule(transmissionDelay, &PktSubmissionTimer::Expire, this);
  }
  m_waitingList.push(PktSubmissionUnit(inPkt, transmissionDelay));
  */
}


PktSubmissionUnit::PktSubmissionUnit(Ptr<IncomingPacket> inPkt_, Time endT_)
	: inPkt(inPkt_), endT(endT_)
{
}

NS_OBJECT_ENSURE_REGISTERED(AquaSimSignalCache);

AquaSimSignalCache::AquaSimSignalCache() :
m_pktNum(0), m_totalPS(0.0), m_pktSubTimer(NULL)
{
  NS_LOG_FUNCTION(this);

  m_head = CreateObject<IncomingPacket>(AquaSimPacketStamp::INVALID);
  m_pktSubTimer = new PktSubmissionTimer(this);
  status = AquaSimPacketStamp::INVALID;
}

AquaSimSignalCache::~AquaSimSignalCache()
{
}

TypeId
AquaSimSignalCache::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::AquaSimSignalCache")
    .SetParent<Object> ();
  ;
  return tid;
}

void
AquaSimSignalCache::AddNewPacket(Ptr<Packet> p){
  /**
  * any packet error marked before this step means
  * this packet is invalid and will be considered
  * as noise to other packets only.
  */
  // TODO is packet collision even really tested or dealt with in this class???
  AquaSimHeader asHeader;
  p->PeekHeader(asHeader);

  Ptr<IncomingPacket> inPkt = CreateObject<IncomingPacket>(p,
		  asHeader.GetErrorFlag() ? AquaSimPacketStamp::INVALID : AquaSimPacketStamp::RECEPTION);

  NS_LOG_DEBUG("AddNewPacket:" << p << " w/ Error flag:" << asHeader.GetErrorFlag() << " and incomingpkt:" << inPkt);


  m_pktSubTimer->AddNewSubmission(inPkt);

  inPkt->next = m_head->next;
  m_head->next = inPkt;

  m_pktNum++;
  m_totalPS += m_phy->EM()->GetRxPower();
  UpdatePacketStatus();
}


bool
AquaSimSignalCache::DeleteIncomingPacket(Ptr<Packet> p){
  NS_LOG_FUNCTION(this);
  Ptr<IncomingPacket> pre = m_head;
  Ptr<IncomingPacket> ptr = m_head->next;

  while (ptr != NULL && ptr->packet != p) {
    ptr = ptr->next;
    pre = pre->next;
  }

  if (ptr != NULL && ptr->packet == p) {
    m_pktNum--;
    m_totalPS -= m_phy->EM()->GetRxPower();
    pre->next = ptr->next;
    ptr = 0;
    return true;
  }
  NS_LOG_DEBUG("DeleteIncomingPacket: ptr:" << ptr << "ptr(packet) == p?" << (ptr->packet != p));
  return false;
}


void
AquaSimSignalCache::SubmitPkt(Ptr<IncomingPacket> inPkt) {
  NS_LOG_FUNCTION(this << inPkt << inPkt->status);

  status = inPkt->status;
  Ptr<Packet> p = inPkt->packet;
  DeleteIncomingPacket(p); //object pointed by inPkt is deleted here
  /**
  * modem has no idea about invalid packets, so release
  * them here
  */
  if (status == AquaSimPacketStamp::INVALID)
  {
    NS_LOG_DEBUG("Packet(" << p << ") dropped");
    p = 0;
  }
  else
    m_phy->SignalCacheCallback(p);
}


Ptr<IncomingPacket>
AquaSimSignalCache::Lookup(Ptr<Packet> p){
  NS_LOG_FUNCTION(this);
  Ptr<IncomingPacket> ptr = m_head->next;

  while (ptr != NULL && ptr->packet != PeekPointer(p)) {
    ptr = ptr->next;
  }
  return ptr;
}


void
AquaSimSignalCache::InvalidateIncomingPacket(){
  NS_LOG_FUNCTION(this);
  Ptr<IncomingPacket> ptr = m_head->next;

  while (ptr != NULL) {
    ptr->status = AquaSimPacketStamp::INVALID;
    ptr = ptr->next;
  }
}


AquaSimPacketStamp::PacketStatus
AquaSimSignalCache::Status(Ptr<Packet> p){
  NS_LOG_FUNCTION(this);
  Ptr<IncomingPacket> ptr = Lookup(p);

  return ptr == NULL ? AquaSimPacketStamp::INVALID : ptr->status;
}


void
AquaSimSignalCache::UpdatePacketStatus(){
  NS_LOG_FUNCTION(this);

  double noise = 0,		//total noise
	 ps = 0;		//power strength
	//,SINR = 0; 		//currently not used

  for (Ptr<IncomingPacket> ptr = m_head->next; ptr != NULL; ptr = ptr->next) {
    ps = m_phy->EM()->GetRxPower();
    noise = m_totalPS - ps;

    if (ptr->status != AquaSimPacketStamp::RECEPTION)
      continue;

    ptr->status = m_phy->Decodable(noise + m_noise->Noise(), ps) ? AquaSimPacketStamp::RECEPTION : AquaSimPacketStamp::INVALID;
  }
}

void
AquaSimSignalCache::SetNoiseGen(Ptr<AquaSimNoiseGen> noise)
{
  NS_LOG_FUNCTION(this);
  m_noise = noise;
}

double AquaSimSignalCache::GetNoise()
{
  return m_totalPS + m_noise->Noise();
}

void AquaSimSignalCache::DoDispose()
{
  NS_LOG_FUNCTION(this);
//  Ptr<IncomingPacket> m_head;
//  PktSubmissionTimer* m_pktSubTimer;

  Ptr<IncomingPacket> pos = m_head;
  while (m_head != NULL) {
    m_head = m_head->next;
    pos->packet = 0;
    pos = 0;
    pos = m_head;
  }

  delete m_pktSubTimer;
  m_pktSubTimer = 0;
  m_phy=0;
  m_noise=0;
  Object::DoDispose();
}

};  // namespace ns3
