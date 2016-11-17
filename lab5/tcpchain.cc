#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include <string.h>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SixthScriptExample");

class MyApp : public Application
{
public:
  MyApp ();
  virtual ~MyApp ();

  static TypeId GetTypeId (void);
void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0),
    m_peer (),
    m_packetSize (0),
    m_nPackets (0),
    m_dataRate (0),
    m_sendEvent (),
    m_running (false),
    m_packetsSent (0)
{
}

MyApp::~MyApp ()
{
  m_socket = 0;
}

/* static */
TypeId MyApp::GetTypeId (void)
{
  static TypeId tid = TypeId ("MyApp")
    .SetParent<Application> ()
    .SetGroupName ("Tutorial")
    .AddConstructor<MyApp> ()
    ;
  return tid;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);

  if (++m_packetsSent < m_nPackets)
    {
      ScheduleTx ();
    }
}

void
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}

static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  NS_LOG_UNCOND (Simulator::Now ().GetSeconds () << "\t" << newCwnd);
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
RxDrop (Ptr<PcapFileWrapper> file, Ptr<const Packet> p)
{
  NS_LOG_UNCOND ("RxDrop at " << Simulator::Now ().GetSeconds ());
  file->Write (Simulator::Now (), p);
}

int
main (int argc, char *argv[])
{
  int NODE_NUM = 2;
  double bc_rate;
  char app_rate[10]={0};
  char pcap_name[10]={0};
  char window_name[10]={0};

  bc_rate=atof(argv[1]);
  strcpy(app_rate, argv[2]);
  strcpy(pcap_name, argv[3]);
  strcpy(window_name, argv[4]);

  NodeContainer nodes_ab;
  nodes_ab.Create (NODE_NUM);

  NodeContainer nodes_bc;
  nodes_bc.Add (nodes_ab.Get (1));
  nodes_bc.Create (NODE_NUM-1);

  NodeContainer nodes_cd;
  nodes_cd.Add (nodes_bc.Get (1));
  nodes_cd.Create (NODE_NUM-1);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices_ab, devices_bc, devices_cd;
  devices_ab = pointToPoint.Install (nodes_ab);
  devices_bc = pointToPoint.Install (nodes_bc);
  devices_cd = pointToPoint.Install (nodes_cd);

  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorRate", DoubleValue (0.000001));
  devices_ab.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
  devices_cd.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
  em->SetAttribute ("ErrorRate", DoubleValue (bc_rate));
  devices_bc.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));

  InternetStackHelper stack;
  stack.Install (nodes_ab.Get (0));
  stack.Install (nodes_bc.Get (0));
  stack.Install (nodes_cd);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces_ab = address.Assign (devices_ab);
  
  address.SetBase ("10.1.2.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces_bc = address.Assign (devices_bc);
  
  address.SetBase ("10.1.3.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces_cd = address.Assign (devices_cd);

  uint16_t sinkPort = 8080;
  Address sinkAddress (InetSocketAddress (interfaces_cd.GetAddress (1), sinkPort)); //
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes_cd.Get (1)); //
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (20.));

  Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (nodes_ab.Get (0), TcpSocketFactory::GetTypeId ());

  Ptr<MyApp> app = CreateObject<MyApp> ();
  app->Setup (ns3TcpSocket, sinkAddress, 1040, 1000, DataRate (app_rate));
  nodes_ab.Get (0)->AddApplication (app);
  app->SetStartTime (Seconds (1.));
  app->SetStopTime (Seconds (20.));

  AsciiTraceHelper asciiTraceHelper;
  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (window_name);
  ns3TcpSocket->TraceConnectWithoutContext ("CongestionWindow", MakeBoundCallback (&CwndChange, stream));

  PcapHelper pcapHelper;
  Ptr<PcapFileWrapper> file = pcapHelper.CreateFile (pcap_name, std::ios::out, PcapHelper::DLT_PPP);
  devices_cd.Get (1)->TraceConnectWithoutContext ("PhyRxDrop", MakeBoundCallback (&RxDrop, file)); //

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  pointToPoint.EnablePcapAll (pcap_name);

  Simulator::Stop (Seconds (20));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}

