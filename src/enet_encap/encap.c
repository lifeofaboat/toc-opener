/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved. 
 *
 * Contributors:
 *     <date>: <author>, <author email> - changes
 ******************************************************************************/
#include <string.h>
#include "opener_api.h"
#include "cpf.h"
#include "encap.h"
#include "endianconv.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipidentity.h"

#define INVALID_SESSION -1

#define SENDER_CONTEXT_SIZE 8                   /*size of sender context in encapsulation header*/

/* definition of known encapsulation commands */
#define COMMAND_NOP                     0x0000
#define COMMAND_LISTSERVICES            0x0004
#define COMMAND_LISTIDENTITY            0x0063
#define COMMAND_LISTINTERFACES          0x0064
#define COMMAND_REGISTERSESSION         0x0065
#define COMMAND_UNREGISTERSESSION       0x0066
#define COMMAND_SENDRRDATA              0x006F
#define COMMAND_SENDUNITDATA            0x0070

/* definition of status codes in encapsulation protocol */
#define OPENER_ENCAP_STATUS_SUCCESS                     0x0000
#define OPENER_ENCAP_STATUS_INVALID_COMMAND             0x0001
#define OPENER_ENCAP_STATUS_INSUFFICIENT_MEM            0x0002
#define OPENER_ENCAP_STATUS_INCORRECT_DATA              0x0003
#define OPENER_ENCAP_STATUS_INVALID_SESSION_HANDLE      0x0064
#define OPENER_ENCAP_STATUS_INVALID_LENGTH              0x0065
#define OPENER_ENCAP_STATUS_UNSUPPORTED_PROTOCOL        0x0069  

/* definition of capability flags */
#define SUPPORT_CIP_TCP                 0x0020
#define SUPPORT_CIP_UDP_CLASS_0_OR_1    0x0100

/*Identity data from cipidentity.c*/
extern EIP_UINT16 VendorID;
extern EIP_UINT16 DeviceType;
extern EIP_UINT16 ProductCode;
extern S_CIP_Revision Revison;
extern EIP_UINT16 ID_Status;
extern EIP_UINT32 SerialNumber;
extern S_CIP_Short_String ProductName;

/*ip address data taken from TCPIPInterfaceObject*/
extern S_CIP_TCPIPNetworkInterfaceConfiguration Interface_Configuration;

/*** defines ***/

#define ITEM_ID_LISTIDENTITY 0x000C

#define SUPPORTED_PROTOCOL_VERSION 1

#define SUPPORTED_OPTIONS_MASK  0x00  /*Mask of which options are supported as of the current CIP specs no other option value as 0 should be supported.*/

#define ENCAPSULATION_HEADER_SESSION_HANDLE_POS 4   /*the position of the session handle within the encapsulation header*/

struct S_Encapsulation_Data g_sEncapData; /*buffer for the encapsulation data used for sending and receiving*/

/*struct S_Identity S_Identity_Object;
 const static UINT8 productname[] = "test device";
 */

struct S_Encapsulation_Interface_Information g_stInterfaceInformation;

int anRegisteredSessions[OPENER_NUMBER_OF_SUPPORTED_SESSIONS];

/*** private functions ***/
int
nop(void);
int
ListServices(struct S_Encapsulation_Data *pa_stReceiveData);
int
ListInterfaces(struct S_Encapsulation_Data *pa_stReceiveData);
int
ListIdentity(struct S_Encapsulation_Data *pa_stReceiveData);
int
RegisterSession(int pa_nSockfd, struct S_Encapsulation_Data *pa_stReceiveData);
int
UnregisterSession(struct S_Encapsulation_Data *pa_stReceiveData);
int
SendUnitData(struct S_Encapsulation_Data *pa_stReceiveData);
int
SendRRData(struct S_Encapsulation_Data *pa_stReceiveData);

int
getFreeSessionIndex(void);
EIP_INT16
createEncapsulationStructure(EIP_UINT8 * buf, int length,
    struct S_Encapsulation_Data *pa_S_ReceiveData);
