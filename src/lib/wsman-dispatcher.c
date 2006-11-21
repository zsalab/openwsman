/*******************************************************************************
 * Copyright (C) 2004-2006 Intel Corp. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of Intel Corp. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Intel Corp. OR THE CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

/**
 * @author Anas Nashif
 * @author Eugene Yarmosh
 */


#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "u/libu.h"
#include "wsman-xml-api.h"
#include "wsman-soap.h"
#include "wsman-xml.h"

#include "wsman-dispatcher.h"
#include "wsman-xml-serializer.h"
#include "wsman-faults.h"
#include "wsman-soap-envelope.h"




/**
 * @defgroup Dispatcher Dispatcher
 * @brief SOAP Dispatcher
 * 
 * @{
 */


// TBD: ??? Should it be SoapH specific
struct __WkHeaderInfo
{
  char* ns;
  char* name;
};


int is_wk_header(WsXmlNodeH header)
{
  static struct __WkHeaderInfo s_Info[] =
    {
      {XML_NS_ADDRESSING, WSA_TO},
      {XML_NS_ADDRESSING, WSA_MESSAGE_ID},
      {XML_NS_ADDRESSING, WSA_RELATES_TO},
      {XML_NS_ADDRESSING, WSA_ACTION},
      {XML_NS_ADDRESSING, WSA_REPLY_TO},
      {XML_NS_ADDRESSING, WSA_TO},
      {XML_NS_ADDRESSING, WSA_FROM},  
      {XML_NS_WS_MAN, WSM_RESOURCE_URI},
      {XML_NS_WS_MAN, WSM_SELECTOR_SET},
      {XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE},
      {XML_NS_WS_MAN, WSM_OPERATION_TIMEOUT},

      {NULL, NULL}
    };

  int i;
  char* name = ws_xml_get_node_local_name(header);
  char* ns = ws_xml_get_node_name_ns(header);

  for(i = 0; s_Info[i].name != NULL; i++)
  {
    if ( (ns == NULL && s_Info[i].ns == NULL)
         ||
         (ns != NULL && s_Info[i].ns != NULL && !strcmp(ns, s_Info[i].ns)) )
    {
      if ( !strcmp(name, s_Info[i].name) )
        return 1;
    }
  }
  debug("mustUnderstand: %s:%s", !ns ? "null" : ns, name);  
  return 0;
}


int unlink_response_entry(SoapH soap, op_t* entry)
{
  int retVal = 0;

  if (soap && entry)
  {
    int try = u_try_lock(soap);

    lnode_t *node = list_first(soap->responseList);
    while( node != NULL )
    {
      if ( entry == (op_t*)node->list_data )
      {
        list_delete(soap->responseList, node);
        u_free(node);
        retVal = 1;
        break;
      }
      node = list_next(soap->responseList, node);
    }

    if (!try)
      u_unlock(soap);
  }

  return retVal;
}



static void 
wsman_generate_op_fault( op_t* op, 
                         WsmanFaultCodeType faultCode,
                         WsmanFaultDetailType faultDetail ) 
{
  if (op->in_doc == NULL)
    return;
  op->out_doc = wsman_generate_fault(op->cntx, op->in_doc, faultCode,
                                     faultDetail, NULL);
  return;
}

int validate_control_headers(op_t* op) 
{
  unsigned long size = 0;
  WsXmlNodeH header = 
    get_soap_header_element(op->dispatch->fw, op->in_doc, NULL, NULL);
  if ( ws_xml_get_child(header, 0, XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE) != NULL )
  {
    size = ws_deserialize_uint32(NULL, header, 0, XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE);
    if ( size < WSMAN_MINIMAL_ENVELOPE_SIZE_REQUEST) 
    {
      wsman_generate_op_fault(op, WSMAN_ENCODING_LIMIT, WSMAN_DETAIL_MAX_ENVELOPE_SIZE);
      return 0;
    } 
  }
  
  if ( ws_xml_get_child(header, 0, XML_NS_WS_MAN, WSM_OPERATION_TIMEOUT) != NULL )
  {
    size = ws_deserialize_uint32(NULL, header, 0, XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE);
    if ( size < WSMAN_MINIMAL_ENVELOPE_SIZE_REQUEST) 
    {
      wsman_generate_op_fault(op, WSMAN_UNSUPPORTED_FEATURE, WSMAN_DETAIL_OPERATION_TIMEOUT);
      return 0;
    } 
  }        
  return 1;
}


