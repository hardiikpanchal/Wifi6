/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2020 Universita' degli Studi di Napoli Federico II
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
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "ns3/log.h"
#include "ns3/qos-utils.h"
#include "ns3/string.h"
#include "ns3/rr-multi-user-scheduler.h"
#include "ns3/wifi-protection.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/wifi-psdu.h"
#include "ns3/he-frame-exchange-manager.h"
#include "ns3/he-configuration.h"
#include "ns3/he-phy.h"
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RrMultiUserScheduler");

NS_OBJECT_ENSURE_REGISTERED (RrMultiUserScheduler);
uint32_t ru_26 = 0;
uint32_t ru_52 = 0;
int formatcnt=0;
uint32_t ru_106 = 0;
uint32_t ru_242 = 0;
uint32_t UL_cycle_count = 0;
 uint64_t Notxcnt=0,DLtxcnt=0,ULtxcnt=0;
 bool startThrougputcalc = false;
 bool flag = true;
 int ulstop_count = 0;
 int tcount=0;
  


Ptr<WifiMacQueueItem> mpdu_stored;

int computedlinfocount = 0;
MultiUserScheduler::DlMuInfo dlmuinfo_stored;

TypeId
RrMultiUserScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RrMultiUserScheduler")
    .SetParent<MultiUserScheduler> ()
    .SetGroupName ("Wifi")
    .AddConstructor<RrMultiUserScheduler> ()
    .AddAttribute ("NStations",
                   "The maximum number of stations that can be granted an RU in a DL MU OFDMA transmission",
                   UintegerValue (4),
                   MakeUintegerAccessor (&RrMultiUserScheduler::m_nStations),
                   MakeUintegerChecker<uint8_t> (1, 74))
    .AddAttribute ("EnableTxopSharing",
                   "If enabled, allow A-MPDUs of different TIDs in a DL MU PPDU.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RrMultiUserScheduler::m_enableTxopSharing),
                   MakeBooleanChecker ())
    .AddAttribute ("ForceDlOfdma",
                   "If enabled, return DL_MU_TX even if no DL MU PPDU could be built.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RrMultiUserScheduler::m_forceDlOfdma),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableUlOfdma",
                   "If enabled, return UL_MU_TX if DL_MU_TX was returned the previous time.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RrMultiUserScheduler::m_enableUlOfdma),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableBsrp",
                   "If enabled, send a BSRP Trigger Frame before an UL MU transmission.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RrMultiUserScheduler::m_enableBsrp),
                   MakeBooleanChecker ())
    .AddAttribute ("UlPsduSize",
                   "The default size in bytes of the solicited PSDU (to be sent in a TB PPDU)",
                   UintegerValue (500),
                   MakeUintegerAccessor (&RrMultiUserScheduler::m_ulPsduSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("UseCentral26TonesRus",
                   "If enabled, central 26-tone RUs are allocated, too, when the "
                   "selected RU type is at least 52 tones.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RrMultiUserScheduler::m_useCentral26TonesRus),
                   MakeBooleanChecker ())
    .AddAttribute ("MaxCredits",
                   "Maximum amount of credits a station can have. When transmitting a DL MU PPDU, "
                   "the amount of credits received by each station equals the TX duration (in "
                   "microseconds) divided by the total number of stations. Stations that are the "
                   "recipient of the DL MU PPDU have to pay a number of credits equal to the TX "
                   "duration (in microseconds) times the allocated bandwidth share",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&RrMultiUserScheduler::m_maxCredits),
                   MakeTimeChecker ())
    .AddAttribute ("SchedulerLogic",
                   "Standard or Bellalta",
                   StringValue ("Standard"),
                   MakeStringAccessor (&RrMultiUserScheduler::m_schedulerLogic),
                   MakeStringChecker ())
    .AddAttribute ("MCS_mode",
                   "HeMcs0-HeMcs11",
                   StringValue ("HeMcs5"),
                   MakeStringAccessor (&RrMultiUserScheduler::m_MCS),
                   MakeStringChecker ())
  ;
  return tid;
}

RrMultiUserScheduler::RrMultiUserScheduler ()
  : m_ulTriggerType (TriggerFrameType::BASIC_TRIGGER)
{
  NS_LOG_FUNCTION (this);
}

RrMultiUserScheduler::~RrMultiUserScheduler ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void
RrMultiUserScheduler::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_apMac != nullptr);
  m_apMac->TraceConnectWithoutContext ("AssociatedSta",
                                       MakeCallback (&RrMultiUserScheduler::NotifyStationAssociated, this));
  m_apMac->TraceConnectWithoutContext ("DeAssociatedSta",
                                       MakeCallback (&RrMultiUserScheduler::NotifyStationDeassociated, this));
  m_staList = {{AC_BE, std::list<MasterInfo> ()}, {AC_VI, std::list<MasterInfo> ()}, {AC_VO, std::list<MasterInfo> ()},
  {AC_BK, std::list<MasterInfo> ()}};
  // if (m_staList.empty())
  //   {
  //     AcIndex primaryAc = m_edca->GetAccessCategory ();
  //     m_staList.at(primaryAc) = {MasterInfo {aid, address, 0.0}};
  //   }
 
  MultiUserScheduler::DoInitialize ();
}
int count=10;

bool bsrp_limit = false;
bool dl_limit=false;
int txcnt=0;
void
RrMultiUserScheduler::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_staList.clear ();
  m_candidates.clear ();
  m_allcandidates.clear();
 // addedAllStations=false;
  m_trigger = nullptr;
  m_txParams.Clear ();
  m_apMac->TraceDisconnectWithoutContext ("AssociatedSta",
                                          MakeCallback (&RrMultiUserScheduler::NotifyStationAssociated, this));
  m_apMac->TraceDisconnectWithoutContext ("DeAssociatedSta",
                                          MakeCallback (&RrMultiUserScheduler::NotifyStationDeassociated, this));
  MultiUserScheduler::DoDispose ();
  std::cout<<"No Transmission count"<< Notxcnt<<"\n";
  std::cout<<"DL Transmission count"<< DLtxcnt<<"\n";
   std::cout<<"UL Transmission count"<< ULtxcnt<<"\n";
  std::cout<<"Select Tx called count"<< formatcnt<<"\n";
  std::cout<<"Select Tx called damn count"<< txcnt<<"\n";
  
  
}
int dlppducalledinselecttx = 0;
int dlppducalledatendselecttx = 0;
MultiUserScheduler::TxFormat
RrMultiUserScheduler::SelectTxFormat (void)
{
  formatcnt++;
  if(GetLastTxFormat()!=NO_TX)txcnt++;

  NS_LOG_FUNCTION (this);
  std::cout << "At time " << Simulator::Now().GetMicroSeconds () <<" SelectTxFormat function called for" << (GetLastTxFormat()==DL_MU_TX ? "DL":"UL") <<'\n';
  
  if (m_enableUlOfdma && m_enableBsrp && GetLastTxFormat () == DL_MU_TX)
    {
      if(bsrp_limit){
      count--;
      std::cout << "count value: " << count << '\n';
      
      if(count <= 0){
        count = 10;
        return TrySendingBsrpTf ();
      }

      }
     
      else{
        return TrySendingBsrpTf ();
      }
    }

  if (m_enableUlOfdma && (GetLastTxFormat () == DL_MU_TX
                          || m_ulTriggerType == TriggerFrameType::BSRP_TRIGGER))
    {
      TxFormat txFormat = TrySendingBasicTf ();
std::cout << "At time " << Simulator::Now().GetMicroSeconds () <<" SelectTxFormat function called again" << (GetLastTxFormat()==DL_MU_TX ? "DL":"UL") <<'\n';
  
      if (txFormat != DL_MU_TX)
        {
          return txFormat;
        }
    }
    
    // if(txcnt<=200 || txcnt%10000==0){
    //   dlppducalledinselecttx++;
    //   std::cout <<"DL ppdu called inside selecttx" <<"\n";
    
    //   return TrySendingDlMuPpdu ();
    // }
    // else {
    //   if(m_enableBsrp){
    //     return TrySendingBsrpTf();
    //   } 
    //   else{
    //     return TrySendingBasicTf();
    //   }

    // }


    dlppducalledatendselecttx++;
    std::cout <<"DL ppdu return at the end of selecttx" <<"\n";
    return TrySendingDlMuPpdu ();
}