EIP_STATUS
checkRegisteredSessions(struct S_Encapsulation_Data *pa_S_ReceiveData);
int
encapsulate_data(struct S_Encapsulation_Data *pa_S_SendData);

/*   void encapInit(void)
 *   initialize sessionlist and interfaceinformation.
 */

void
encapInit(void)
{
  int i;

  /* initialize Sessions to invalid == free session */
  for (i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; i++)
    {
      anRegisteredSessions[i] = EIP_INVALID_SOCKET;
    }

  /*TODO make the interface information configurable*/
  /* initialize interface information */
  g_stInterfaceInformation.TypeCode = CIP_ITEM_ID_LISTSERVICE_RESPONSE;
  g_stInterfaceInformation.Length = sizeof(g_stInterfaceInformation);
  g_stInterfaceInformation.EncapsulationProtocolVersion = 1;
  g_stInterfaceInformation.CapabilityFlags = SUPPORT_CIP_TCP
      | SUPPORT_CIP_UDP_CLASS_0_OR_1;
  strcpy((char *) g_stInterfaceInformation.NameofService, "communications");
}

/*   int handleReceivedExplictData(int pa_socket, EIP_UINT8* pa_buf, int pa_length. int *pa_nRemainingBytes)
 *   Read received bytes, copy to struct S_Encapsulation_data and handles the command.
 *      pa_socket	socket handle from which data is received.
 *      pa_buf		buffer to be read.
 *      pa_length	length of the data in pa_buf.
 *  return length of reply
 */
int
handleReceivedExplictData(int pa_socket, /* socket from which data was received*/
EIP_UINT8 * pa_buf, /* input buffer*/
int pa_length, /* length of input*/
int *pa_nRemainingBytes) /* return how many bytes of the input are left over after we're done here*/
{
  int nRetVal = 0;
  /* eat the encapsulation header*/
  /* the structure contains a pointer to the encapsulated data*/
  /* returns how many bytes are left after the encapsulated data*/
  *pa_nRemainingBytes = createEncapsulationStructure(pa_buf, pa_length,
      &g_sEncapData);

  if (SUPPORTED_OPTIONS_MASK == g_sEncapData.nOptions) /*TODO generate appropriate error response*/
    {
      if (*pa_nRemainingBytes >= 0) /* check if the message is corrupt: header size + claimed payload size > than what we actually received*/
        {
          /* full package or more received */
          g_sEncapData.nStatus = OPENER_ENCAP_STATUS_SUCCESS;
          /* each of these can return one of:
           reply size >0
           0 (no reply shall be sent)
           EIP_ERROR (i.e. -1) an error occurred, no reply shall be sent*/
          switch (g_sEncapData.nCommand_code)
            {
          case (COMMAND_NOP):
            nRetVal = nop();
            break;

          case (COMMAND_LISTSERVICES):
            nRetVal = ListServices(&g_sEncapData);
            break;

          case (COMMAND_LISTIDENTITY):
            nRetVal = ListIdentity(&g_sEncapData);
            break;

          case (COMMAND_LISTINTERFACES):
            nRetVal = ListInterfaces(&g_sEncapData);
            break;

          case (COMMAND_REGISTERSESSION):
            nRetVal = RegisterSession(pa_socket, &g_sEncapData);
            break;

          case (COMMAND_UNREGISTERSESSION):
            nRetVal = UnregisterSession(&g_sEncapData);
            break;

          case (COMMAND_SENDRRDATA):
            nRetVal = SendRRData(&g_sEncapData);
            break;

          case (COMMAND_SENDUNITDATA):
            nRetVal = SendUnitData(&g_sEncapData); /* not currently supported, SendUnitData is a stub*/
            break;
          default:
            g_sEncapData.nStatus = OPENER_ENCAP_STATUS_INVALID_COMMAND;
            g_sEncapData.nData_length = 0;
            nRetVal = encapsulate_data(&g_sEncapData);
            break;
            }
        }
    }

  return nRetVal;
}

