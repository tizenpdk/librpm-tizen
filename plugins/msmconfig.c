/*
 * This file is part of MSM security plugin
 * Greatly based on the code of MSSF security plugin
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Ilhan Gurel <ilhan.gurel@nokia.com>
 *
 * Copyright (C) 2011 - 2013 Intel Corporation.
 *
 * Contact: Elena Reshetova <elena.reshetova@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <libxml/tree.h>

#include "rpmio/rpmbase64.h"
#include "rpmio/rpmlog.h"
#include "rpm/rpmfileutil.h"
#include <rpm/rpmmacro.h>

#include "msm.h"

typedef enum credType_e {
    CRED_ALLOWMATCHES  = 0,
    CRED_ALLOW         = 1,
    CRED_DENYMATCHES   = 2,
    CRED_DENY          = 3,
    CRED_PROVIDE       = 4
} credType;

/**
 * Serializes key data
 * @todo Problem with getting keydata
 * @param parent	XML node
 * @param keyinfo	keyinfo structure
 * @return		none
 */
static void msmHandleKeyinfo(xmlNode *parent, keyinfo_x *keyinfo)
{    
    char *enc = NULL;

    if (!parent)
        return;
    
    while (keyinfo) {
        xmlNode *node = xmlNewNode(NULL, BAD_CAST "keyinfo");

        /* b64 encode keydata first */    
        if ((enc = rpmBase64Encode(keyinfo->keydata, keyinfo->keylen, -1)) != NULL) {
            xmlAddChild(node, xmlNewText(BAD_CAST "\n"));        
            xmlAddChild(node, xmlNewText(BAD_CAST enc));
            msmFreePointer((void**)&enc);   
        }

        xmlAddChild(parent, node);
        keyinfo = keyinfo->prev;
    }
}

/**
 * Serializes ac_domain data
 * @param parent	XML node
 * @param type	    Type (allow, deny,..)
 * @param ac_domain	ac_domain structure
 * @return		    none
 */
static void msmHandleACDomains(xmlNode *parent, credType type, 
				  ac_domain_x *ac_domain)
{
    if (!ac_domain || !parent)
        return;

    xmlNode *node = NULL;

    if ((type == CRED_ALLOWMATCHES) || (type == CRED_ALLOW)) {
        node = xmlNewNode(NULL, BAD_CAST "allow");    
    } else if ((type == CRED_DENYMATCHES) || (type == CRED_DENY)) {
        node = xmlNewNode(NULL, BAD_CAST "deny"); 
    } else if (type == CRED_PROVIDE) {
	node = parent;
    } else {
        return;    
    }

    while (ac_domain) {
        xmlNode *childnode = xmlNewNode(NULL, BAD_CAST "ac_domain");
        if ((type == CRED_ALLOWMATCHES) || (type == CRED_DENYMATCHES)) {
            xmlNewProp(childnode, BAD_CAST "match", BAD_CAST ac_domain->match);
        } else {
            xmlNewProp(childnode, BAD_CAST "name", BAD_CAST ac_domain->name);
	    if (ac_domain->type)
		xmlNewProp(childnode, BAD_CAST "policy", BAD_CAST ac_domain->type);
	    if (ac_domain->plist)
		xmlNewProp(childnode, BAD_CAST "plist", BAD_CAST ac_domain->plist);
        }
        xmlAddChild(node, childnode);
	if (type == CRED_ALLOW || type == CRED_DENY)
	    ac_domain = ac_domain->hh.next;
	else
	    ac_domain = ac_domain->prev;
    }

    if (type != CRED_PROVIDE)
	    xmlAddChild(parent, node);
}

/**
 * Serializes origin data
 * @param parent	XML node
 * @param origin	origin structure
 * @return		    none
 */
static void msmHandleOrigin(xmlNode *parent, origin_x *origin)
{    
    if (!parent)
        return;
    
    while (origin) {
        xmlNode *node = xmlNewNode(NULL, BAD_CAST "origin");
        xmlAddChild(parent, node);
        msmHandleKeyinfo(node, origin->keyinfos);
        origin = origin->prev;
    }
}

/**
 * Serializes provides data
 * @param parent	XML node
 * @param provide	provide structure
 * @return		    none
 */
static void msmHandleProvide(xmlNode *parent, provide_x *provide)
{    
    if (!parent)
        return;     

    while (provide) {
	if (provide->ac_domains) {
            xmlNode *node = xmlNewNode(NULL, BAD_CAST "provide");
            xmlAddChild(parent, node);
            msmHandleACDomains(node, CRED_PROVIDE, provide->ac_domains);
            if (provide->origin) {
                xmlNode *childnode = xmlNewNode(NULL, BAD_CAST "for");
                xmlNewProp(childnode, BAD_CAST "origin", BAD_CAST provide->origin);
                xmlAddChild(node, childnode);
            }
	}
        provide = provide->prev;
    }
}