WsXmlNodeH 
validate_mustunderstand_headers(op_t* op)
{
  WsXmlNodeH child = NULL;
  WsXmlNodeH header;
  int i;
    
  header = get_soap_header_element(op->dispatch->fw, 
                                   op->in_doc, NULL, NULL);
  char* nsUri = ws_xml_get_node_name_ns(header);    

  for(i = 0; (child = ws_xml_get_child(header, i, NULL, NULL)) != NULL; i++)
  {
    if ( ws_xml_find_attr_bool(child, nsUri, SOAP_MUST_UNDERSTAND) )
    {
      lnode_t* node = list_first(op->processed_headers);
      while(node != NULL)
      {
        if ( node->list_data == node )
          break;
        node = list_next(op->processed_headers, node);
      }
      if ( node == NULL )
      {
        if ( !is_wk_header(child) )
        {
          break;
        }
      }
    }
  }

  if ( child != NULL ) 
  {
    debug( "Mustunderstand Fault; %s", ws_xml_get_node_text(child) );
  }
  return child;
}


int
process_filter_chain(op_t* op, 
                     list_t* list) 
{
  int retVal = 0;
  callback_t* filter = (callback_t*)list_first(list);
  while( !retVal && filter != NULL )
  {
    retVal = filter->proc((SoapOpH)op, filter->node.list_data);
    filter = (callback_t*)list_next(list, &filter->node);
  }
  return retVal;
}

/**
 * Process Filters
 * @param op SOAP operation
 * @param inbound Direction of message, 0 for outbound  and 1 for inbound.
 * @return 0 on sucesses, 1 on error.
 **/
int
process_filters( op_t* op,
                 int inbound)
{
  int retVal = 0;
  list_t* list;

  debug( "Processing Filters: %s", (!inbound) ? "outbound" : "inbound" );
  if ( !(op->dispatch->flags & SOAP_SKIP_DEF_FILTERS) )
  {
    list = (!inbound) ? op->dispatch->fw->outboundFilterList :
      op->dispatch->fw->inboundFilterList;
    retVal = process_filter_chain(op, list); 
  }

  if ( !retVal )
  {
    list = (!inbound) ? op->dispatch->outboundFilterList :
      op->dispatch->inboundFilterList;
    retVal = process_filter_chain(op, list); 
  }

  if ( !retVal && inbound )
  {
        
    WsXmlNodeH notUnderstoodHeader;
    if ( (notUnderstoodHeader = validate_mustunderstand_headers(op)) != 0 ) {        
      wsman_generate_notunderstood_fault(op, notUnderstoodHeader);
      retVal = 1;
    }
        
    if (!validate_control_headers(op)) 
    {            
      retVal = 1;
    }
  }
  if (retVal) {
    debug( "Filteres Processed");
  }
  return retVal;
}

int
soap_add_disp_filter( SoapDispatchH disp,
                      SoapServiceCallback callbackProc,
                      void* callbackData,
                      int inbound)
{
  callback_t* entry = NULL;
  if ( disp )
  {
    list_t* list = (!inbound) ? 
      ((dispatch_t*)disp)->outboundFilterList : 
      ((dispatch_t*)disp)->inboundFilterList;
    entry = make_callback_entry(callbackProc, callbackData, list);
  }
  return (entry == NULL);
}


int
soap_add_op_filter( SoapOpH op,
                    SoapServiceCallback proc,
                    void* data,
                    int inbound)
{
  if ( op )
    return soap_add_disp_filter((SoapDispatchH)((op_t*)op)->dispatch, 
                                proc, 
                                data,
                                inbound);
  return 1;
}


int 
soap_add_filter(SoapH soap, 
                SoapServiceCallback callbackProc,
                void* callbackData, 
                int inbound)
{
  callback_t* entry = NULL;
  if (soap) {
    list_t* list = (!inbound) ?
    soap->outboundFilterList :
    soap->inboundFilterList;
    entry = make_callback_entry(callbackProc, callbackData, list);
  }
  return (entry == NULL);
}