int bsrp_call_count = 0;

// auto trigger_stored;

// if(count )

MultiUserScheduler::TxFormat

RrMultiUserScheduler::TrySendingBsrpTf (void)
{
  NS_LOG_FUNCTION (this);
  std::cout << "At time "<<Simulator::Now().GetMicroSeconds () << " TrySendingBsrpTf function called"<<"\n";

  if(computedlinfocount > 40 && m_enableBsrp && !flag && false){



//   m_ul_candidates.clear();
//   AcIndex primaryAc = m_edca->GetAccessCategory ();
//   std::cout << "stations for ul:"<< m_staList[primaryAc].size()<<"\n";
//   auto staIt = m_staList[primaryAc].begin ();  
//   while (staIt != m_staList[primaryAc].end ())
//     { 
//       uint8_t tid=0;
//       while (tid < 8){
//         AcIndex ac = QosUtilsMapTidToAc (tid);
//         Ptr<const WifiMacQueueItem> mpdu_dummy;    
//         if (m_apMac->GetQosTxop (ac)->GetBaAgreementEstablished (staIt->address, tid)){
//             m_ul_candidates.push_back({staIt, mpdu_dummy}); 
//             break;    // terminate the for loop
//         }            
//         tid++;
//       }      
//       staIt++;
//     }



//    // UlMuInfo {m_trigger, m_tbPpduDuration, std::move (m_txParams)}
//   m_txParams.Clear ();
//   m_txParams.m_txVector.SetPreambleType (WIFI_PREAMBLE_HE_MU); // TB?
//   m_txParams.m_txVector.SetChannelWidth (m_apMac->GetWifiPhy ()->GetChannelWidth ());
//   m_txParams.m_txVector.SetGuardInterval (m_apMac->GetHeConfiguration ()->GetGuardInterval ().GetNanoSeconds ());

//   auto candidateIt = m_ul_candidates.begin (); // iterator over the list of candidate receivers

// DlMuInfo dlMuInfo;
// dlMuInfo.txParams.m_txVector.SetPreambleType (m_txParams.m_txVector.GetPreambleType ());
//   dlMuInfo.txParams.m_txVector.SetChannelWidth (m_txParams.m_txVector.GetChannelWidth ());
//   dlMuInfo.txParams.m_txVector.SetGuardInterval (m_txParams.m_txVector.GetGuardInterval ());
// std::size_t nRusAssigneds =m_ul_candidates.size();
// std::cout << "m_ul_candidates size in new code ffff"<<nRusAssigneds<<"\n";

//   for (std::size_t i = 0; i < nRusAssigneds + 0; i++)
//     {
//       if (candidateIt == m_ul_candidates.end ())
//         {
//           break;
//         }

//       uint16_t staId = candidateIt->first->aid;
//       // AssignRuIndices will be called below to set RuSpec
//       dlMuInfo.txParams.m_txVector.SetHeMuUserInfo (staId,
//                                                     {{false, (i < nRusAssigneds ? HeRu::RU_26_TONE : HeRu::RU_26_TONE), 1},
//                                                        WifiMode (m_MCS), 1});
//       candidateIt++;
//     }


//   AssignRuIndices (dlMuInfo.txParams.m_txVector);


//     CtrlTriggerHeader trigger (TriggerFrameType::BSRP_TRIGGER, dlmuinfo_stored.txParams.m_txVector);

//     WifiTxVector txVector = dlmuinfo_stored.txParams.m_txVector;
//     txVector.SetGuardInterval (trigger.GetGuardInterval ());

//     Ptr<Packet> packet = Create<Packet> ();
//     packet->AddHeader (trigger);

//     Mac48Address receiver = Mac48Address::GetBroadcast ();
//     if (trigger.GetNUserInfoFields () == 1)
//       {
//         NS_ASSERT (m_apMac->GetStaList ().find (trigger.begin ()->GetAid12 ()) != m_apMac->GetStaList ().end ());
//         receiver = m_apMac->GetStaList ().at (trigger.begin ()->GetAid12 ());
//       }

//     WifiMacHeader hdr (WIFI_MAC_CTL_TRIGGER);
//     hdr.SetAddr1 (receiver);
//     hdr.SetAddr2 (m_apMac->GetAddress ());
//     hdr.SetDsNotTo ();
//     hdr.SetDsNotFrom ();

//     Ptr<WifiMacQueueItem> item = Create<WifiMacQueueItem> (packet, hdr);

//     m_txParams.Clear ();
//     // set the TXVECTOR used to send the Trigger Frame
//     m_txParams.m_txVector = m_apMac->GetWifiRemoteStationManager ()->GetRtsTxVector (receiver);

//     if (!m_heFem->TryAddMpdu (item, m_txParams, m_availableTime))
//       {
//         // sending the BSRP Trigger Frame is not possible, hence return NO_TX. In
//         // this way, no transmission will occur now and the next time we will
//         // try again sending a BSRP Trigger Frame.
//         NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
//         Notxcnt++;
//         return NO_TX;
//       }

//     // Compute the time taken by each station to transmit 8 QoS Null frames
//     Time qosNullTxDuration = Seconds (0);
//     for (const auto& userInfo : trigger)
//       {
//         Time duration = WifiPhy::CalculateTxDuration (m_sizeOf8QosNull, txVector,
//                                                       m_apMac->GetWifiPhy ()->GetPhyBand (),
//                                                       userInfo.GetAid12 ());
//         qosNullTxDuration = Max (qosNullTxDuration, duration);
//       }

//     if (m_availableTime != Time::Min ())
//       {
//         // TryAddMpdu only considers the time to transmit the Trigger Frame
//         NS_ASSERT (m_txParams.m_protection && m_txParams.m_protection->protectionTime != Time::Min ());
//         NS_ASSERT (m_txParams.m_acknowledgment && m_txParams.m_acknowledgment->acknowledgmentTime.IsZero ());
//         NS_ASSERT (m_txParams.m_txDuration != Time::Min ());

//         if (m_txParams.m_protection->protectionTime
//             + m_txParams.m_txDuration     // BSRP TF tx time
//             + m_apMac->GetWifiPhy ()->GetSifs ()
//             + qosNullTxDuration
//             > m_availableTime)
//           {
//             NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
//             Notxcnt++;
//             return NO_TX;
//           }
//       }

//     NS_LOG_INFO("Duration of QoS Null frames in ms: " << qosNullTxDuration.As (Time::MS));
//     // std::cout<<"TF sent and QoS Frames received"<<Simulator::Now().GetMicroSeconds()<<" us. Duration of QoS Null frames in ms: " << qosNullTxDuration.As (Time::MS)<<"\n";
//     trigger.SetUlLength (HePhy::ConvertHeTbPpduDurationToLSigLength (qosNullTxDuration,
//                                                                       m_apMac->GetWifiPhy ()->GetPhyBand ()));
//     trigger.SetCsRequired (true);
//     m_heFem->SetTargetRssi (trigger);

//     packet = Create<Packet> ();
//     packet->AddHeader (trigger);
//     m_trigger = Create<WifiMacQueueItem> (packet, hdr);

//     m_ulTriggerType = TriggerFrameType::BSRP_TRIGGER;
//     m_tbPpduDuration = qosNullTxDuration;

//////////////////////////////////////////////////////////////////////

  CtrlTriggerHeader trigger (TriggerFrameType::BSRP_TRIGGER, dlmuinfo_stored.txParams.m_txVector);

  WifiTxVector txVector = dlmuinfo_stored.txParams.m_txVector;
  txVector.SetGuardInterval (trigger.GetGuardInterval ());

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (trigger);

  Mac48Address receiver = Mac48Address::GetBroadcast ();
  if (trigger.GetNUserInfoFields () == 1)
    {
      NS_ASSERT (m_apMac->GetStaList ().find (trigger.begin ()->GetAid12 ()) != m_apMac->GetStaList ().end ());
      receiver = m_apMac->GetStaList ().at (trigger.begin ()->GetAid12 ());
    }

  WifiMacHeader hdr (WIFI_MAC_CTL_TRIGGER);
  hdr.SetAddr1 (receiver);
  hdr.SetAddr2 (m_apMac->GetAddress ());
  hdr.SetDsNotTo ();
  hdr.SetDsNotFrom ();

  Ptr<WifiMacQueueItem> item = Create<WifiMacQueueItem> (packet, hdr);

  m_txParams.Clear ();
  // set the TXVECTOR used to send the Trigger Frame
  m_txParams.m_txVector = m_apMac->GetWifiRemoteStationManager ()->GetRtsTxVector (receiver);

  if (!m_heFem->TryAddMpdu (item, m_txParams, m_availableTime))
    {
      // sending the BSRP Trigger Frame is not possible, hence return NO_TX. In
      // this way, no transmission will occur now and the next time we will
      // try again sending a BSRP Trigger Frame.
      NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
      Notxcnt++;
      return NO_TX;
    }

  // Compute the time taken by each station to transmit 8 QoS Null frames
  Time qosNullTxDuration = Seconds (0);
  for (const auto& userInfo : trigger)
    {
      Time duration = WifiPhy::CalculateTxDuration (m_sizeOf8QosNull, txVector,
                                                    m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                    userInfo.GetAid12 ());
      qosNullTxDuration = Max (qosNullTxDuration, duration);
    }

  if (m_availableTime != Time::Min ())
    {
      // TryAddMpdu only considers the time to transmit the Trigger Frame
      NS_ASSERT (m_txParams.m_protection && m_txParams.m_protection->protectionTime != Time::Min ());
      NS_ASSERT (m_txParams.m_acknowledgment && m_txParams.m_acknowledgment->acknowledgmentTime.IsZero ());
      NS_ASSERT (m_txParams.m_txDuration != Time::Min ());

      if (m_txParams.m_protection->protectionTime
          + m_txParams.m_txDuration     // BSRP TF tx time
          + m_apMac->GetWifiPhy ()->GetSifs ()
          + qosNullTxDuration
          > m_availableTime)
        {
          NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
          Notxcnt++;
          return NO_TX;
        }
    }

  NS_LOG_INFO("Duration of QoS Null frames in ms: " << qosNullTxDuration.As (Time::MS));
  // std::cout<<"TF sent and QoS Frames received"<<Simulator::Now().GetMicroSeconds()<<" us. Duration of QoS Null frames in ms: " << qosNullTxDuration.As (Time::MS)<<"\n";
  trigger.SetUlLength (HePhy::ConvertHeTbPpduDurationToLSigLength (qosNullTxDuration,
                                                                    m_apMac->GetWifiPhy ()->GetPhyBand ()));
  trigger.SetCsRequired (true);
  m_heFem->SetTargetRssi (trigger);

  packet = Create<Packet> ();
  packet->AddHeader (trigger);
  m_trigger = Create<WifiMacQueueItem> (packet, hdr);

  m_ulTriggerType = TriggerFrameType::BSRP_TRIGGER;
  m_tbPpduDuration = qosNullTxDuration;

  std::cout << "Issue function called: "<<"\n";
    
  }

  else{
  ///////////////////////////Original code//////////////////////////////////
  

  CtrlTriggerHeader trigger (TriggerFrameType::BSRP_TRIGGER, GetDlMuInfo ().txParams.m_txVector);

  WifiTxVector txVector = GetDlMuInfo ().txParams.m_txVector;
  txVector.SetGuardInterval (trigger.GetGuardInterval ());

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (trigger);

  Mac48Address receiver = Mac48Address::GetBroadcast ();
  if (trigger.GetNUserInfoFields () == 1)
    {
      NS_ASSERT (m_apMac->GetStaList ().find (trigger.begin ()->GetAid12 ()) != m_apMac->GetStaList ().end ());
      receiver = m_apMac->GetStaList ().at (trigger.begin ()->GetAid12 ());
    }

  WifiMacHeader hdr (WIFI_MAC_CTL_TRIGGER);
  hdr.SetAddr1 (receiver);
  hdr.SetAddr2 (m_apMac->GetAddress ());
  hdr.SetDsNotTo ();
  hdr.SetDsNotFrom ();

  Ptr<WifiMacQueueItem> item = Create<WifiMacQueueItem> (packet, hdr);

  m_txParams.Clear ();
  // set the TXVECTOR used to send the Trigger Frame
  m_txParams.m_txVector = m_apMac->GetWifiRemoteStationManager ()->GetRtsTxVector (receiver);

  if (!m_heFem->TryAddMpdu (item, m_txParams, m_availableTime))
    {
      // sending the BSRP Trigger Frame is not possible, hence return NO_TX. In
      // this way, no transmission will occur now and the next time we will
      // try again sending a BSRP Trigger Frame.
      NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
      Notxcnt++;
      return NO_TX;
    }

  // Compute the time taken by each station to transmit 8 QoS Null frames
  Time qosNullTxDuration = Seconds (0);
  for (const auto& userInfo : trigger)
    {
      Time duration = WifiPhy::CalculateTxDuration (m_sizeOf8QosNull, txVector,
                                                    m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                    userInfo.GetAid12 ());
      qosNullTxDuration = Max (qosNullTxDuration, duration);
    }

  if (m_availableTime != Time::Min ())
    {
      // TryAddMpdu only considers the time to transmit the Trigger Frame
      NS_ASSERT (m_txParams.m_protection && m_txParams.m_protection->protectionTime != Time::Min ());
      NS_ASSERT (m_txParams.m_acknowledgment && m_txParams.m_acknowledgment->acknowledgmentTime.IsZero ());
      NS_ASSERT (m_txParams.m_txDuration != Time::Min ());

      if (m_txParams.m_protection->protectionTime
          + m_txParams.m_txDuration     // BSRP TF tx time
          + m_apMac->GetWifiPhy ()->GetSifs ()
          + qosNullTxDuration
          > m_availableTime)
        {
          NS_LOG_DEBUG ("Remaining TXOP duration is not enough for BSRP TF exchange");
          Notxcnt++;
          return NO_TX;
        }
    }

  NS_LOG_INFO("Duration of QoS Null frames in ms: " << qosNullTxDuration.As (Time::MS));
  // std::cout<<"TF sent and QoS Frames received"<<Simulator::Now().GetMicroSeconds()<<" us. Duration of QoS Null frames in ms: " << qosNullTxDuration.As (Time::MS)<<"\n";
  trigger.SetUlLength (HePhy::ConvertHeTbPpduDurationToLSigLength (qosNullTxDuration,
                                                                    m_apMac->GetWifiPhy ()->GetPhyBand ()));
  trigger.SetCsRequired (true);
  m_heFem->SetTargetRssi (trigger);

  packet = Create<Packet> ();
  packet->AddHeader (trigger);
  m_trigger = Create<WifiMacQueueItem> (packet, hdr);

  m_ulTriggerType = TriggerFrameType::BSRP_TRIGGER;
  m_tbPpduDuration = qosNullTxDuration;
  }
//  ULtxcnt++;
  return UL_MU_TX;
 
}