/*   INT8 encapsulate_data(struct S_Encapsulation_Data *pa_stSendData)
 *   add encapsulation header and sender_context to data.
 *      pa_stSendData pointer to structure with header and datapointer.
 *  return size of reply
 */
int
encapsulate_data(struct S_Encapsulation_Data *pa_stSendData)
{
  pa_stSendData->pEncapsulation_Data = &g_acCommBuf[2];
  /*htols(pa_stSendData->nCommand_code, &pa_stSendData->pEncapsulation_Data);*/
  htols(pa_stSendData->nData_length, &pa_stSendData->pEncapsulation_Data);
  /*the g_acCommBuf should already contain the correct session handle*/
  /*htoll(pa_stSendData->nSession_handle, &pa_stSendData->pEncapsulation_Data); */
  pa_stSendData->pEncapsulation_Data += 4;
  htoll(pa_stSendData->nStatus, &pa_stSendData->pEncapsulation_Data);
  /*the g_acCommBuf should already contain the correct sender context*/
  /*memcpy(pa_stSendData->pEncapsulation_Data, pa_stSendData->anSender_context, SENDER_CONTEXT_SIZE);*/
  pa_stSendData->pEncapsulation_Data += SENDER_CONTEXT_SIZE + 2; /* the plus 2 is for the options value*/
  /*the g_acCommBuf should already contain the correct  options value*/
  /*htols((EIP_UINT16)pa_stSendData->nOptions, &pa_stSendData->pEncapsulation_Data);*/

  return ENCAPSULATION_HEADER_LENGTH + pa_stSendData->nData_length;
}

/*   INT8 nop()
 *   do nothing, only implemented for CIP conformity.
 */
int
nop(void)
{
  return 0;
}

/*   INT8 ListServices(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   generate reply with "Communications Services" + compatibility Flags.
 *      pa_S_ReceiveData pointer to structure with received data
 */
int
ListServices(struct S_Encapsulation_Data *pa_stReceiveData)
{
  EIP_UINT8 *pacCommBuf = pa_stReceiveData->pEncapsulation_Data;

  pa_stReceiveData->nData_length = g_stInterfaceInformation.Length + 2;

  /* copy Interface data to nmsg for sending */
  htols(1, &pacCommBuf);
  htols(g_stInterfaceInformation.TypeCode, &pacCommBuf);
  htols((EIP_UINT16) (g_stInterfaceInformation.Length - 4), &pacCommBuf);
  htols(g_stInterfaceInformation.EncapsulationProtocolVersion, &pacCommBuf);
  htols(g_stInterfaceInformation.CapabilityFlags, &pacCommBuf);
  memcpy(pacCommBuf, g_stInterfaceInformation.NameofService,
      sizeof(g_stInterfaceInformation.NameofService));

  return encapsulate_data(pa_stReceiveData); /* encapsulate the data from structure and send to originator a reply */
}

int
ListInterfaces(struct S_Encapsulation_Data *pa_stReceiveData)
{
  EIP_UINT8 *pacCommBuf = pa_stReceiveData->pEncapsulation_Data;
  pa_stReceiveData->nData_length = 2;
  htols(0x0000, &pacCommBuf); /* copy Interface data to nmsg for sending */

  return encapsulate_data(pa_stReceiveData); /* encapsulate the data from structure and send to originator a reply */
}

/*   INT8 ListIdentity(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   send Get_Attribute_All to Identity Object and send data + sender_context back.
 *      pa_S_ReceiveData pointer to structure with received data
 *  return status
 * 			0 .. success
 */
