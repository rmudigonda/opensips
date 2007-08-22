/*
 * $Id: xcap_auth.c 1337 2006-12-07 18:05:05Z bogdan_iancu $
 *
 * presence_xml module - 
 *
 * Copyright (C) 2006 Voice Sistem S.R.L.
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 *  2007-04-11  initial version (anca)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>

#include "../../str.h"
#include "../../dprint.h"
#include "../../parser/parse_uri.h"
#include "../presence/utils_func.h"
#include "presence_xml.h"
#include "xcap_auth.h"
#include "pidf.h"

extern char* xcap_table;
extern int force_active;
extern db_func_t pxml_dbf;
extern db_con_t *pxml_db;
int http_get_rules_doc(str* user, str* domain, str* rules_doc);

int pres_watcher_allowed(subs_t* subs)
{
	xmlDocPtr xcap_tree= NULL;
	xmlNodePtr node= NULL,  actions_node = NULL;
	xmlNodePtr sub_handling_node = NULL;
	char* sub_handling = NULL;
	
	DBG("PRESENCE_XML:pres_watcher_allowed: ...\n");
	
	/* if force_active set status to active*/
	if(force_active)
	{
		subs->status= ACTIVE_STATUS;
		return 0;
	}
	if(subs->auth_rules_doc== NULL)
		return 0;

	xcap_tree= xmlParseMemory(subs->auth_rules_doc->s,
			subs->auth_rules_doc->len);
	if(xcap_tree== NULL)
	{
		LOG(L_ERR, "PRESENCE_XML:pres_watcher_allowed: ERROR parsing xml memory\n");
		return -1;
	}

	node= get_rule_node(subs, xcap_tree);
	if(node== NULL)
		return 0;

	/* process actions */	
	actions_node = xmlNodeGetChildByName(node, "actions");
	if(actions_node == NULL)
	{	
		DBG( "PRESENCE_XML:pres_watcher_allowed: actions_node NULL\n");
		return 0;
	}
	DBG("PRESENCE_XML:pres_watcher_allowed:actions_node->name= %s\n",
			actions_node->name);
			
	sub_handling_node = xmlNodeGetChildByName(actions_node, "sub-handling");
	if(sub_handling_node== NULL)
	{	
		DBG( "PRESENCE_XML:pres_watcher_allowed:sub_handling_node NULL\n");
		return 0;
	}
	sub_handling = (char*)xmlNodeGetContent(sub_handling_node);
		DBG("PRESENCE_XML:pres_watcher_allowed:sub_handling_node->name= %s\n",
			sub_handling_node->name);
	DBG("PRESENCE_XML:pres_watcher_allowed:sub_handling_node->content= %s\n",
			sub_handling);
	
	if(sub_handling== NULL)
	{
		LOG(L_ERR, "PRESENCE_XML:pres_watcher_allowed:ERROR Couldn't get"
				" sub-handling content\n");
		return -1;
	}
	if( strncmp((char*)sub_handling, "block",5 )==0)
	{	
		subs->status = TERMINATED_STATUS;;
		subs->reason.s= "rejected";
		subs->reason.len = 8;
	}
	else	
	if( strncmp((char*)sub_handling, "confirm",7 )==0)
	{	
		subs->status = PENDING_STATUS;
	}
	else
	if( strncmp((char*)sub_handling , "polite-block",12 )==0)
	{	
		subs->status = ACTIVE_STATUS;
		subs->reason.s= "polite-block";
		subs->reason.len = 12;
	}
	else	
	if( strncmp((char*)sub_handling , "allow",5 )==0)
	{	
		subs->status = ACTIVE_STATUS;
		subs->reason.s = NULL;
	}
	xmlFree(sub_handling);

	return 0;

}	