int ul_count = 10;
bool ul_limit = false;
MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingBasicTf (void)
{
  NS_LOG_FUNCTION (this);
  std::cout << "At time "<<Simulator::Now().GetMicroSeconds () << " TrySendingBasicTf function called"<<"\n";
  // check if an UL OFDMA transmission is possible after a DL OFDMA transmission
  NS_ABORT_MSG_IF (m_ulPsduSize == 0, "The UlPsduSize attribute must be set to a non-null value");

  // determine which of the stations served in DL have UL traffic
  uint32_t maxBufferSize = 0;
  
  // candidates sorted in decreasing order of queue size
  std::multimap<uint8_t, CandidateInfo, std::greater<uint8_t>> ulCandidates;

   // Sri Prakash
  // mycode begin
  m_ul_candidates.clear();
  AcIndex primaryAc = m_edca->GetAccessCategory ();
  std::cout <<"ulprimaryac: "<< unsigned(primaryAc)<<'\n';
  std::cout << "stations for ul:"<< m_staList[primaryAc].size()<<"\n";
  auto staIt = m_staList[primaryAc].begin ();  
  while (staIt != m_staList[primaryAc].end ())
    { 
      uint8_t tid=0;
      while (tid < 8){
        AcIndex ac = QosUtilsMapTidToAc (tid);
        Ptr<const WifiMacQueueItem> mpdu_dummy;    
        if (m_apMac->GetQosTxop (ac)->GetBaAgreementEstablished (staIt->address, tid)){
            m_ul_candidates.push_back({staIt, mpdu_dummy}); 
            std::cout<<"AID is "<<(staIt->aid)<<"\n";
           break;    // terminate the for loop
        }            
        tid++;
      }      
      staIt++;
    }

std::cout <<"mulcandsize: "<<m_ul_candidates.size()<<"\n";
  // my code end
  // Sri Prakash

std::vector<std::pair<uint8_t, ns3::RrMultiUserScheduler::CandidateInfo>> q_array;

if(!m_enableBsrp){

   for (const auto& candidate : m_ul_candidates)
  //for (const auto& candidate : m_candidates)
    {
      // if(candidate.first->address == "00:00:00:00:00:03" || candidate.first->address == "00:00:00:00:00:04" )continue;
        uint8_t random_queue = (rand() % 20) + 1;
        std::cout << "random queue of station: "<< candidate.first->address << " is :" << int(random_queue)  <<'\n';
        maxBufferSize = std::max(maxBufferSize, static_cast<uint32_t> (random_queue * 256));
        ulCandidates.emplace (random_queue, candidate); //Giving equal size when no bsrp to give fair allocation
        }
 
}
else{

// uint8_t tid = 0;
// while(tid<8){

  for (const auto& candidate : m_ul_candidates)
  //for (const auto& candidate : m_candidates)
    {

  //     WifiMacHeader& hdr = mpdu_stored->GetHeader ();
  //     NS_ASSERT (hdr.IsQosData ());

  //   uint32_t bufferSize = m_queue->GetNBytes (hdr.GetQosTid (), hdr.GetAddr1 ())
  //                       + m_baManager->GetRetransmitQueue ()->GetNBytes (hdr.GetQosTid (), hdr.GetAddr1 ());
  // // A queue size value of 254 is used for all sizes greater than 64 768 octets.
  //     uint8_t queueSize = static_cast<uint8_t> (std::ceil (std::min (bufferSize, 64769u) / 256.0));


      uint8_t queueSize = m_apMac->GetMaxBufferStatus(candidate.first->address);
      // q_array.push_back({queueSize, candidate});
      // i++;

      std::cout << "Buffer status of station " << candidate.first->address << " is " << +queueSize << "\n";
      //std::cout<<"PSDU size"<<m_ulPsduSize<<"\n";
      if (queueSize == 255)
        {
          NS_LOG_DEBUG ("Buffer status of station " << candidate.first->address << " is unknown");          
          maxBufferSize = std::max (maxBufferSize, m_ulPsduSize);
        }
      else if (queueSize == 254)
        {
          NS_LOG_DEBUG ("Buffer status of station " << candidate.first->address << " is not limited");
          maxBufferSize = 0xffffffff;
        }
      else
        {
          NS_LOG_DEBUG ("Buffer status of station " << candidate.first->address << " is " << +queueSize);          
          maxBufferSize = std::max (maxBufferSize, static_cast<uint32_t> (queueSize * 256));
        }
      // serve the station if its queue size is not null
      if (queueSize > 0 && ulCandidates.size() < 9 && m_enableBsrp && queueSize != 255)
        {
          ulCandidates.emplace (queueSize, candidate); //multimap already puts them in sorted order(decreasing queue size)
        }

        

    }    
// tid++;
// }
}

 // if the maximum buffer size is 0, skip UL OFDMA and proceed with trying DL OFDMA
  if (maxBufferSize > 0 && !ulCandidates.empty())
    {
      ulstop_count++;
      
      NS_ASSERT (!ulCandidates.empty ());
      UL_cycle_count++;
      std::cout << "UL Cycle count: "<<UL_cycle_count << '\n';
      std::size_t count = ulCandidates.size();
      // std::size_t count = std::fmin(2, ulCandidates.size());
      std::size_t nCentral26TonesRus;

      bool ul_scheduler;
      // if(m_schedulerLogic == "Bellalta"){
      //   ul_scheduler = false;
      // }else{
      //   ul_scheduler = true;
      // }

      // std::string m_schedulerLogic_UL = "rr"; // Full bw
      std::string m_schedulerLogic_UL = "Bellalta"; // equal split
      

      if(m_schedulerLogic_UL == "Bellalta"){
        ul_scheduler = false;
      }else{
        ul_scheduler = true;
      }

      if(ulCandidates.size() > 9){
        ul_scheduler = true;
      }


      std::cout<< "DL scheduler logic: " << m_schedulerLogic <<'\n';
      std::cout<< "UL scheduler logic: " << m_schedulerLogic_UL <<'\n'; 
      //standard == rr
      //

      HeRu::RuType ruType = HeRu::GetEqualSizedRusForStations (m_apMac->GetWifiPhy ()->GetChannelWidth (),
                                                               count, nCentral26TonesRus, ul_scheduler);

      // False: Equal split
      // True: Full BW

      if (!m_useCentral26TonesRus || ulCandidates.size () == count)
        {
          nCentral26TonesRus = 0;
        }
      else
        {
          nCentral26TonesRus = std::min (ulCandidates.size () - count, nCentral26TonesRus);
        }

      WifiTxVector txVector;
      txVector.SetPreambleType (WIFI_PREAMBLE_HE_TB);
      std::cout << "m_ul_candidates size: " << m_ul_candidates.size() << '\n';
      std::cout << "UL candidates size: " << ulCandidates.size() << '\n';
      std::cout << "Count: " << count << '\n';
      std::cout << "ncentral26toneRUs: " << nCentral26TonesRus << '\n';
      auto candidateIt = ulCandidates.begin ();

      count = std::min(count, ulCandidates.size()); //later correction
      std::cout << "Updated Count variable: " << count << '\n';


      if (GetLastTxFormat () == DL_MU_TX)  //When BSRP is OFF
        {
          txVector.SetChannelWidth (GetDlMuInfo ().txParams.m_txVector.GetChannelWidth ());
          txVector.SetGuardInterval (CtrlTriggerHeader ().GetGuardInterval ());

          for (std::size_t i = 0; i < count + nCentral26TonesRus; i++)
            {

              std::cout << "hardik1: Last DL/bsrp off" << '\n';
              std::cout << "UL candidates size inside loop: " << ulCandidates.size() << '\n';
              // if(candidateIt != ulCandidates.end ()){
              //   break;
              // }
              NS_ASSERT (candidateIt != ulCandidates.end ());
              uint16_t staId = candidateIt->second.first->aid;
              std::cout << candidateIt->second.first->address<<" "<<ruType<< '\n';
              
              if(ruType == HeRu::RU_106_TONE){
                ru_106++;
              }else if(ruType == HeRu::RU_52_TONE){
                ru_52++;
              }else if(ruType == HeRu::RU_242_TONE){
                ru_242++;
              }else if(ruType == HeRu::RU_26_TONE){
                ru_26++;
              }

              // AssignRuIndices will be called below to set RuSpec              
              txVector.SetHeMuUserInfo (staId,   
                                        {{false, (i < count ? ruType : HeRu::RU_26_TONE), 1},    
                                        //GetUlMuInfo ().txParams.m_txVector.GetMode (staId),
                                        //GetUlMuInfo ().txParams.m_txVector.GetNss (staId)});
                                         WifiMode (m_MCS), 1});                                        
              //std::cout<< "params set \n";      // Sri Prakash
              candidateIt++;
            }
        }
      else // when BSRP is ON
        {
          CtrlTriggerHeader trigger;
          GetUlMuInfo ().trigger->GetPacket ()->PeekHeader (trigger);
 
          txVector.SetChannelWidth (trigger.GetUlBandwidth ());
          txVector.SetGuardInterval (trigger.GetGuardInterval ());

          for (std::size_t i = 0; i < count + nCentral26TonesRus; i++)
            {
              std::cout << "hardik2: Last UL/bsrp on" << '\n';
            
              // if(candidateIt != ulCandidates.end ()){
              //   break;
              // }
              NS_ASSERT (candidateIt != ulCandidates.end ());
              uint16_t staId = candidateIt->second.first->aid;
              // auto userInfoIt = trigger.FindUserInfoWithAid (staId);
              //NS_ASSERT (userInfoIt != trigger.end ());
              // AssignRuIndices will be called below to set RuSpec
              std::cout << candidateIt->second.first->address<<" "<<ruType<< '\n';
              if(ruType == HeRu::RU_106_TONE){
                ru_106++;
              }else if(ruType == HeRu::RU_52_TONE){
                ru_52++;
              }else if(ruType == HeRu::RU_242_TONE){
                ru_242++;
              }else if(ruType == HeRu::RU_26_TONE){
                ru_26++;
              }

              txVector.SetHeMuUserInfo (staId,
                                        {{false, (i < count ? ruType : HeRu::RU_26_TONE), 1},
                                        // HePhy::GetHeMcs (userInfoIt->GetUlMcs ()),
                                        // userInfoIt->GetNss ()});
                                        WifiMode (m_MCS), 1});

              candidateIt++;
            }    // Sri Prakash
        }

      
      std::cout << "242 tone RU count: " << ru_242 << "\n";

      std::cout << "106 tone RU count: " << ru_106 << "\n";

      std::cout << "52 tone RU count: " << ru_52 << "\n";

      std::cout << "26 tone RU count: " << ru_26 << "\n";

      // remove candidates that will not be served
      ulCandidates.erase (candidateIt, ulCandidates.end ());
      std::cout << "After erasing UL candidates size :" << ulCandidates.size() <<'\n'; 
      AssignRuIndices (txVector);

      CtrlTriggerHeader trigger (TriggerFrameType::BASIC_TRIGGER, txVector);
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (trigger);

      Mac48Address receiver = Mac48Address::GetBroadcast ();
      if (ulCandidates.size () == 1)
        {
          receiver = ulCandidates.begin ()->second.first->address;
        }

      WifiMacHeader hdr (WIFI_MAC_CTL_TRIGGER);
      hdr.SetAddr1 (receiver);
      hdr.SetAddr2 (m_apMac->GetAddress ());
      hdr.SetDsNotTo ();
      hdr.SetDsNotFrom ();

      Ptr<WifiMacQueueItem> item = Create<WifiMacQueueItem> (packet, hdr);

      // compute the maximum amount of time that can be granted to stations.
      // This value is limited by the max PPDU duration
      Time maxDuration = GetPpduMaxTime (txVector.GetPreambleType ());

      m_txParams.Clear ();
      // set the TXVECTOR used to send the Trigger Frame
      m_txParams.m_txVector = m_apMac->GetWifiRemoteStationManager ()->GetRtsTxVector (receiver);

      if (!m_heFem->TryAddMpdu (item, m_txParams, m_availableTime))
        {
          // an UL OFDMA transmission is not possible, hence return NO_TX. In
          // this way, no transmission will occur now and the next time we will
          // try again performing an UL OFDMA transmission.
          NS_LOG_DEBUG ("Remaining TXOP duration is not enough for UL MU exchange");
          Notxcnt++;
          return NO_TX;
        }

      if (m_availableTime != Time::Min ())
        {
          // TryAddMpdu only considers the time to transmit the Trigger Frame
          NS_ASSERT (m_txParams.m_protection && m_txParams.m_protection->protectionTime != Time::Min ());
          NS_ASSERT (m_txParams.m_acknowledgment && m_txParams.m_acknowledgment->acknowledgmentTime != Time::Min ());
          NS_ASSERT (m_txParams.m_txDuration != Time::Min ());

          maxDuration = Min (maxDuration, m_availableTime
                                          - m_txParams.m_protection->protectionTime
                                          - m_txParams.m_txDuration
                                          - m_apMac->GetWifiPhy ()->GetSifs ()
                                          - m_txParams.m_acknowledgment->acknowledgmentTime);
          if (maxDuration.IsNegative ())
            {
              NS_LOG_DEBUG ("Remaining TXOP duration is not enough for UL MU exchange");
              Notxcnt++;
              std::cout<<"Remaining TXOP duration is not enough for UL MU exchange\n";
              return NO_TX;
            }
        }

      // Compute the time taken by each station to transmit a frame of maxBufferSize size
      Time bufferTxTime = Seconds (0);
      for (const auto& userInfo : trigger)
        {
          Time duration = WifiPhy::CalculateTxDuration (maxBufferSize, txVector,
                                                        m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                        userInfo.GetAid12 ());
          bufferTxTime = Max (bufferTxTime, duration);
        }

      if (bufferTxTime < maxDuration)
        {
          // the maximum buffer size can be transmitted within the allowed time
          maxDuration = bufferTxTime;
        }
      else
        {
          // maxDuration may be a too short time. If it does not allow any station to
          // transmit at least m_ulPsduSize bytes, give up the UL MU transmission for now
          Time minDuration = Seconds (0);
          for (const auto& userInfo : trigger)
            {
              Time duration = WifiPhy::CalculateTxDuration (m_ulPsduSize, txVector,
                                                            m_apMac->GetWifiPhy ()->GetPhyBand (),
                                                            userInfo.GetAid12 ());
              minDuration = (minDuration.IsZero () ? duration : Min (minDuration, duration));
            }

          if (maxDuration < minDuration)
            {
              // maxDuration is a too short time, hence return NO_TX. In this way,
              // no transmission will occur now and the next time we will try again
              // performing an UL OFDMA transmission.
              NS_LOG_DEBUG ("Available time " << maxDuration.As (Time::MS) << " is too short");
              std::cout<<"Available time " << maxDuration.As (Time::MS) << " is too short\n";
              Notxcnt++;
              std::cout<< "At time "<<Simulator::Now().GetMicroSeconds()<<"NO TX heyy called\n";
              return NO_TX;
            }
        }

      // maxDuration is the time to grant to the stations. Finalize the Trigger Frame
      NS_LOG_DEBUG ("TB PPDU duration: " << maxDuration.As (Time::MS));
      trigger.SetUlLength (HePhy::ConvertHeTbPpduDurationToLSigLength (maxDuration,
                                                                       m_apMac->GetWifiPhy ()->GetPhyBand ()));
      trigger.SetCsRequired (true);
      m_heFem->SetTargetRssi (trigger);
      // set Preferred AC to the AC that gained channel access
      std::cout << "AP EDCA access category: "<< unsigned(m_edca->GetAccessCategory ()) << "\n";
      tcount++;
   //   ns3::AcIndex ac_dummy = ((tcount&1 )? AC_BE: AC_VO);
      // ns3::AcIndex ac_dummy = AC_;
      for (auto& userInfo : trigger)
        {

          std::cout << "AID of user: " << userInfo.GetAid12() <<'\n';
          userInfo.SetBasicTriggerDepUserInfo (0, 0, m_edca->GetAccessCategory ());
          
          // userInfo.SetBasicTriggerDepUserInfo (0, 0, ac_dummy);
        }

      packet = Create<Packet> ();
      packet->AddHeader (trigger);
      m_trigger = Create<WifiMacQueueItem> (packet, hdr);

      m_ulTriggerType = TriggerFrameType::BASIC_TRIGGER;
      m_tbPpduDuration = maxDuration;
      ULtxcnt++;
      std::cout << "UL_MU_TX" << '\n';
      return UL_MU_TX;

      }
    // }
    // std::cout<< "At time "<<Simulator::Now().GetMicroSeconds()<<" Doing DL since max Buffer size of stations is empty\n";
  std::cout << "DL_MU_TX" << '\n';
  DLtxcnt++;
  std::cout << "DL when UL has no data"<<"\n";
  return DL_MU_TX;
}


