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


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpBulkSendExample");

int
main(int argc, char *argv[])
{
	//Send 1 MB of data
	uint32_t maxBytes = 1000000;
	
	Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpFixed"));

	NS_LOG_INFO("Create nodes.");
	NodeContainer nodes;
	nodes.Create(4);

	NS_LOG_INFO("Create channels.");

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

	devicesAB.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));
	devicesBC.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emBC));
	devicesCD.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (emABCD));

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

	NS_LOG_INFO("Create Applications.");

	uint16_t port = 9;  // well-known echo port number


	BulkSendHelper source("ns3::TcpSocketFactory",
		InetSocketAddress(sinkinterfaces.GetAddress(1), port));
	source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
	ApplicationContainer sourceApps = source.Install(nodes.Get(0));

	sourceApps.Start(Seconds(0.0));
	sourceApps.Stop(Seconds(10.0));

	PacketSinkHelper sink("ns3::TcpSocketFactory",
		InetSocketAddress(Ipv4Address::GetAny(), port));

	ApplicationContainer sinkApps = sink.Install(nodes.Get(3));
	sinkApps.Start(Seconds(0.0));
	sinkApps.Stop(Seconds(100.0));

	AsciiTraceHelper ascii;
	pointToPoint.EnableAsciiAll (ascii.CreateFileStream ("proj2-part4.tr"));
	pointToPoint.EnablePcapAll ("proj2-part4", false);
      
  	Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
	  
	NS_LOG_INFO("Run Simulation.");
	Simulator::Stop(Seconds(100.0));
	Simulator::Run();
	Simulator::Destroy();
	NS_LOG_INFO("Done.");
}
