/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
*/

// Network topology
//
//       n0 ----------- n1
//            500 Kbps
//             5 ms
//
// - Flow from n0 to n1 using BulkSendApplication.
// - Tracing of queues and packet receptions to file "tcp-bulk-send.tr"
//   and pcap tracing available when tracing is turned on.

#include <string>
#include <fstream>
#include <list>
#include <algorithm>
#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/packet-sink.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"
#include "tcp-fixed.h"
// shitty global list because I'm a terrible programmer
// this keeps track of all sequence numbers sent during simualtion so we can check for duplicates
std::list<uint32_t> seqNums;

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpBulkSendExample");

// Call back function for sequence number change
static void 
SeqChange (Ptr<OutputStreamWrapper> stream, ns3::SequenceNumber< uint32_t, int32_t > oldSeq, ns3::SequenceNumber< uint32_t, int32_t > newSeq) 
{
  // add new seq num to list
  //seqNums.push_front(oldSeq.GetValue());
  
  // returns true if new seq num is in list (i.e. sequence number has been retransmitted
  //bool found = (std::find(seqNums.begin(), seqNums.end(), newSeq.GetValue()) != seqNums.end());
  
  //if (found) {
	// if retransmitted, print seq number in third column so gnuplot will print it a different color
	*stream->GetStream () << Simulator::Now ().GetSeconds () /*<< " " << oldSeq*/ << " " << newSeq << /*" " << newSeq <<*/ std::endl;
  //} else {
	// if not retransmitted print NaN in third column so gnuplot will ignore it
  //	*stream->GetStream () << Simulator::Now ().GetSeconds () /*<< " " << oldSeq*/ << " " << newSeq << " NaN" << std::endl;
  //}
}

static void
TraceSeq ()
{
  // Trace changes to the congestion window
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("proj2-part4-seq.dat");
  Config::ConnectWithoutContext ("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/NextTxSequence", MakeBoundCallback (&SeqChange,stream));
}

int
main(int argc, char *argv[])
{
	//Send 1 MB of data
	uint32_t maxBytes = 1000000;
	
	// Ensure we are using TCPFixed
	Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpFixed"));

	//
	// Explicitly create the nodes required by the topology (shown above).
	//
	NS_LOG_INFO("Create nodes.");
	NodeContainer nodes;
	nodes.Create(4);

	NS_LOG_INFO("Create channels.");

	//
	// Explicitly create the point-to-point link required by the topology (shown above).
	//
	PointToPointHelper pointToPoint;
	pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
	pointToPoint.SetChannelAttribute("Delay", StringValue("10ms"));

  	NetDeviceContainer devicesAB, devicesBC, devicesCD;
 	devicesAB = pointToPoint.Install(nodes.Get(0), nodes.Get(1));
  	devicesBC = pointToPoint.Install (nodes.Get(1), nodes.Get(2));
  	devicesCD = pointToPoint.Install (nodes.Get(2), nodes.Get (3));

	Ptr<RateErrorModel> emABCD = CreateObject<RateErrorModel> ();
	emABCD->SetAttribute ("ErrorRate", DoubleValue (0.000001));
	
	Ptr<RateErrorModel> emBC = CreateObject<RateErrorModel> ();
	emBC->SetAttribute ("ErrorRate", DoubleValue (0.00005));

	// BER OF A-B
	devicesAB.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));
	
	// BER OF B-C
	devicesBC.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emBC));
	
	// BER OF C-D
	devicesCD.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));

	//
	// Install the internet stack on the nodes
	//
	InternetStackHelper internet;
	internet.Install(nodes);

	//
	// We've got the "hardware" in place.  Now we need to add IP addresses.
	//
	NS_LOG_INFO("Assign IP Addresses.");
	Ipv4AddressHelper ipv4;
	ipv4.SetBase ("10.1.1.0", "255.255.255.0");
	Ipv4InterfaceContainer appinterfaces = ipv4.Assign (devicesAB);
	ipv4.SetBase ("10.1.2.0", "255.255.255.0");
	ipv4.Assign (devicesBC);
	ipv4.SetBase ("10.1.3.0", "255.255.255.0");
	Ipv4InterfaceContainer sinkinterfaces = ipv4.Assign (devicesCD);

	NS_LOG_INFO("Create Applications.");

	//
	// Create a BulkSendApplication and install it on node 0
	//
	uint16_t port = 9;  // well-known echo port number


	BulkSendHelper source("ns3::TcpSocketFactory",
		InetSocketAddress(sinkinterfaces.GetAddress(1), port));
	// Set the amount of data to send in bytes.  Zero is unlimited.
	source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
	ApplicationContainer sourceApps = source.Install(nodes.Get(0));

	sourceApps.Start(Seconds(0.0));
	sourceApps.Stop(Seconds(10.0));
	//
	// Create a PacketSinkApplication and install it on node 1
	//
	PacketSinkHelper sink("ns3::TcpSocketFactory",
		InetSocketAddress(Ipv4Address::GetAny(), port));

	ApplicationContainer sinkApps = sink.Install(nodes.Get(3));
	sinkApps.Start(Seconds(0.0));
	sinkApps.Stop(Seconds(100.0));

	// Set up tracing
	AsciiTraceHelper ascii;
	pointToPoint.EnableAsciiAll (ascii.CreateFileStream ("proj2-part4.tr"));
	pointToPoint.EnablePcapAll ("proj2-part4", false);
	// Setup tracing for cwnd
	Simulator::Schedule(Seconds(0.00001),&TraceSeq);
      
  	// Set up inter-network routing
  	Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
	  
	//
	// Now, do the actual simulation.
	//
	NS_LOG_INFO("Run Simulation.");
	Simulator::Stop(Seconds(100.0));
	Simulator::Run();
	Simulator::Destroy();
	NS_LOG_INFO("Done.");
}