int
outbound_control_header_filter( SoapOpH opHandle, 
                                void* data)
{
  unsigned long size = 0;
  char *buf = NULL;
  int len, envelope_size;
  SoapH soap = soap_get_op_soap(opHandle);
  WsXmlDocH in_doc = soap_get_op_doc(opHandle, 1);
  WsXmlDocH out_doc = soap_get_op_doc(opHandle, 0);
  WsXmlNodeH inHeaders = get_soap_header_element(soap, in_doc, NULL, NULL);

  if (inHeaders) {
    if (ws_xml_get_child(inHeaders, 0,
                XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE) != NULL) {
      size = ws_deserialize_uint32(NULL, inHeaders, 0,
                                 XML_NS_WS_MAN, WSM_MAX_ENVELOPE_SIZE);
      ws_xml_dump_memory_enc(out_doc, &buf, &len, "UTF-8");
      envelope_size = ws_xml_utf8_strlen(buf);
      if (envelope_size > size ) {
        wsman_generate_op_fault((op_t*) opHandle, WSMAN_ENCODING_LIMIT,
                                WSMAN_DETAIL_MAX_ENVELOPE_SIZE_EXCEEDED);
      }
    }
  }
  return 0;
}

int
outbound_addressing_filter(SoapOpH opHandle, 
                           void* data)
{
  SoapH soap = soap_get_op_soap(opHandle);
  WsXmlDocH in_doc = soap_get_op_doc(opHandle, 1);
  WsXmlDocH out_doc = soap_get_op_doc(opHandle, 0);
    
  WsXmlNodeH outHeaders = get_soap_header_element(soap, out_doc, NULL, NULL);

  if (outHeaders) {
    if ( ws_xml_get_child(outHeaders, 0, XML_NS_ADDRESSING,
            WSA_MESSAGE_ID) == NULL && !wsman_is_identify_request(in_doc)) {
      char uuidBuf[100];
      generate_uuid(uuidBuf, sizeof(uuidBuf), 0);
      ws_xml_add_child(outHeaders, XML_NS_ADDRESSING, WSA_MESSAGE_ID, uuidBuf);
      debug( "Adding message id: %s" , uuidBuf);
    }

    if ( in_doc != NULL ) {
      WsXmlNodeH inMsgIdNode;
      inMsgIdNode = get_soap_header_element(soap, in_doc,
                                    XML_NS_ADDRESSING, WSA_MESSAGE_ID);
      if (inMsgIdNode != NULL && !ws_xml_get_child(outHeaders, 0,
                                XML_NS_ADDRESSING, WSA_RELATES_TO)) {
        ws_xml_add_child(outHeaders, XML_NS_ADDRESSING,
                WSA_RELATES_TO, ws_xml_get_node_text(inMsgIdNode));
      }
    }
  }
  return 0;
}



/**
 * List all dispatcher interfaces
 * @param interfaces Dispatcher interfaces
 */
#ifdef DEBUG_VERBOSE
void wsman_dispatcher_list( list_t *interfaces ) 
{
  lnode_t *node = list_first(interfaces);
  while(node) {
    WsDispatchInterfaceInfo* interface = (WsDispatchInterfaceInfo*) node->list_data;	
    debug("Listing Dispatcher: interface->wsmanResourceUri: %s", interface->wsmanResourceUri);		 
    node = list_next (interfaces, node);
  }		
}
#else
#define wsman_dispatcher_list
#endif


int
process_inbound_operation(op_t* op, 
                          WsmanMessage *msg)
{
  int retVal = 1;
  char* buf = NULL;
  int len;

  if ( process_filters(op, 1) ) 
  {
    if (wsman_is_fault_envelope(op->out_doc)) {
      msg->http_code = WSMAN_STATUS_INTERNAL_SERVER_ERROR;
    } else {
      msg->http_code = WSMAN_STATUS_OK;
    }

    if (op->out_doc)
    {
      ws_xml_dump_memory_enc(op->out_doc, &buf, &len, "UTF-8");     
      u_buf_set(msg->response, buf, len);

      ws_xml_destroy_doc(op->out_doc);
      u_free(buf);
      destroy_op_entry(op);
    } else {
      error( "doc is null");
    }
  } else {
    if ( op->dispatch->serviceCallback != NULL )
      retVal = op->dispatch->serviceCallback((SoapOpH)op, op->dispatch->serviceData);
    else
      error( "op service callback is null");    	

    if ( (retVal = process_filters(op, 0)) == 0 ) {
      if (op->out_doc) {
        if (wsman_is_fault_envelope(op->out_doc)) {
          msg->http_code = WSMAN_STATUS_INTERNAL_SERVER_ERROR;
        } else {
          msg->http_code = WSMAN_STATUS_OK;
        }
        ws_xml_dump_memory_enc(op->out_doc, &buf, &len, "UTF-8");
        u_buf_set(msg->response, buf, len);      
        ws_xml_destroy_doc(op->out_doc);
        u_free(buf);
        destroy_op_entry(op);
      } else {
        error( "doc is null");
      }
    }
  }
  return retVal;
}