int
ListIdentity(struct S_Encapsulation_Data * pa_stReceiveData)
{
  /* List Identity reply according to EIP/CIP Specification */
  EIP_UINT8 *pacCommBuf = pa_stReceiveData->pEncapsulation_Data;
  EIP_BYTE *acIdLenBuf;

  htols(1, &pacCommBuf); /* one item */
  htols(ITEM_ID_LISTIDENTITY, &pacCommBuf);

  acIdLenBuf = pacCommBuf;
  pacCommBuf += 2; /*at this place the real length will be inserted below*/

  htols(SUPPORTED_PROTOCOL_VERSION, &pacCommBuf);
  htols(htons(AF_INET), &pacCommBuf);
  htols(htons(OPENER_ETHERNET_PORT), &pacCommBuf);
  htoll(Interface_Configuration.IPAddress, &pacCommBuf);
  memset(pacCommBuf, 0, 8);
  pacCommBuf += 8;

  htols(VendorID, &pacCommBuf);
  htols(DeviceType, &pacCommBuf);
  htols(ProductCode, &pacCommBuf);
  *(pacCommBuf)++ = Revison.MajorRevision;
  *(pacCommBuf)++ = Revison.MinorRevision;
  htols(ID_Status, &pacCommBuf);
  htoll(SerialNumber, &pacCommBuf);
  *pacCommBuf++ = (unsigned char) ProductName.Length;
  memcpy(pacCommBuf, ProductName.String, ProductName.Length);
  pacCommBuf += ProductName.Length;
  *pacCommBuf++ = 0xFF;

  pa_stReceiveData->nData_length = pacCommBuf
      - &g_acCommBuf[ENCAPSULATION_HEADER_LENGTH];
  htols(pacCommBuf - acIdLenBuf - 2, &acIdLenBuf); /* the -2 is for not counting the length field*/

  return encapsulate_data(pa_stReceiveData);
}

/*   INT8 RegisterSession(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   Check supported protocol, generate session handle, send replay back to originator.
 *      pa_nSockfd       socket this request is associated to. Needed for double register check
 *      pa_S_ReceiveData pointer to received data with request/response.
 *  return status
 * 			0 .. success
 */
int
RegisterSession(int pa_nSockfd, struct S_Encapsulation_Data * pa_stReceiveData)
{
  int i;
  int nSessionIndex = 0;
  EIP_UINT8 *pacBuf;
  EIP_UINT16 nProtocolVersion = ltohs(&pa_stReceiveData->pEncapsulation_Data);
  EIP_UINT16 nOptionFlag = ltohs(&pa_stReceiveData->pEncapsulation_Data);

  /* check if requested protocol version is supported and the register session option flag is zero*/
  if ((0 < nProtocolVersion)
      && (nProtocolVersion <= SUPPORTED_PROTOCOL_VERSION) && (0 == nOptionFlag))
    { /*Option field should be zero*/
      /* check if the socket has already a session open */
      for (i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; ++i)
        {
          if (anRegisteredSessions[i] == pa_nSockfd)
            {
              /* the socket has already registered a session this is not allowed*/
              pa_stReceiveData->nSession_handle = i + 1; /*return the already assigned session back, the cip spec is not clear about this needs to be tested*/
              pa_stReceiveData->nStatus
                  = OPENER_ENCAP_STATUS_UNSUPPORTED_PROTOCOL;
              nSessionIndex = INVALID_SESSION;
              pacBuf = &g_acCommBuf[ENCAPSULATION_HEADER_SESSION_HANDLE_POS];
              htoll(pa_stReceiveData->nSession_handle, &pacBuf); /*encapsulate_data will not update the session handle so we have to do it here by hand*/
              break;
            }
        }

      if (INVALID_SESSION != nSessionIndex)
        {
          nSessionIndex = getFreeSessionIndex();
          if (INVALID_SESSION == nSessionIndex) /* no more sessions available */
            {
              pa_stReceiveData->nStatus = OPENER_ENCAP_STATUS_INSUFFICIENT_MEM;
            }
          else
            { /* successful session registered */
              anRegisteredSessions[nSessionIndex] = pa_nSockfd; /* store associated socket */
              pa_stReceiveData->nSession_handle = nSessionIndex + 1;
              pa_stReceiveData->nStatus = OPENER_ENCAP_STATUS_SUCCESS;
              pacBuf = &g_acCommBuf[ENCAPSULATION_HEADER_SESSION_HANDLE_POS];
              htoll(pa_stReceiveData->nSession_handle, &pacBuf); /*encapsulate_data will not update the session handle so we have to do it here by hand*/
            }
        }
    }
  else
    { /* protocol not supported */
      pa_stReceiveData->nStatus = OPENER_ENCAP_STATUS_UNSUPPORTED_PROTOCOL;
    }

  pa_stReceiveData->nData_length = 4;

  return encapsulate_data(pa_stReceiveData);
}

