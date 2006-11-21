
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "u/libu.h"
#include "wsman-xml-api.h"
#include "wsman-soap.h"
#include "wsman-xml.h"
#include "wsman-xml-serializer.h"

#include "wsman-faults.h"
#include "wsman-soap-envelope.h"
#include "wsman-soap-message.h"


void 
wsman_set_message_flags(WsmanMessage *msg, 
                        unsigned int flag)
{
    msg->flags |= flag;
    return;
}


WsmanMessage*
wsman_soap_message_new()
{
    WsmanMessage *wsman_msg = u_zalloc(sizeof(WsmanMessage));
    u_buf_create(&wsman_msg->request);
    u_buf_create(&wsman_msg->response);

    wsman_msg->status.fault_code = 0;
    wsman_msg->status.fault_detail_code = 0;
    wsman_msg->status.fault_msg = NULL;
    return wsman_msg;
}

void
wsman_soap_message_destroy(WsmanMessage* wsman_msg)
{
  u_buf_free(wsman_msg->response);
  u_buf_free(wsman_msg->request);
  if (wsman_msg->status.fault_msg)
    u_free(wsman_msg->status.fault_msg);		
	
  u_free(wsman_msg);
}