void
dispatch_inbound_call(SoapH soap,
                      WsmanMessage *msg) 
{   
  int ret;		 
  WsXmlDocH in_doc = wsman_build_inbound_envelope(soap, msg);

  debug( "Inbound call...");
  op_t* op = NULL;

  if (in_doc != NULL && !wsman_fault_occured(msg)) {
    dispatch_t* dispatch = get_dispatch_entry(soap, in_doc);

    if (dispatch != NULL) {
      op = create_op_entry(soap, dispatch, msg, 0);
      if (op == NULL ) {
        destroy_dispatch_entry(dispatch);
      }
    } else if (!wsman_fault_occured(msg)) {
      debug("xx");
      wsman_set_fault(msg, WSA_DESTINATION_UNREACHABLE, 
                      WSMAN_DETAIL_INVALID_RESOURCEURI, NULL);
    }

    if ( op != NULL ) {
      op->in_doc = in_doc;
      ret = process_inbound_operation(op, msg);
    }
  }

  if (in_doc != NULL) {
    msg->in_doc = in_doc;
  }
  debug( "Inbound call completed");
  return;
}



dispatch_t*
get_dispatch_entry(SoapH soap, WsXmlDocH doc)
{
  dispatch_t* dispatch = NULL;
  if ( soap->dispatcherProc ) {
    dispatch = (dispatch_t*)soap->dispatcherProc(soap->cntx,
                                    soap->dispatcherData, doc);
  }

  if ( dispatch == NULL ) {
    error( "Dispatcher Error");
  } else { 
    dispatch->usageCount++;
  }
  return dispatch;
}

static char *
wsman_dispatcher_match_ns( WsDispatchInterfaceInfo* r, 
                           char *uri )
{
  char *ns = NULL;
  die_if(r == NULL);
  if (r->namespaces == NULL) {
    return NULL;
  }

  if ( uri ) 
  {
    lnode_t *node = list_first(r->namespaces);
    while(node) 
    {
      WsSupportedNamespaces *sns = (WsSupportedNamespaces *)node->list_data;
      if ( sns->ns != NULL && strstr(uri, sns->ns) ) {
        ns = u_strdup(sns->ns);
        break;
      }
      node = list_next(r->namespaces, node);
    }
  }
  return ns;
}