/*   INT8 UnregisterSession(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   close all corresponding TCP connections and delete session handle.
 *      pa_S_ReceiveData pointer to unregister session request with corresponding socket handle.
 *  return status
 * 			0.. success
 */
int
UnregisterSession(struct S_Encapsulation_Data * pa_stReceiveData)
{
  int i;

  if ((0 < pa_stReceiveData->nSession_handle)
      && (pa_stReceiveData->nSession_handle
          <= OPENER_NUMBER_OF_SUPPORTED_SESSIONS))
    {
      i = pa_stReceiveData->nSession_handle - 1;
      if (EIP_INVALID_SOCKET != anRegisteredSessions[i])
        {
          IApp_CloseSocket(anRegisteredSessions[i]);
          anRegisteredSessions[i] = EIP_INVALID_SOCKET;
          return EIP_OK;
        }
    }

  /* no such session registered */
  pa_stReceiveData->nData_length = 0;
  pa_stReceiveData->nStatus = OPENER_ENCAP_STATUS_INVALID_SESSION_HANDLE;
  return encapsulate_data(pa_stReceiveData);
}

/*   INT8 SendUnitData(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   Call Connection Manager.
 *      pa_S_ReceiveData pointer to structure with data and header information.
 *  return status 	0 .. success.
 * 					-1 .. error
 */
int
SendUnitData(struct S_Encapsulation_Data * pa_stReceiveData)
{
  EIP_INT16 nSendSize;
  /* Command specific data UDINT .. Interface Handle, UINT .. Timeout, CPF packets */
  /* don't use the data yet */
  ltohl(&pa_stReceiveData->pEncapsulation_Data); /* skip over null interface handle*/
  ltohs(&pa_stReceiveData->pEncapsulation_Data); /* skip over unused timeout value*/
  pa_stReceiveData->nData_length -= 6; /* the rest is in CPF format*/

  if (checkRegisteredSessions(pa_stReceiveData) == EIP_ERROR) /* see if the EIP session is registered*/
    { /* received a package with non registered session handle */
      pa_stReceiveData->nData_length = 0;
      pa_stReceiveData->nStatus = OPENER_ENCAP_STATUS_INVALID_SESSION_HANDLE;
      return encapsulate_data(pa_stReceiveData);
    }

  nSendSize
      = notifyConnectedCPF(pa_stReceiveData->pEncapsulation_Data,
          pa_stReceiveData->nData_length,
          &g_acCommBuf[ENCAPSULATION_HEADER_LENGTH]);

  if (nSendSize > 0)
    { /* need to send reply */
      pa_stReceiveData->nData_length = nSendSize;
      return encapsulate_data(pa_stReceiveData);
    }

  return nSendSize;
}

/*   INT8 SendRRData(struct S_Encapsulation_Data *pa_stReceiveData)
 *   Call UCMM or Message Router if UCMM not implemented.
 *      pa_stReceiveData pointer to structure with data and header information.
 *  return status 	0 .. success.
 * 					-1 .. error
 */