void
RrMultiUserScheduler::NotifyStationAssociated (uint16_t aid, Mac48Address address)
{
  NS_LOG_FUNCTION (this << aid << address);

  if (GetWifiRemoteStationManager ()->GetHeSupported (address))
    {
      for (auto& staList : m_staList)
        {
          staList.second.push_back (MasterInfo {aid, address, 0.0});
        }
    }
}

void
RrMultiUserScheduler::NotifyStationDeassociated (uint16_t aid, Mac48Address address)
{
  NS_LOG_FUNCTION (this << aid << address);

  if (GetWifiRemoteStationManager ()->GetHeSupported (address))
    {
      for (auto& staList : m_staList)
        {
          staList.second.remove_if ([&aid, &address] (const MasterInfo& info)
                                    { return info.aid == aid && info.address == address; });
        }
    }
}

MultiUserScheduler::TxFormat
RrMultiUserScheduler::TrySendingDlMuPpdu (void)
{
  NS_LOG_FUNCTION (this);

  AcIndex primaryAc = m_edca->GetAccessCategory ();
  std::cout << "dlprimaryac: "<< unsigned(primaryAc) << '\n';

  if (m_staList[primaryAc].empty ())
    {
      NS_LOG_DEBUG ("No HE stations associated: return SU_TX");
      std::cout<<"No HE stations associated: return SU_TX"<<"\n";

      return TxFormat::SU_TX;
    }
 //std::cout<<m_nStations<<" "<<m_staList[primaryAc].size ()<<"\n";

  
  std::size_t count = std::min (static_cast<std::size_t> (m_nStations), m_staList[primaryAc].size ());
  std::size_t nCentral26TonesRus;
  HeRu::RuType ruType;
   std::cout<<count<<"TrySendingDlMuPpdu called \n";
 m_candidates.clear();

 auto staIt1 = m_staList[primaryAc].begin ();
   uint8_t currTid = wifiAcList.at (primaryAc).GetHighTid ();

  Ptr<const WifiMacQueueItem> mpdu = m_edca->PeekNextMpdu ();

  if (mpdu != nullptr && mpdu->GetHeader ().IsQosData ())
    {
      currTid = mpdu->GetHeader ().GetQosTid ();
    }

  // determine the list of TIDs to check
  std::vector<uint8_t> tids;

  if (m_enableTxopSharing)
    {
      for (auto acIt = wifiAcList.find (primaryAc); acIt != wifiAcList.end (); acIt++)
        {
          uint8_t firstTid = (acIt->first == primaryAc ? currTid : acIt->second.GetHighTid ());
          tids.push_back (firstTid);
          tids.push_back (acIt->second.GetOtherTid (firstTid));
        }
    }
  else
    {
      tids.push_back (currTid);
    }

 
 while (staIt1 != m_staList[primaryAc].end ()
         && m_candidates.size () < std::min (static_cast<std::size_t> (m_nStations), count + nCentral26TonesRus))
    {
      NS_LOG_DEBUG ("Next candidate STA (MAC=" << staIt1->address << ", AID=" << staIt1->aid << ")");
     
      // check if the AP has at least one frame to be sent to the current station
      for (uint8_t tid : tids)
        {
          AcIndex ac = QosUtilsMapTidToAc (tid);
          NS_ASSERT (ac >= primaryAc);
          // check that a BA agreement is established with the receiver for the
          // considered TID, since ack sequences for DL MU PPDUs require block ack
          if (m_apMac->GetQosTxop (ac)->GetBaAgreementEstablished (staIt1->address, tid))
            {
              mpdu = m_apMac->GetQosTxop (ac)->PeekNextMpdu (tid, staIt1->address);
 
              if (mpdu != 0)
                { 
                      NS_LOG_DEBUG ("Adding candidate STA (MAC=" << staIt1->address << ", AID="
                                    << staIt1->aid << ") TID=" << +tid);
                      m_candidates.push_back ({staIt1, mpdu});
                 
                }
              else
                {
                //  std::cout<<"No frames to send to " << staIt->address << " with TID=" << +tid<<"\n";
                  NS_LOG_DEBUG ("No frames to send to " << staIt1->address << " with TID=" << +tid);
                }
            }
        }

      // move to the next station in the list
      staIt1++;
    } 

count = std::min (static_cast<std::size_t> (m_nStations), m_candidates.size ());
if(count==0)count=1;
  std::cout<<count<<" Printing count \n";
  std::size_t limit = 9;
  count = std::min(count, limit);

  if (m_schedulerLogic == "Standard")
    {
      ruType = HeRu::GetEqualSizedRusForStations (m_apMac->GetWifiPhy ()->GetChannelWidth (), count, nCentral26TonesRus);
    }
  else
    {
      ruType = HeRu::GetEqualSizedRusForStations (m_apMac->GetWifiPhy ()->GetChannelWidth (), count, nCentral26TonesRus, false);
    }
   // std::cout<<count<<" Printing count \n";
  NS_ASSERT (count >= 1);

  if (!m_useCentral26TonesRus)
    {
      nCentral26TonesRus = 0;
    }




  m_txParams.Clear ();
  m_txParams.m_txVector.SetPreambleType (WIFI_PREAMBLE_HE_MU);
  m_txParams.m_txVector.SetChannelWidth (m_apMac->GetWifiPhy ()->GetChannelWidth ());
  m_txParams.m_txVector.SetGuardInterval (m_apMac->GetHeConfiguration ()->GetGuardInterval ().GetNanoSeconds ());

  // The TXOP limit can be exceeded by the TXOP holder if it does not transmit more
  // than one Data or Management frame in the TXOP and the frame is not in an A-MPDU
  // consisting of more than one MPDU (Sec. 10.22.2.8 of 802.11-2016).
  // For the moment, we are considering just one MPDU per receiver.
  Time actualAvailableTime = (m_initialFrame ? Time::Min () : m_availableTime);

  // iterate over the associated stations until an enough number of stations is identified
 // auto staIt1 = m_staList[primaryAc].begin ();
  m_candidates.clear ();
//m_allcandidates.clear ();

  // while(staIt1 != m_staList[primaryAc].end ()){
  //   m_allcandidates.push_back({staIt1,NULL});
  //   staIt1++;
  // }

 auto staIt = m_staList[primaryAc].begin ();
  NS_LOG_DEBUG ("m_nStations: " << unsigned(m_nStations) << ", count: " << count << ", m_staList: " << m_staList[primaryAc].size());
  std::cout << "m_nStations: " << unsigned(m_nStations) << ", count: " << count << ", m_staList: " << m_staList[primaryAc].size() << '\n';
  while (staIt != m_staList[primaryAc].end ()
         && m_candidates.size () < std::min (static_cast<std::size_t> (m_nStations), count + nCentral26TonesRus))
    {
      NS_LOG_DEBUG ("Next candidate STA (MAC=" << staIt->address << ", AID=" << staIt->aid << ")");
  
      HeRu::RuType currRuType = (m_candidates.size () < count ? ruType : HeRu::RU_26_TONE);

      // check if the AP has at least one frame to be sent to the current station
      for (uint8_t tid : tids)
        {
          AcIndex ac = QosUtilsMapTidToAc (tid);
          NS_ASSERT (ac >= primaryAc);
          // check that a BA agreement is established with the receiver for the
          // considered TID, since ack sequences for DL MU PPDUs require block ack
          if (m_apMac->GetQosTxop (ac)->GetBaAgreementEstablished (staIt->address, tid))
            {
              mpdu = m_apMac->GetQosTxop (ac)->PeekNextMpdu (tid, staIt->address);
    
  // if( m_staList[primaryAc].size() == m_nStations  && m_allcandidates.size()<m_nStations ){
  //   //std::cout<<"gegeggHHHHHHHHHHHHHHHHHHHHHHHe\n";
  //       m_allcandidates.push_back({staIt, mpdu});
  //     }
              // we only check if the first frame of the current TID meets the size
              // and duration constraints. We do not explore the queues further.
              if (mpdu != 0)
                {
                  // Use a temporary TX vector including only the STA-ID of the
                  // candidate station to check if the MPDU meets the size and time limits.
                  // An RU of the computed size is tentatively assigned to the candidate
                  // station, so that the TX duration can be correctly computed.

                  WifiTxVector suTxVector = GetWifiRemoteStationManager ()->GetDataTxVector (mpdu->GetHeader ()),
                               txVectorCopy = m_txParams.m_txVector;

                  m_txParams.m_txVector.SetHeMuUserInfo (staIt->aid,
                                                         {{false, currRuType, 1},
                                                          suTxVector.GetMode (),
                                                          suTxVector.GetNss ()});
// std::cout<<WifiMode("HeMcs5")<<" Printing mode ?\n";
                  if (!m_heFem->TryAddMpdu (mpdu, m_txParams, actualAvailableTime))
                    {
                      //std::cout<<"Adding the peeked frame violates the time constraints\n";
                      NS_LOG_DEBUG ("Adding the peeked frame violates the time constraints");
                      m_txParams.m_txVector = txVectorCopy;
                    }
                  else
                    {
                      // the frame meets the constraints
//  std::cout<<"Adding candidate STA (MAC=" << staIt->address << ", AID="
//                                     << staIt->aid << ") TID=" << +tid<<"\n";
                      NS_LOG_DEBUG ("Adding candidate STA (MAC=" << staIt->address << ", AID="
                                    << staIt->aid << ") TID=" << +tid);
                      m_candidates.push_back ({staIt, mpdu});
                      break;    // terminate the for loop
                    }
                }
              else
                {
                //  std::cout<<"No frames to send to " << staIt->address << " with TID=" << +tid<<"\n";
                  NS_LOG_DEBUG ("No frames to send to " << staIt->address << " with TID=" << +tid);
                }
            }
        }

      // move to the next station in the list
      staIt++;
    } 
    //std::cout << "Try Sending DL MU PPDu called Candidates available to be scheduled for DL: " << m_candidates.size() << '\n';
std::cout<<"DL All stations size"<<m_candidates.size()<<" Total stations: "<<int(m_nStations)<<"\n";
  if (m_candidates.empty ())
    {
      if (m_forceDlOfdma)
        {
          NS_LOG_DEBUG ("The AP does not have suitable frames to transmit: return NO_TX");
          Notxcnt++;
          std::cout<<"AP has no data to send hui";
          return NO_TX;
        }
      NS_LOG_DEBUG ("The AP does not have suitable frames to transmit: return SU_TX");
      return SU_TX;
    }
  DLtxcnt++;
  std::cout << "DL in dlmuppdu "<< "\n";
  // if(flag) mpdu_stored = mpdu;
  return TxFormat::DL_MU_TX;
}



