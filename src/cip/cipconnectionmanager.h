/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved. 
 *
 * Contributors:
 *     <date>: <author>, <author email> - changes
 ******************************************************************************/
#ifndef CIPCONNECTIONMANAGER_H_
#define CIPCONNECTIONMANAGER_H_

#include "opener_user_conf.h"
#include "typedefs.h"
#include "ciptypes.h"

#define CONSUMING 0           /* these are used as array indexes, watch out if changing these values */
#define PRODUCING 1

/*! States of a connection */
typedef enum
{
  CONN_STATE_NONEXISTENT = 0,
  CONN_STATE_CONFIGURING = 1,
  CONN_STATE_WAITINGFORCONNECTIONID = 2 /* only used in DeviceNet*/,
  CONN_STATE_ESTABLISHED = 3,
  CONN_STATE_TIMEDOUT = 4,
  CONN_STATE_DEFERREDDELETE = 5 /* only used in DeviceNet */,
  CONN_STATE_CLOSING
} CONN_STATE;

/* instance_type attributes */
typedef enum
{
  enConnTypeExplicit         = 0,
  enConnTypeIOExclusiveOwner = 0x01,
  enConnTypeIOInputOnly      = 0x11,
  enConnTypeIOListenOnly     = 0x21
} EConnType;

/*! Possible values for the watch dog time out action of a connection */
typedef enum
{
  enWatchdogTransitionToTimedOut = 0, /*!< , invalid for explicit message connections */
  enWatchdogAutoDelete = 1, /*!< Default for explicit message connections, default for I/O connections on EIP*/
  enWatchdogAutoReset = 2, /*!< Invalid for explicit message connections */
  enWatchdogDeferredDelete = 3
/*!< Only valid for DeviceNet, invalid for I/O connections */
} EWatchdogTimeOutAction;

typedef struct
{
  CONN_STATE state;
  EIP_UINT16 ConnectionID;
/*TODO think if this is needed anymore
 TCMReceiveDataFunc m_ptfuncReceiveData; */
} S_Link_Consumer;

typedef struct
{
  CONN_STATE state;
  EIP_UINT16 ConnectionID;
} S_Link_Producer;

typedef struct
{
  S_Link_Consumer Consumer;
  S_Link_Producer Producer;
} S_Link_Object;

/*! The data needed for handling connections. This data is strongly related to
 * the connection object defined in the CIP-specification. However the full
 * functionality of the connection object is not implemented. Therefore this
 * data can not be accessed with CIP means.
 */
typedef struct CIP_ConnectionObject
{
  CONN_STATE State;
  EConnType m_eInstanceType;
  EIP_BYTE TransportClassTrigger;
  /* conditional
   UINT16 DeviceNetProductedConnectionID;
   UINT16 DeviceNetConsumedConnectionID;
   */
  EIP_BYTE DeviceNetInitialCommCharacteristics;
  EIP_UINT16 ProducedConnectionSize;
  EIP_UINT16 ConsumedConnectionSize;
  EIP_UINT16 ExpectedPacketRate;

  /*conditional*/
  EIP_UINT32 CIPProducedConnectionID;
  EIP_UINT32 CIPConsumedConnectionID;
  /**/
  EWatchdogTimeOutAction WatchdogTimeoutAction;
  EIP_UINT16 ProducedConnectionPathLength;
  S_CIP_EPATH ProducedConnectionPath;
  EIP_UINT16 ConsumedConnectionPathLength;
  S_CIP_EPATH ConsumedConnectionPath;
  /* conditional
   UINT16 ProductionInhibitTime;
   */
  /* non CIP Attributes, only relevant for opened connections */
  EIP_BYTE Priority_Timetick;
  EIP_UINT8 Timeoutticks;
  EIP_UINT16 ConnectionSerialNumber;
  EIP_UINT16 OriginatorVendorID;
  EIP_UINT32 OriginatorSerialNumber;
  EIP_UINT16 ConnectionTimeoutMultiplier;
  EIP_UINT32 O_to_T_RPI;
  EIP_UINT16 O_to_T_NetworkConnectionParameter;
  EIP_UINT32 T_to_O_RPI;
  EIP_UINT16 T_to_O_NetworkConnectionParameter;
  EIP_BYTE TransportTypeTrigger;
  EIP_UINT8 ConnectionPathSize;
  S_CIP_ElectronicKey ElectronicKey;
  S_CIP_ConnectionPath ConnectionPath; /* padded EPATH*/
  S_Link_Object stLinkObject;

  S_CIP_Instance *p_stConsumingInstance;
  /*S_CIP_CM_Object *p_stConsumingCMObject; */

  S_CIP_Instance *p_stProducingInstance;
  /*S_CIP_CM_Object *p_stProducingCMObject; */

  EIP_UINT32 EIPSequenceCountProducing; /* the EIP level sequence Count for Class 0/1 Producing Connections may have a different value than SequenceCountProducing*/
  EIP_UINT32 EIPSequenceCountConsuming; /* the EIP level sequence Count for Class 0/1 Producing Connections may have a different value than SequenceCountProducing*/

  EIP_UINT16 SequenceCountProducing; /* sequence Count for Class 1 Producing Connections*/
  EIP_UINT16 SequenceCountConsuming; /* sequence Count for Class 1 Producing Connections*/

  EIP_INT32 TransmissionTriggerTimer;
  EIP_INT32 InnacitvityWatchdogTimer;
  struct sockaddr_in remote_addr; /* socket address for produce */
  int sockfd[2]; /* socket handles, indexed by CONSUMING or PRODUCING */

  /* pointers to be used in the active connection list */
  struct CIP_ConnectionObject *m_pstNext;
  struct CIP_ConnectionObject *m_pstFirst;
} S_CIP_ConnectionObject;

#define CIP_CONNECTION_MANAGER_CLASS_CODE 0x06

/* public functions */

/*! Initialize the data of the connection manager object
 */
EIP_STATUS
Connection_Manager_Init(void);

/*!  Get a connected object dependent on requested ConnectionID.
 *   The returned connection may not be in established state. The user has to check
 *   this!   
 *   @param ConnectionID  requested ConnectionID of opened connection
 *   @return pointer to connected Object
 *           0 .. connection not present in device
 */
S_CIP_ConnectionObject *
getConnectedObject(EIP_UINT32 ConnectionID);

/*! Copy the given connection data from pa_pstSrc to pa_pstDst
 */
void
copyConnectionData(S_CIP_ConnectionObject *pa_pstDst,
    S_CIP_ConnectionObject *pa_pstSrc);


#endif /*CIPCONNECTIONMANAGER_H_*/