int
SendRRData(struct S_Encapsulation_Data * pa_stReceiveData)
{
  EIP_INT16 nSendSize;
  /* Commandspecific data UDINT .. Interface Handle, UINT .. Timeout, CPF packets */
  /* don't use the data yet */
  ltohl(&pa_stReceiveData->pEncapsulation_Data); /* skip over null interface handle*/
  ltohs(&pa_stReceiveData->pEncapsulation_Data); /* skip over unused timeout value*/
  pa_stReceiveData->nData_length -= 6; /* the rest is in CPF format*/

  if (checkRegisteredSessions(pa_stReceiveData) == EIP_ERROR) /* see if the EIP session is registered*/
    { /* received a package with non registered session handle */
      pa_stReceiveData->nData_length = 0;
      pa_stReceiveData->nStatus = OPENER_ENCAP_STATUS_INVALID_SESSION_HANDLE;
      return encapsulate_data(pa_stReceiveData);
    }

  nSendSize
      = notifyCPF(pa_stReceiveData->pEncapsulation_Data,
          pa_stReceiveData->nData_length,
          &g_acCommBuf[ENCAPSULATION_HEADER_LENGTH]);

  if (nSendSize > 0)
    { /* need to send reply */
      pa_stReceiveData->nData_length = nSendSize;
      return encapsulate_data(pa_stReceiveData);
    }

  return nSendSize;
}

/*   INT8 getFreeSessionIndex()
 *   search for available sessions an return index.
 *  return index of free session in anRegisteredSessions.
 * 			-1 .. no free session available
 */
int
getFreeSessionIndex(void)
{
  int i;
  for (i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; i++)
    {
      if (EIP_INVALID_SOCKET == anRegisteredSessions[i])
        {
          return i;
        }
    }
  return -1;
}

/*   INT16 createEncapsulationStructure(INT16 pa_sockfd, INT8 *pa_buf, UINT16 pa_length, struct S_Encapsulation_Data *pa_S_Data)
 *   copy data from pa_buf in little endian to host in structure.
 *      pa_length	length of the data in pa_buf.
 *      pa_S_Data	structure to which data shall be copied
 *  return difference between bytes in pa_buf an data_length
 *  		0 .. full package received
 * 			>0 .. more than one packet received
 * 			<0 .. only fragment of data portion received
 */
EIP_INT16
createEncapsulationStructure(EIP_UINT8 * pa_buf, /* receive buffer*/
int pa_length, /* size of stuff in  buffer (might be more than one message)*/
struct S_Encapsulation_Data * pa_stData) /* the struct to be created*/

{
  pa_stData->nCommand_code = ltohs(&pa_buf);
  pa_stData->nData_length = ltohs(&pa_buf);
  pa_stData->nSession_handle = ltohl(&pa_buf);
  pa_stData->nStatus = ltohl(&pa_buf);
  /*memcpy(pa_stData->anSender_context, pa_buf, SENDER_CONTEXT_SIZE);*/
  pa_buf += SENDER_CONTEXT_SIZE;
  pa_stData->nOptions = ltohl(&pa_buf);
  pa_stData->pEncapsulation_Data = pa_buf;
  return (pa_length - ENCAPSULATION_HEADER_LENGTH - pa_stData->nData_length);
}

/*   INT8 checkRegisteredSessions(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   check if received package belongs to registered session.
 *      pa_stReceiveData received data.
 *  return 0 .. Session registered
 *  		-1 .. invalid session -> return unsupported command received
 */
EIP_STATUS
checkRegisteredSessions(struct S_Encapsulation_Data * pa_stReceiveData)
{

  if ((0 < pa_stReceiveData->nSession_handle)
      && (pa_stReceiveData->nSession_handle
          <= OPENER_NUMBER_OF_SUPPORTED_SESSIONS))
    {
      if (EIP_INVALID_SOCKET
          != anRegisteredSessions[pa_stReceiveData->nSession_handle - 1])
        {
          return EIP_OK;
        }
    }

  return EIP_ERROR;
}

void
closeSession(int pa_nSocket)
{
  int i;
  for (i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; ++i)
    {
      if (anRegisteredSessions[i] == pa_nSocket)
        {
          IApp_CloseSocket(pa_nSocket);
          anRegisteredSessions[i] = EIP_INVALID_SOCKET;
          break;
        }
    }
}

void encapShutDown(void){
  int i;
    for (i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; ++i)
      {
        if (EIP_INVALID_SOCKET == anRegisteredSessions[i])
          {
            IApp_CloseSocket(anRegisteredSessions[i]);
            anRegisteredSessions[i] = EIP_INVALID_SOCKET;
          }
      }
}