MultiUserScheduler::DlMuInfo
RrMultiUserScheduler::ComputeDlMuInfo (void)  
{
  computedlinfocount++;
  NS_LOG_FUNCTION (this);
  std::cout<<"Compute DL Mu info called\n";
// if(getall) m_candidates = m_allcandidates ;
  if (m_candidates.empty ())
    {
      // std::cout<< "At time "<<Simulator::Now().GetMicroSeconds()<<" Empty DL candidate vector\n";
      return DlMuInfo ();
    }
  

  uint16_t bw = m_apMac->GetWifiPhy ()->GetChannelWidth ();

  // compute how many stations can be granted an RU and the RU size
  std::size_t nRusAssigned = m_txParams.GetPsduInfoMap ().size ();
  std::size_t nCentral26TonesRus;
// std::cout<< "At time "<<Simulator::Now().GetMicroSeconds()<<" Computing DL Info vector of"<<m_candidates.size()<< "stations \n";
  HeRu::RuType ruType;
  if (m_schedulerLogic == "Standard")
    {
      ruType = HeRu::GetEqualSizedRusForStations (bw, nRusAssigned, nCentral26TonesRus);
      std::cout <<  "At time "<<Simulator::Now().GetMicroSeconds()<< "\n";
      std::cout << "Using the standard scheduler(full bw(no bw waste)) for DL, trying to allocate " << m_candidates.size() << " STAs" << std::endl;
      // std::cout << "Decided to use RUs of type " << ruType << std::endl;
    }
  else
    {
      ruType = HeRu::GetEqualSizedRusForStations (bw, nRusAssigned, nCentral26TonesRus, false);
      std::cout <<  "At time "<<Simulator::Now().GetMicroSeconds()<< "\n";
      std::cout << "Using the bellalta (equal split(bw waste)) scheduler for DL, trying to allocate " << m_candidates.size() << " STAs" << std::endl;
      // std::cout << "Decided to use RUs of type " << ruType << std::endl;
    }

  std::cout << "RUs assigned: " << nRusAssigned << '\n';
  std::cout << "mcandidates size(DL): "<< m_candidates.size() << "\n";
  std::cout << "ncentraltone26RUs(DL): "<< nCentral26TonesRus << '\n';

  NS_LOG_DEBUG (nRusAssigned << " stations are being assigned a " << ruType << " RU");

  if (!m_useCentral26TonesRus || m_candidates.size () == nRusAssigned)
    {
      nCentral26TonesRus = 0;
    }
  else
    {
      nCentral26TonesRus = std::min (m_candidates.size () - nRusAssigned, nCentral26TonesRus);
      NS_LOG_DEBUG (nCentral26TonesRus << " stations are being assigned a 26-tones RU");
    }

  DlMuInfo dlMuInfo;

  // We have to update the TXVECTOR
  dlMuInfo.txParams.m_txVector.SetPreambleType (m_txParams.m_txVector.GetPreambleType ());
  dlMuInfo.txParams.m_txVector.SetChannelWidth (m_txParams.m_txVector.GetChannelWidth ());
  dlMuInfo.txParams.m_txVector.SetGuardInterval (m_txParams.m_txVector.GetGuardInterval ());
 
  auto candidateIt = m_candidates.begin (); // iterator over the list of candidate receivers

  for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
    {
      if (candidateIt == m_candidates.end ())
        {
          break;
        }

      uint16_t staId = candidateIt->first->aid;
      // AssignRuIndices will be called below to set RuSpec
      dlMuInfo.txParams.m_txVector.SetHeMuUserInfo (staId,
                                                    {{false, (i < nRusAssigned ? ruType : HeRu::RU_26_TONE), 1},
                                                      m_txParams.m_txVector.GetMode (staId),
                                                      m_txParams.m_txVector.GetNss (staId)});
      candidateIt++;
    }

  std::cout << "no bsrp dl mu info"<<"\n";

  // remove candidates that will not be served
  m_candidates.erase (candidateIt, m_candidates.end ());

  AssignRuIndices (dlMuInfo.txParams.m_txVector);
  m_txParams.Clear ();

  Ptr<const WifiMacQueueItem> mpdu;
// std::cout<<"----------------------------------------Allocation Done --------------------------------------------------\n";
  // std::cout << "We have " << m_candidates.size() << " STAs to serve" << std::endl;
  // std::cout << "Assigned " << nRusAssigned << " RUs" << std::endl;

  // Compute the TX params (again) by using the stored MPDUs and the final TXVECTOR
  Time actualAvailableTime = (m_initialFrame ? Time::Min () : m_availableTime);

  for (const auto& candidate : m_candidates)
    {
      mpdu = candidate.second;
     // if(mpdu == nullptr)m_candidates.erase(candidate);
      NS_ASSERT (mpdu != nullptr);

      bool ret = m_heFem->TryAddMpdu (mpdu, dlMuInfo.txParams, actualAvailableTime);
      NS_UNUSED (ret);
      NS_ASSERT_MSG (ret, "Weird that an MPDU does not meet constraints when "
                          "transmitted over a larger RU");
    }

  // We have to complete the PSDUs to send
  Ptr<WifiMacQueue> queue;
  Mac48Address receiver;

  for (const auto& candidate : m_candidates)
    {
      // Let us try first A-MSDU aggregation if possible
      mpdu = candidate.second;
      NS_ASSERT (mpdu != nullptr);
      uint8_t tid = mpdu->GetHeader ().GetQosTid ();
      receiver = mpdu->GetHeader ().GetAddr1 ();
      NS_ASSERT (receiver == candidate.first->address);

      NS_ASSERT (mpdu->IsQueued ());
      WifiMacQueueItem::QueueIteratorPair queueIt = mpdu->GetQueueIteratorPairs ().front ();
      NS_ASSERT (queueIt.queue != nullptr);
      Ptr<WifiMacQueueItem> item = *queueIt.it;
      queueIt.it++;

      if (!mpdu->GetHeader ().IsRetry ())
        {
          // this MPDU must have been dequeued from the AC queue and we can try
          // A-MSDU aggregation
          item = m_heFem->GetMsduAggregator ()->GetNextAmsdu (mpdu, dlMuInfo.txParams, m_availableTime, queueIt);

          if (item == nullptr)
            {
              // A-MSDU aggregation failed or disabled
              item = *mpdu->GetQueueIteratorPairs ().front ().it;
            }
          m_apMac->GetQosTxop (QosUtilsMapTidToAc (tid))->AssignSequenceNumber (item);
        }

      // Now, let's try A-MPDU aggregation if possible
      std::vector<Ptr<WifiMacQueueItem>> mpduList = m_heFem->GetMpduAggregator ()->GetNextAmpdu (item, dlMuInfo.txParams, m_availableTime, queueIt);

      if (mpduList.size () > 1)
        {
          // A-MPDU aggregation succeeded, update psduMap
          dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu> (std::move (mpduList));
        }
      else
        {
          dlMuInfo.psduMap[candidate.first->aid] = Create<WifiPsdu> (item, true);
        }
    }

  AcIndex primaryAc = m_edca->GetAccessCategory ();

  // The amount of credits received by each station equals the TX duration (in
  // microseconds) divided by the number of stations.
  double creditsPerSta = dlMuInfo.txParams.m_txDuration.ToDouble (Time::US)
                        / m_staList[primaryAc].size ();
  // Transmitting stations have to pay a number of credits equal to the TX duration
  // (in microseconds) times the allocated bandwidth share.
  double debitsPerMhz = dlMuInfo.txParams.m_txDuration.ToDouble (Time::US)
                        / (nRusAssigned * HeRu::GetBandwidth (ruType)
                          + nCentral26TonesRus * HeRu::GetBandwidth (HeRu::RU_26_TONE));

  // assign credits to all stations
  for (auto& sta : m_staList[primaryAc])
    {
      sta.credits += creditsPerSta;
      sta.credits = std::min (sta.credits, m_maxCredits.ToDouble (Time::US));
    }

  // subtract debits to the selected stations
  candidateIt = m_candidates.begin ();

  for (std::size_t i = 0; i < nRusAssigned + nCentral26TonesRus; i++)
    {
      if (candidateIt == m_candidates.end ())
        {
          break;
        }

      candidateIt->first->credits -= debitsPerMhz * HeRu::GetBandwidth (i < nRusAssigned ? ruType : HeRu::RU_26_TONE);

      candidateIt++;
    }

  // sort the list in decreasing order of credits
  m_staList[primaryAc].sort ([] (const MasterInfo& a, const MasterInfo& b)
                              { return a.credits > b.credits; });

  NS_LOG_DEBUG ("Next station to serve has AID=" << m_staList[primaryAc].front ().aid);
  getall = false;

  std::cout << "MCANDIDATES size: "<<m_candidates.size() <<"\n";

  if(computedlinfocount < 100 && m_candidates.size() == m_nStations && flag){
    std::cout << "computeDLMUINFO called count: "<<computedlinfocount<<'\n';
    dlmuinfo_stored = dlMuInfo;
    flag = false;
  }

  return dlMuInfo;
}