xmlNodePtr get_rule_node(subs_t* subs, xmlDocPtr xcap_tree )
{
	str w_uri= {0, 0};
	char* id = NULL, *domain = NULL;
	int apply_rule = -1;
	xmlNodePtr ruleset_node = NULL, node1= NULL, node2= NULL;
	xmlNodePtr cond_node = NULL, except_node = NULL;
	xmlNodePtr identity_node = NULL, validity_node =NULL, sphere_node = NULL;
	xmlNodePtr iden_child;

	uandd_to_uri(subs->from_user, subs->from_domain, &w_uri);
	if(w_uri.s == NULL)
	{
		LOG(L_ERR, "PRESENCE_XML: get_rule_node:Error while creating uri\n");
		return NULL;
	}
	ruleset_node = xmlDocGetNodeByName(xcap_tree, "ruleset", NULL);
	if(ruleset_node == NULL)
	{
		DBG( "PRESENCE_XML:get_rule_node: ruleset_node NULL\n");
		goto error;

	}	
	for(node1 = ruleset_node->children ; node1; node1 = node1->next)
	{
		if(xmlStrcasecmp(node1->name, (unsigned char*)"text")==0 )
				continue;

		/* process conditions */
		DBG("PRESENCE_XML:get_rule_node: node1->name= %s\n",node1->name);

		cond_node = xmlNodeGetChildByName(node1, "conditions");
		if(cond_node == NULL)
		{	
			DBG( "PRESENCE_XML:get_rule_node:cond node NULL\n");
			goto error;
		}
		DBG("PRESENCE_XML:get_rule_node:cond_node->name= %s\n",
				cond_node->name);

		validity_node = xmlNodeGetChildByName(cond_node, "validity");
		if(validity_node !=NULL)
		{
			DBG("PRESENCE_XML:get_rule_node:found validity tag\n");

		}	
		sphere_node = xmlNodeGetChildByName(cond_node, "sphere");

		identity_node = xmlNodeGetChildByName(cond_node, "identity");
		if(identity_node == NULL)
		{
			LOG(L_ERR, "PRESENCE_XML:get_rule_node:ERROR didn't find"
					" identity tag\n");
			goto error;
		}	
		
		iden_child= xmlNodeGetChildByName(identity_node, "one");
		if(iden_child)	
		{
			for(node2 = identity_node->children; node2; node2 = node2->next)
			{
				if(xmlStrcasecmp(node2->name, (unsigned char*)"one")!= 0)
					continue;
				
				id = xmlNodeGetAttrContentByName(node2, "id");	
				if(id== NULL)
				{
					LOG(L_ERR, "PRESENCE_XML:get_rule_node:Error while extracting"
							" attribute\n");
					goto error;
				}
				if((strlen(id)== w_uri.len && 
							(strncmp(id, w_uri.s, w_uri.len)==0)))	
				{
					apply_rule = 1;
					xmlFree(id);
					break;
				}
				xmlFree(id);
			}
		}	

		/* search for many node*/
		iden_child= xmlNodeGetChildByName(identity_node, "many");
		if(iden_child)	
		{
			domain = NULL;
			for(node2 = identity_node->children; node2; node2 = node2->next)
			{
				if(xmlStrcasecmp(node2->name, (unsigned char*)"many")!= 0)
					continue;
	
				domain = xmlNodeGetAttrContentByName(node2, "domain");
				if(domain == NULL)
				{	
					DBG("PRESENCE_XML:get_rule_node: No domain attribute to many\n");
				}
				else	
				{
					DBG("PRESENCE_XML:get_rule_node: <many domain= %s>\n", domain);
					if((strlen(domain)!= subs->from_domain.len && 
								strncmp(domain, subs->from_domain.s,
									subs->from_domain.len) ))
					{
						xmlFree(domain);
						continue;
					}	
				}
				xmlFree(domain);
				apply_rule = 1;
				if(node2->children == NULL)       /* there is no exception */
					break;

				for(except_node = node2->children; except_node;
						except_node= except_node->next)
				{
					if(xmlStrcasecmp(except_node->name, (unsigned char*)"except"))
						continue;

					id = xmlNodeGetAttrContentByName(except_node, "id");	
					if(id!=NULL)
					{
						if((strlen(id)- 1== w_uri.len && 
								(strncmp(id, w_uri.s, w_uri.len)==0)))	
						{
							xmlFree(id);
							apply_rule = 0;
							break;
						}
						xmlFree(id);
					}	
					else
					{
						domain = NULL;
						domain = xmlNodeGetAttrContentByName(except_node, "domain");
						if(domain!=NULL)
						{
							DBG("PRESENCE_XML:get_rule_node: Found except domain= %s\n- strlen(domain)= %d\n",
									domain, strlen(domain));
							if(strlen(domain)==subs->from_domain.len &&
								(strncmp(domain,subs->from_domain.s , subs->from_domain.len)==0))	
							{
								DBG("PRESENCE_XML:get_rule_node: except domain match\n");
								xmlFree(domain);
								apply_rule = 0;
								break;
							}
							xmlFree(domain);
						}	

					}	
				}
				if(apply_rule== 1)  /* if a match was found no need to keep searching*/
					break;

			}		
		}
		if(apply_rule ==1)
			break;
	}

	DBG("PRESENCE_XML:get_rule_node: apply_rule= %d\n", apply_rule);
	if(w_uri.s!=NULL)
		pkg_free(w_uri.s);

	if( !apply_rule || !node1)
		return NULL;

	return node1;

error:
	if(w_uri.s)
		pkg_free(w_uri.s);
	return NULL;
}	
int insert_db_xcap_doc(str* user, str* domain, str body)
{
	db_key_t query_cols[5];
	db_val_t query_vals[5];
	int n_query_cols= 0;

	query_cols[n_query_cols]= "username";
	query_vals[n_query_cols].nul= 0;
	query_vals[n_query_cols].type= DB_STR;
	query_vals[n_query_cols].val.str_val= *user;
	n_query_cols++;
	
	query_cols[n_query_cols]= "domain";
	query_vals[n_query_cols].nul= 0;
	query_vals[n_query_cols].type= DB_STR;
	query_vals[n_query_cols].val.str_val= *domain;
	n_query_cols++;

	query_cols[n_query_cols]= "xcap";
	query_vals[n_query_cols].nul= 0;
	query_vals[n_query_cols].type= DB_STR;
	query_vals[n_query_cols].val.str_val= body;
	n_query_cols++;

	query_cols[n_query_cols]= "doc_type";
	query_vals[n_query_cols].nul= 0;
	query_vals[n_query_cols].type= DB_INT;
	query_vals[n_query_cols].val.int_val= PRES_RULES;
	n_query_cols++;

	if(pxml_dbf.use_table(pxml_db, xcap_table)< 0)
	{
		LOG(L_ERR, "PRESENCE_XML:insert_db_xcap_doc:ERORR in sql use table\n");
		return -1;
	}
	if(pxml_dbf.insert(pxml_db, query_cols, query_vals, n_query_cols)< 0)
	{
		LOG(L_ERR, "PRESENCE_XML:insert_db_xcap_doc:ERORR in sql use table\n");
		return -1;
	}
	
	return 0;
}

