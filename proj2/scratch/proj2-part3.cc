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


std::list<uint32_t> seqNums;

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpBulkSend Rerouting Example");

int
main(int argc, char *argv[])
{
	uint32_t maxBytes = 1000000;
	Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
  	Config::SetDefault ("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue (true));
	Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1 << 22));
  	Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1 << 22));

	NS_LOG_INFO("Create nodes.");
	NodeContainer nodes;
	nodes.Create(7);

  	NS_LOG_INFO("Create channels.");
	PointToPointHelper pointToPoint;
	pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
	pointToPoint.SetChannelAttribute("Delay", StringValue("10ms"));

  	NetDeviceContainer devicesAB, devicesBC, devicesCD, devicesAE, devicesEF, devicesFG, devicesGD;
 	devicesAB = pointToPoint.Install(nodes.Get(0), nodes.Get(1));
  	devicesBC = pointToPoint.Install (nodes.Get(1), nodes.Get(2));
  	devicesCD = pointToPoint.Install (nodes.Get(2), nodes.Get (3));
  	devicesAE = pointToPoint.Install (nodes.Get (0), nodes.Get (4));
  	devicesEF = pointToPoint.Install (nodes.Get (4), nodes.Get(5));
  	devicesFG = pointToPoint.Install (nodes.Get (5), nodes.Get (6));
  	devicesGD = pointToPoint.Install (nodes.Get (6), nodes.Get (3));

	Ptr<RateErrorModel> emABCD = CreateObject<RateErrorModel> ();
	emABCD->SetAttribute ("ErrorRate", DoubleValue (0.000001));
	
	Ptr<RateErrorModel> emBC = CreateObject<RateErrorModel> ();
	emBC->SetAttribute ("ErrorRate", DoubleValue (0.00005));

	devicesAB.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));
	devicesBC.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emBC));	
	devicesCD.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));  
  	devicesAE.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));
  	devicesEF.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));
  	devicesFG.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));
  	devicesGD.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));

  	NS_LOG_INFO("Install internet stack.");
	InternetStackHelper internet;
	internet.Install(nodes);

	NS_LOG_INFO("Assign IP Addresses.");
	Ipv4AddressHelper ipv4;
	ipv4.SetBase ("10.1.1.0", "255.255.255.0");
	Ipv4InterfaceContainer appinterfaces = ipv4.Assign (devicesAB);
	ipv4.SetBase ("10.1.2.0", "255.255.255.0");
	ipv4.Assign (devicesBC);
	ipv4.SetBase ("10.1.3.0", "255.255.255.0");
	Ipv4InterfaceContainer sinkinterfaces = ipv4.Assign (devicesCD);
  	ipv4.SetBase ("10.1.4.0", "255.255.255.0");
	ipv4.Assign (devicesAE);
  	ipv4.SetBase ("10.1.5.0", "255.255.255.0");
	ipv4.Assign (devicesEF);
  	ipv4.SetBase ("10.1.6.0", "255.255.255.0");
	ipv4.Assign (devicesFG);
  	ipv4.SetBase ("10.1.7.0", "255.255.255.0");
	ipv4.Assign (devicesGD);
  
  	Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

	NS_LOG_INFO("Create Applications.");

	uint16_t port = 9;  // well-known echo port number

	BulkSendHelper source("ns3::TcpSocketFactory",
	InetSocketAddress(sinkinterfaces.GetAddress(1), port));
	source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
	ApplicationContainer sourceApps = source.Install(nodes.Get(0));

	sourceApps.Start(Seconds(0.0));
	sourceApps.Stop(Seconds(100.0));
	PacketSinkHelper sink("ns3::TcpSocketFactory",
	InetSocketAddress(Ipv4Address::GetAny(), port));

	ApplicationContainer sinkApps = sink.Install(nodes.Get(3));
	sinkApps.Start(Seconds(0.0));
	sinkApps.Stop(Seconds(100.0));

  	NS_LOG_INFO("Link Breakage.");
  
  	Ptr<Node> nb = nodes.Get (1);
  	Ptr<Ipv4> ipv4b = nb->GetObject<Ipv4> ();
  	uint32_t ipv4ifIndexb = 2; 
  	Simulator::Schedule (Seconds (2), &Ipv4::SetDown, ipv4b, ipv4ifIndexb);

  	NS_LOG_INFO("Tracing Configuration.");
  
	AsciiTraceHelper ascii;
  	Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream ("proj2-part3.tr");
	pointToPoint.EnableAsciiAll (stream);
	pointToPoint.EnablePcapAll ("proj2-part3", false);
  	internet.EnableAsciiIpv4All (stream);
  
  	Ipv4GlobalRoutingHelper g;
  	Ptr<OutputStreamWrapper> routingStream1 = Create<OutputStreamWrapper> ("proj2-part3-1.routes", std::ios::out);
	Ptr<OutputStreamWrapper> routingStream2 = Create<OutputStreamWrapper> ("proj2-part3-2.routes", std::ios::out);
        g.PrintRoutingTableAllAt (Seconds (1), routingStream1);
	g.PrintRoutingTableAllAt (Seconds (10), routingStream2);
 	
	NS_LOG_INFO("Run Simulation.");
	Simulator::Stop(Seconds(100.0));
	Simulator::Run();
	Simulator::Destroy();
	NS_LOG_INFO("Done.");
}