void
RrMultiUserScheduler::AssignRuIndices (WifiTxVector& txVector)
{
  NS_LOG_FUNCTION (this << txVector);

  uint8_t bw = txVector.GetChannelWidth ();

  // find the RU types allocated in the TXVECTOR
  std::set<HeRu::RuType> ruTypeSet;
  for (const auto& userInfo : txVector.GetHeMuUserInfoMap ())
    {
      ruTypeSet.insert (userInfo.second.ru.ruType);
    }

  std::vector<HeRu::RuSpec> ruSet, central26TonesRus;

  // This scheduler allocates equal sized RUs and optionally the remaining 26-tone RUs
  if (ruTypeSet.size () == 2)
    {
      // central 26-tone RUs have been allocated
      NS_ASSERT (ruTypeSet.find (HeRu::RU_26_TONE) != ruTypeSet.end ());
      ruTypeSet.erase (HeRu::RU_26_TONE);
      NS_ASSERT (ruTypeSet.size () == 1);
      central26TonesRus = HeRu::GetCentral26TonesRus (bw, *ruTypeSet.begin ());
    }

  NS_ASSERT (ruTypeSet.size () == 1);
  ruSet = HeRu::GetRusOfType (bw, *ruTypeSet.begin ());

  auto ruSetIt = ruSet.begin ();
  auto central26TonesRusIt = central26TonesRus.begin ();

  for (const auto& userInfo : txVector.GetHeMuUserInfoMap ())
    {
      if (userInfo.second.ru.ruType == *ruTypeSet.begin ())
        {
          NS_ASSERT (ruSetIt != ruSet.end ());
          txVector.SetRu (*ruSetIt, userInfo.first);
          ruSetIt++;
        }
      else
        {
          NS_ASSERT (central26TonesRusIt != central26TonesRus.end ());
          txVector.SetRu (*central26TonesRusIt, userInfo.first);
          central26TonesRusIt++;
        }
    }

}

MultiUserScheduler::UlMuInfo
RrMultiUserScheduler::ComputeUlMuInfo (void)
{
  // std::cout<< "At time "<<Simulator::Now().GetMicroSeconds()<<" Computing UL Info\n";
   std::cout << "At time "<<Simulator::Now().GetMicroSeconds()<<" setting trigger for sending UL allocation info to stations"<< std::endl;
  return UlMuInfo {m_trigger, m_tbPpduDuration, std::move (m_txParams)};
}

} //namespace ns3