SoapDispatchH 
wsman_dispatcher( WsContextH cntx, 
                  void* data, 
                  WsXmlDocH doc) 
{
  SoapDispatchH disp = NULL;
  char *uri = NULL, *action;
  WsManDispatcherInfo* dispInfo = (WsManDispatcherInfo*)data;
  WsDispatchEndPointInfo* ep = NULL;
  WsDispatchEndPointInfo* ep_custom = NULL;
  int i, resUriMatch = 0;
  char *ns = NULL;

  WsDispatchInterfaceInfo* r = NULL;  
  lnode_t *node = list_first((list_t *)dispInfo->interfaces);

  if ( doc == NULL ) {
    error("doc is null");
    u_free(data);
    goto cleanup;
  } else {
    uri = wsman_get_resource_uri(cntx, doc);
    action = ws_addressing_get_action(cntx, doc);
    if ( (!uri || !action) &&  !wsman_is_identify_request(doc) ) {
      goto cleanup;
    }
  }

  while( node != NULL )
  {            
    WsDispatchInterfaceInfo* interface = (WsDispatchInterfaceInfo*)node->list_data;
    if ( wsman_is_identify_request(doc)) 
    {
      if ( (ns = wsman_dispatcher_match_ns(interface, XML_NS_WSMAN_ID ) ) ) 
      {
        r = interface;
        resUriMatch = 1;
        break;                    
      }
      debug("ns did not match");
    }
    /*
     * If Resource URI is null then most likely we are dealing with  a generic plugin
     * supporting a namespace with multiple Resource URIs (e.g. CIM)
     */
    else if (interface->wsmanResourceUri == NULL && ( ns = wsman_dispatcher_match_ns(interface, uri)) ) {
      r = interface;
      resUriMatch = 1;
      break;                    
    } else if (interface->wsmanResourceUri && !strcmp(uri, interface->wsmanResourceUri) ) {     
      r = interface;
      resUriMatch = 1;
      break;                    
    }
    node = list_next ((list_t *)dispInfo->interfaces , node);                            
  }

  if ( wsman_is_identify_request(doc) && r != NULL) {
    ep = &r->endPoints[0];      
  }
  else if ( r != NULL )
  {            	
    char* ptr = action;
    /*
     * See if the action is part of the namespace which means that
     * we are dealing with a custom action
     */
    if ( ns != NULL )
    {
      int len = strlen(ns);
      if ( !strncmp(action, ns, len) && action[len] == '/' )
        ptr = &action[len + 1];
    }

    for(i = 0; r->endPoints[i].serviceEndPoint != NULL; i++)
    {
      if ( r->endPoints[i].inAction != NULL && !strcmp(ptr, r->endPoints[i].inAction) )
      {
        ep = &r->endPoints[i];
        break;
      } else if (r->endPoints[i].inAction == NULL) {
        // Just store it for later in case no match is found for above condition
        ep_custom = &r->endPoints[i];
      }

    }
  }

  ws_remove_context_val(cntx, WSM_RESOURCE_URI);

  if ( ep != NULL ) {
    for(i = 0; i < dispInfo->mapCount; i++)
    {
      if ( dispInfo->map[i].ep == ep )
      {
        disp = dispInfo->map[i].disp;
        break;
      }
    }
  } else if ( ep_custom != NULL ) {
    for(i = 0; i < dispInfo->mapCount; i++)
    {
      if ( dispInfo->map[i].ep == ep_custom )
      {
        disp = dispInfo->map[i].disp;
        break;
      }
    }
  }

 cleanup:
  if (ns)
    u_free(ns);
  return disp;
}

/*
 * Create Dispatch Entry
 * @param soap Soap handle
 * @param inboundAction Inbound Action
 * @param outboundAction Outbound Action (optional)
 * @param role Role (reserved, must be NULL)
 * @param callbackProc Callback processor
 * @param callbackData Callback data
 * @param flags Flags
 * @return Dispatch Handle
 */
SoapDispatchH
soap_create_dispatch(SoapH soap, 
                     char* inboundAction,
                     char* outboundAction, // optional
                     char* role, // reserved, must be NULL
                     SoapServiceCallback callbackProc,
                     void* callbackData,
                     unsigned long flags)
{
  debug("Creating dispatch");
  dispatch_t* disp = NULL;
  if ( soap && role == NULL )
  {
    disp = create_dispatch_entry(soap, inboundAction, outboundAction,
                                 role, callbackProc, callbackData, flags);
  }

  return (SoapDispatchH)disp;
}

/*
 * Start Dispatcher
 */
void soap_start_dispatch(SoapDispatchH disp)
{
  if ( disp ) {
    list_append(((dispatch_t*)disp)->fw->dispatchList, 
                &((dispatch_t*)disp)->node);
  }
}


/*
 * Create dispatch Entry
 * 
 * @todo support for custom roles
 * @param fw Soap Framework Handle
 * @param inboundAction Inbound Action
 * @param outboundAction Outbound Action
 * @param role Role
 * @param proc Call back processor
 * @param data Callback Data
 * @param flags Flags
 * @return Dispatch Entry
 */
dispatch_t*
create_dispatch_entry(SoapH soap, 
                      char* inboundAction, 
                      char* outboundAction, 
                      char* role, 
                      SoapServiceCallback proc, 
                      void* data, 
                      unsigned long flags)
{

  dispatch_t* entry = wsman_dispatch_entry_new();
  if ( entry ) {
    entry->fw = soap;
    entry->flags = flags;
    entry->inboundAction = u_str_clone(inboundAction);
    entry->outboundAction = u_str_clone(outboundAction);
    entry->serviceCallback = proc;
    entry->serviceData = data;
    entry->usageCount = 1;
    entry->inboundFilterList =  list_create(LISTCOUNT_T_MAX);
    entry->outboundFilterList =  list_create(LISTCOUNT_T_MAX);
  }
  return entry;
}


dispatch_t* wsman_dispatch_entry_new() 
{
  dispatch_t* entry = 
    (dispatch_t*)u_zalloc(sizeof(dispatch_t));

  if ( entry )
    return entry;
  else
    return NULL;
            

}

/** @} */


