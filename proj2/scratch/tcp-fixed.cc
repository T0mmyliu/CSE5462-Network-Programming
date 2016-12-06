
#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << Simulator::Now ().GetSeconds () << " [node " << m_node->GetId () << "] "; }

#include "tcp-fixed.h"
#include "ns3/log.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/simulator.h"
#include "ns3/abort.h"
#include "ns3/node.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpFixed");

NS_OBJECT_ENSURE_REGISTERED (TcpFixed);
static const int FIXED_WINDOW_SIZE = 53600;

TypeId
TcpFixed::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpFixed")
    .SetParent<TcpSocketBase> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpFixed> ()
    .AddAttribute ("ReTxThreshold", "Threshold for fast retransmit",
                    UintegerValue (3),
                    MakeUintegerAccessor (&TcpFixed::m_retxThresh),
                    MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("LimitedTransmit", "Enable limited transmit",
                   BooleanValue (false),
                   MakeBooleanAccessor (&TcpFixed::m_limitedTx),
                   MakeBooleanChecker ())
 ;
  return tid;
}

TcpFixed::TcpFixed (void)
  : m_retxThresh (3), // mute valgrind, actual value set by the attribute system
    m_inFastRec (false),
    m_limitedTx (false) // mute valgrind, actual value set by the attribute system
{
  NS_LOG_FUNCTION (this);
}

TcpFixed::TcpFixed (const TcpFixed& sock)
  : TcpSocketBase (sock),
    m_retxThresh (sock.m_retxThresh),
    m_inFastRec (false),
    m_limitedTx (sock.m_limitedTx)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
}

TcpFixed::~TcpFixed (void)
{
}

Ptr<TcpSocketBase>
TcpFixed::Fork (void)
{
  return CopyObject<TcpFixed> (this);
}

/* New ACK (up to seqnum seq) received. Increase cwnd and call TcpSocketBase::NewAck() */
void
TcpFixed::NewAck (const SequenceNumber32& seq)
{
  NS_LOG_FUNCTION (this << seq);
  NS_LOG_LOGIC ("TcpFixed received ACK for seq " << seq <<
                " cwnd " << m_cWnd <<
                " ssthresh " << m_ssThresh);

  // Check for exit condition of fast recovery
  if (m_inFastRec && seq < m_recover)
    { // Partial ACK, partial window deflation (RFC2582 sec.3 bullet #5 paragraph 3)
      // m_cWnd += m_segmentSize - (seq - m_txBuffer->HeadSequence ());
      m_cWnd = FIXED_WINDOW_SIZE;
      NS_LOG_INFO ("Partial ACK for seq " << seq << " in fast recovery: cwnd set to " << m_cWnd);
      m_txBuffer->DiscardUpTo(seq);  //Bug 1850:  retransmit before newack
      DoRetransmit (); // Assume the next seq is lost. Retransmit lost packet
      TcpSocketBase::NewAck (seq); // update m_nextTxSequence and send new data if allowed by window
      return;
    }
  else if (m_inFastRec && seq >= m_recover)
    { // Full ACK (RFC2582 sec.3 bullet #5 paragraph 2, option 1)
      // m_cWnd = std::min (m_ssThresh.Get (), BytesInFlight () + m_segmentSize);
      m_cWnd = FIXED_WINDOW_SIZE;
      m_inFastRec = false;
      NS_LOG_INFO ("Received full ACK for seq " << seq <<". Leaving fast recovery with cwnd set to " << m_cWnd);
    }

  // Increase of cwnd based on current phase (slow start or congestion avoidance)
  if (m_cWnd < m_ssThresh)
    { // Slow start mode, add one segSize to cWnd. Default m_ssThresh is 65535. (RFC2001, sec.1)
      // m_cWnd += m_segmentSize;
      m_cWnd = FIXED_WINDOW_SIZE;
      NS_LOG_INFO ("In SlowStart, ACK of seq " << seq << "; update cwnd to " << m_cWnd << "; ssthresh " << m_ssThresh);
    }
  else
    { // Congestion avoidance mode, increase by (segSize*segSize)/cwnd. (RFC2581, sec.3.1)
      // To increase cwnd for one segSize per RTT, it should be (ackBytes*segSize)/cwnd
      double adder = static_cast<double> (m_segmentSize * m_segmentSize) / m_cWnd.Get ();
      adder = std::max (1.0, adder);
      // m_cWnd += static_cast<uint32_t> (adder);
      m_cWnd = FIXED_WINDOW_SIZE;
      NS_LOG_INFO ("In CongAvoid, updated to cwnd " << m_cWnd << " ssthresh " << m_ssThresh);
    }

  // Complete newAck processing
  TcpSocketBase::NewAck (seq);
}

/* Cut cwnd and enter fast recovery mode upon triple dupack */
void
TcpFixed::DupAck (const TcpHeader& t, uint32_t count)
{
  NS_LOG_FUNCTION (this << count);
  if (count == m_retxThresh && !m_inFastRec)
    { // triple duplicate ack triggers fast retransmit (RFC2582 sec.3 bullet #1)
      m_ssThresh = std::max (2 * m_segmentSize, BytesInFlight () / 2);
      // m_cWnd = m_ssThresh + 3 * m_segmentSize;
      m_cWnd = FIXED_WINDOW_SIZE;
      m_recover = m_highTxMark;
      m_inFastRec = true;
      NS_LOG_INFO ("Triple dupack. Enter fast recovery mode. Reset cwnd to " << m_cWnd <<
                   ", ssthresh to " << m_ssThresh << " at fast recovery seqnum " << m_recover);
      DoRetransmit ();
    }
  else if (m_inFastRec)
    { // Increase cwnd for every additional dupack (RFC2582, sec.3 bullet #3)
      // m_cWnd += m_segmentSize;
      m_cWnd = FIXED_WINDOW_SIZE;
      NS_LOG_INFO ("Dupack in fast recovery mode. Increase cwnd to " << m_cWnd);
      if (!m_sendPendingDataEvent.IsRunning ())
        {
          SendPendingData (m_connected);
        }
    }
  else if (!m_inFastRec && m_limitedTx && m_txBuffer->SizeFromSequence (m_nextTxSequence) > 0)
    { // RFC3042 Limited transmit: Send a new packet for each duplicated ACK before fast retransmit
      NS_LOG_INFO ("Limited transmit");
      uint32_t sz = SendDataPacket (m_nextTxSequence, m_segmentSize, true);
      m_nextTxSequence += sz;                    // Advance next tx sequence
    };
}

/* Retransmit timeout */
void
TcpFixed::Retransmit (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  m_inFastRec = false;

  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT) return;
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence () >= m_highTxMark) return;

  // According to RFC2581 sec.3.1, upon RTO, ssthresh is set to half of flight
  // size and cwnd is set to 1*MSS, then the lost packet is retransmitted and
  // TCP back to slow start
  m_ssThresh = std::max (2 * m_segmentSize, BytesInFlight () / 2);
  // m_cWnd = m_segmentSize;
  m_cWnd = FIXED_WINDOW_SIZE;
  m_nextTxSequence = m_txBuffer->HeadSequence (); // Restart from highest Ack
  NS_LOG_INFO ("RTO. Reset cwnd to " << m_cWnd <<
               ", ssthresh to " << m_ssThresh << ", restart from seqnum " << m_nextTxSequence);
  DoRetransmit ();                          // Retransmit the packet
}

} // namespace ns3