/**
 * Serializes packages data
 * @param parent	XML node
 * @param package	package structure
 * @return		none
 */
static void msmHandlePackage(xmlNode *parent, package_x *package)
{    
    if (!parent)
        return; 

    while (package) {
	if (!package->newer) {
	    xmlNode *node = xmlNewNode(NULL, BAD_CAST "package");
	    xmlNewProp(node, BAD_CAST "name", BAD_CAST package->name);
	    if (package->modified) 
		xmlNewProp(node, BAD_CAST "modified", BAD_CAST package->modified);
	    xmlAddChild(parent, node);
	    msmHandleProvide(node, package->provides);
	}
	package = package->prev;
    }
}

/**
 * Serializes sw source data
 * @param parent	XML node
 * @param sw_source	sw_source structure
 * @return		    none
 */
static void msmHandleSWSource(xmlNode *parent, sw_source_x *sw_source)
{
    #define MAX_DEPTH 10
    xmlNode *node[MAX_DEPTH];
    sw_source_x *temp;
    int depth = 0;

    if (!sw_source || !parent)
        return;

    node[0] = parent;

    while (sw_source) {
	depth = 1; /* recalculate depth */
	for (temp = sw_source->parent; temp; temp = temp->parent) depth++;
	if (!sw_source->newer && depth < MAX_DEPTH) {
	    node[depth] = xmlNewNode(NULL, BAD_CAST "sw_source");
	    xmlNewProp(node[depth], BAD_CAST "name", BAD_CAST sw_source->name);
	    xmlNewProp(node[depth], BAD_CAST "rankkey", BAD_CAST sw_source->rankkey);
	    xmlAddChild(node[depth-1], node[depth]);
	    msmHandleOrigin(node[depth], sw_source->origins);
	    msmHandleACDomains(node[depth], CRED_ALLOWMATCHES, sw_source->allowmatches);
	    msmHandleACDomains(node[depth], CRED_ALLOW, sw_source->allows);
	    msmHandleACDomains(node[depth], CRED_DENYMATCHES, sw_source->denymatches);
	    msmHandleACDomains(node[depth], CRED_DENY, sw_source->denys);
	    msmHandlePackage(node[depth], sw_source->packages);
	    if (sw_source->older) {
		/* packages still belong to this sw_source */
		msmHandlePackage(node[depth], sw_source->older->packages);
	    }
	}
	sw_source = sw_source->next;
    }
}

/**
 * Saves sw_source configuration into /etc/dev-sec-policy.
 * @param mfx		data to serialize
 * @return		RPMRC_OK or RPMRC_FAIL
 */
rpmRC msmSaveDeviceSecPolicyXml(manifest_x *mfx, const char *rootDir)
{    
    FILE *outFile;
    rpmRC rc = RPMRC_OK;
    char *fullPath = NULL;

    fullPath = rpmGenPath(rootDir, DEVICE_SECURITY_POLICY, NULL);
    rpmlog(RPMLOG_DEBUG, "fullPath %s\n", fullPath);
    if (!fullPath) {
        rpmlog(RPMLOG_ERR, "Building a full path failed for device security policy\n");
        return RPMRC_FAIL;
    }

    /* if data doesn't have sw_source information, no need to do anything */    
    if (mfx && mfx->sw_sources) {    
	sw_source_x *sw_source;
	xmlDoc *doc = xmlNewDoc( BAD_CAST "1.0");
	xmlNode *rootnode = xmlNewNode(NULL, BAD_CAST "config");
	xmlDocSetRootElement(doc, rootnode);

	LISTHEAD(mfx->sw_sources, sw_source);	
        msmHandleSWSource(rootnode, sw_source);

        outFile = fopen(fullPath, "w");
        if (outFile) {
            xmlElemDump(outFile, doc, rootnode);
            fclose(outFile);
        } else {
            rpmlog(RPMLOG_ERR, "Unable to write device security policy to %s\n",
                   fullPath);
            msmFreePointer((void**)&fullPath);
            rc = RPMRC_FAIL;
        }
        xmlFreeDoc(doc);
        xmlCleanupParser();
    }

    msmFreePointer((void**)&fullPath);
    return rc;
}

/**
 * Copies a file
 * @param old_filename	old file name
 * @param new_filename	new file name
 * @return		result of copy or -1 on failure
 */
static int copyFile(char *old_filename, char  *new_filename)
{
    FD_t ptr_old, ptr_new;
    int res;

    ptr_old = Fopen(old_filename, "r.fdio");
    ptr_new = Fopen(new_filename, "w.fdio");

    if ((ptr_old == NULL) || (Ferror(ptr_old))) {
        return  -1;
    }

    if ((ptr_new == NULL) || (Ferror(ptr_new))) {
        Fclose(ptr_old);
        return  -1;
    }

    res = ufdCopy(ptr_old, ptr_new);

    Fclose(ptr_new);
    Fclose(ptr_old);
    return res;
}