int pres_get_rules_doc(str* user, str* domain, str** rules_doc)
{
	
	return get_rules_doc(user, domain, PRES_RULES, rules_doc);
}

int get_rules_doc(str* user, str* domain, int type, str** rules_doc)
{
	db_key_t query_cols[5];
	db_val_t query_vals[5];
	db_key_t result_cols[3];
	int n_query_cols = 0;
	db_res_t *result = 0;
	db_row_t *row ;	
	db_val_t *row_vals ;
	str body ;
	str* doc= NULL;

	if(force_active)
		return 0;

	/* first search in database */
	query_cols[n_query_cols] = "username";
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val = *user;
	n_query_cols++;
	
	query_cols[n_query_cols] = "domain";
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val = *domain;
	n_query_cols++;
	
	query_cols[n_query_cols] = "doc_type";
	query_vals[n_query_cols].type = DB_INT;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.int_val= type;
	n_query_cols++;

	result_cols[0] = "xcap";

	if (pxml_dbf.use_table(pxml_db, xcap_table) < 0) 
	{
		LOG(L_ERR, "PRESENCE_XML:get_rules_doc: Error in use_table\n");
		return -1;
	}

	if( pxml_dbf.query(pxml_db, query_cols, 0 , query_vals, result_cols, 
				n_query_cols, 1, 0, &result)<0)
	{
		LOG(L_ERR, "PRESENCE_XML:get_rules_doc:Error while querying table"
			" xcap for [user]=%.*s\t[domain]= %.*s\n",user->len, user->s,
			domain->len, domain->s);
		if(result)
			pxml_dbf.free_result(pxml_db, result);
		return -1;
	}
	if(result== NULL)
		return -1;

	if(result->n<=0)
	{
		DBG("PRESENCE_XML:get_rules_doc:No xcap document found for [user]=%.*s"
			"\t[domain]= %.*s\n",user->len, user->s,domain->len, domain->s);
		pxml_dbf.free_result(pxml_db, result);
		result= NULL;
		if(!integrated_xcap_server)
		{
			/* send a query to the server for the document */	
			if(http_get_rules_doc(user, domain, &body)< 0)
			{
				LOG(L_ERR, "PRESENCE_XML:get_rules_doc:Error sending http"
						" GET request to xcap server\n");
				goto error;
			}
			if(body.s && body.len)
			{
				if(insert_db_xcap_doc(user, domain, body)< 0)
				{
					LOG(L_ERR, "PRESENCE_XML:get_rules_doc:Error inserting"
							" in xcap_xml db table\n");
					goto error;
				}
				goto done; 
			}
		}
		return 0;
	}	
	row = &result->rows[0];
	row_vals = ROW_VALUES(row);

	body.s = row_vals[0].val.str_val.s;
	if(body.s== NULL)
	{
		LOG(L_ERR, "PRESENCE_XML:get_rules_doc:ERROR Xcap doc NULL\n");
		goto error;
	}	
	body.len = strlen(body.s);
	if(body.len== 0)
	{
		LOG(L_ERR,"PRESENCE_XML:get_rules_doc:ERROR Xcap doc empty\n");
		goto error;
	}			
	DBG("PRESENCE_XML:get_xcap_tree: xcap body:\n%.*s", body.len,body.s);

done:
	doc= (str*)pkg_malloc(sizeof(str));
	if(doc== NULL)
	{
		LOG(L_ERR, "PRESENCE_XML:get_rules_doc:ERROR No more memory\n");
		goto error;
	}
	doc->s= (char*)pkg_malloc(body.len* sizeof(char));
	if(doc->s== NULL)
	{
		LOG(L_ERR, "PRESENCE_XML:get_rules_doc:ERROR No more memory\n");
		pkg_free(doc);
		goto error;
	}
	memcpy(doc->s, body.s, body.len);
	doc->len= body.len;

	*rules_doc= doc;

	if(result)
		pxml_dbf.free_result(pxml_db, result);

	return 0;

error:
	if(result)
		pxml_dbf.free_result(pxml_db, result);

	return -1;

}

int http_get_rules_doc(str* user, str* domain, str* rules_doc)
{
	str uri;
	xcap_doc_sel_t doc_sel;
	char* doc= NULL;
	xcap_serv_t* xs;

	if(uandd_to_uri(*user, *domain, &uri)< 0)
	{
		LOG(L_ERR, "PRESENCE_XML:http_get_doc:ERROR constructing uri\n");
		goto error;
	}

	doc_sel.auid.s= "presence-rules";
	doc_sel.auid.len= strlen("presence-rules");
	doc_sel.type= USERS_TYPE;
	doc_sel.xid= uri;
	doc_sel.filename.s= "index";
	doc_sel.filename.len= 5;

	/* need the whole document so the node selector is NULL */
	/* don't know which is the authoritative server for the user
	 * so send requet to all in the list */
	xs= xs_list;
	while(xs)
	{
		doc= xcap_GetElem(xs->addr, &doc_sel, NULL);
		if(doc== NULL)
		{
			LOG(L_ERR, "PRESENCE_XML:http_get_doc: ERROR while fetching"
					" data from xcap server\n");
			goto error;	
		}
	}

	rules_doc->s= doc;
	rules_doc->len= doc?strlen(doc):0;

	return 0;

error:
	
	return -1;


}
