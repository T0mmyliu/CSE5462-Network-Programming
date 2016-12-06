 #ifndef TCP_FIXED_H
 #define TCP_FIXED_H
  
 #include "ns3/tcp-socket-base.h"
 
 namespace ns3 {
  
 /**
 * \ingroup socket
 * \ingroup tcp
 *
 * \brief An implementation of a stream socket using TCP.
 *
 * This class contains the NewReno implementation of TCP, as of \RFC{2582}.
 */
 class TcpFixed : public TcpSocketBase
 {
 public:
 /**
 * \brief Get the type ID.
 * \return the object TypeId
 */
 static TypeId GetTypeId (void);
 /**
 * Create an unbound tcp socket.
 */
 TcpFixed (void);
 /**
 * \brief Copy constructor
 * \param sock the object to copy
 */
 TcpFixed (const TcpFixed& sock);
 virtual ~TcpFixed (void);
 
 protected:
 virtual Ptr<TcpSocketBase> Fork (void); // Call CopyObject<TcpNewReno> to clone me
 virtual void NewAck (SequenceNumber32 const& seq); // Inc cwnd and call NewAck() of parent
 virtual void DupAck (const TcpHeader& t, uint32_t count);  // Halving cwnd and reset nextTxSequence
 virtual void Retransmit (void); // Exit fast recovery upon retransmit timeout
 
 protected:
 SequenceNumber32       m_recover;      //!< Previous highest Tx seqnum for fast recovery
 uint32_t               m_retxThresh;   //!< Fast Retransmit threshold
 bool                   m_inFastRec;    //!< currently in fast recovery
 bool                   m_limitedTx;    //!< perform limited transmit
 };
  
 } // namespace ns3
  
 #endif /* TCP_NEWRENO_H */