/**
 * Loads device security policy.
 * @param rootDir	--root rpm optional prefix
 * @param dsp		pointer to the loaded policy
 * @return		RPMRC_OK or RPMRC_FAIL
 */
rpmRC msmLoadDeviceSecurityPolicy(const char* rootDir, manifest_x **dsp)
{
    char *dspFullPath = NULL, *dspFullPathDir = NULL;
    char *defaultdspPath = NULL;
    struct stat buf;

    dspFullPath = rpmGenPath(rootDir, (const char*)DEVICE_SECURITY_POLICY, NULL);
    rpmlog(RPMLOG_DEBUG, "Device Security policy full path %s\n", dspFullPath);
    if (!dspFullPath) {
        rpmlog(RPMLOG_ERR, "Building a full path failed for the device security policy\n");
        goto ldsp_error;
    }

    if (stat(dspFullPath, &buf) != 0) { // the policy file is missing
        if (rootDir) { // we are running with --root option and policy is missing, need to copy it for now
            // first create prefix for it
            char *sysconfdir = rpmExpand((const char*)"%{?_sysconfdir}", NULL);
            if (!sysconfdir || !strcmp(sysconfdir, "")) {
		rpmlog(RPMLOG_ERR, "Failed to expand %%_sysconfdir macro\n");
                goto ldsp_error;
            }

            dspFullPathDir = rpmGenPath(rootDir, sysconfdir, NULL);
            rpmlog(RPMLOG_DEBUG, "Device Security policy full path dir %s\n", dspFullPathDir);
            msmFreePointer((void**)&sysconfdir);
            if (!dspFullPathDir) {
		rpmlog(RPMLOG_ERR, "Building a full path for the device security policy dir failed\n");
                goto ldsp_error;
            }
            if (rpmioMkpath(dspFullPathDir, 0755, getuid(), getgid()) != 0) {
                    rpmlog(RPMLOG_ERR, "Failed to create a path for the device security policy dir\n");
                    goto ldsp_error;
            }
            defaultdspPath = rpmExpand((const char*)"%{?__transaction_msm_default_policy}", NULL);
            if (!defaultdspPath || !strcmp(defaultdspPath, "")) {
                rpmlog(RPMLOG_ERR, "Failed to expand transaction_msm_default_policy macro\n");
                goto ldsp_error;
            }
            if(copyFile(defaultdspPath, dspFullPath) == -1) {
		/* Do not allow plug-in to proceed without security policy existing */
		rpmlog(RPMLOG_ERR, "Failed to copy the device security policy to the chroot environment\n");
                goto ldsp_error;
            }
	} else {
            /* Do not allow plug-in to proceed without security policy existing */
            rpmlog(RPMLOG_ERR, "Policy file is missing at %s\n",
		   dspFullPath);
            goto ldsp_error;
        }
    }

    rpmlog(RPMLOG_DEBUG, "reading the device security policy from %s\n", dspFullPath);
    *dsp = msmProcessDevSecPolicyXml(dspFullPath);

    if (!*dsp) {
        rpmlog(RPMLOG_ERR, "Failed to process sw sources from %s\n", dspFullPath);
        goto ldsp_error;
    } else {
        if (msmSetupSWSources(NULL, *dsp, NULL)) {
            rpmlog(RPMLOG_ERR, "Failed to setup the device security policy from %s\n", dspFullPath);
            goto ldsp_error;
        }
    }

    return RPMRC_OK;

ldsp_error:
    msmFreePointer((void**)&dspFullPath);
    msmFreePointer((void**)&dspFullPathDir);
    msmFreePointer((void**)&defaultdspPath);
    return RPMRC_FAIL;
}

/**
 * Creates a directory for the smack rules. 
 * @param rootDir	--root rpm optional prefix
 * @return		RPMRC_OK or RPMRC_FAIL
 */
rpmRC msmSetupSmackRulesDir(const char* rootDir)
{
    char *smackRulesFullPath = NULL;
    rpmRC res = RPMRC_FAIL;

    smackRulesFullPath = rpmGenPath(rootDir, SMACK_RULES_PATH, NULL);
    rpmlog(RPMLOG_DEBUG, "smackRulesFullPath for SMACK_RULES_PATH %s\n", smackRulesFullPath);

    if (!smackRulesFullPath){
        rpmlog(RPMLOG_ERR, "Building a full path failed for smack rules path\n");
    	return res;
    }

    if (rpmioMkpath(smackRulesFullPath, 0744, getuid(), getgid()) != 0) {
        rpmlog(RPMLOG_ERR, "Failed to create a directory for smack rules\n");
    } else {
        res = RPMRC_OK;
    }

    msmFreePointer((void**)&smackRulesFullPath);
    return res;
}

