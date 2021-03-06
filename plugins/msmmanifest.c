/*
 * This file is part of MSM security plugin
 * Greatly based on the code of MSSF security plugin
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Tero Aho <ext-tero.aho@nokia.com>
 *
 * Copyright (C) 2011 -2013 Intel Corporation.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libxml/xmlreader.h>
#include <sys/capability.h>

#include "msm.h"

#include "rpmio/rpmbase64.h"
#include "rpmio/rpmlog.h"

/* We'll support only the basic set of characters */
#define ASCII(s) (const char *)s
#define XMLCHAR(s) (const xmlChar *)s

static int msmVerifyAccessType(const char* type)
{
    int res = 0, idx = 0;

    if (type) {
        if (strnlen(type, SMACK_ACCESS_TYPE_LENGTH + 1) >
            SMACK_ACCESS_TYPE_LENGTH) {
            rpmlog(RPMLOG_ERR, "Lenght of the access type is bigger than allowed value: %s\n", type);
            return -1;
        }
        while ( type[idx] != '\0' ){
            if ((type[idx] !='a') && (type[idx]!='r') && (type[idx]!='w') &&
                (type[idx]!='x') && (type[idx]!='t') && (type[idx]!='l') && (type[idx] !='-')) {
                rpmlog(RPMLOG_ERR, "Not allowed character in access type: %s\n", type);
                res = -1;
                break;
            }
            idx++;
        }
    } else return -1; 

    return res;
}

static int msmVerifySmackLabel(const char* label)
{
    int res = 0, idx = 0;

    if (label) {
        if (strlen(ASCII(label)) > SMACK_LABEL_LENGTH) { //smack limitation on lenght
            rpmlog(RPMLOG_ERR, "Domain or label name  %s lenght is longer than defined SMACK_LABEL_LENGTH\n", label);
            return -1; 
        }
        if (strlen(ASCII(label)) == 0){
            rpmlog(RPMLOG_ERR, "An attempt to define an empty domain or label name\n");
            return -1; 
        }
        if (label[0] == '-') {
            rpmlog(RPMLOG_ERR, "Dash is not allowed as first character in smack label: %s\n", label);
            return -1;
        }
        while ( label[idx] != '\0' ){
            if ((label[idx] =='\"') || (label[idx] =='\'') || (label[idx] =='/') ||
                (label[idx] =='\\') || (label[idx] > '~') || (label[idx] <= ' ')) {
                rpmlog(RPMLOG_ERR, "Not allowed character in smack label: %s, position: %d \n", label, idx);
                res = -1;
                break;
            }
            idx++;
        }
    } else return -1; 

    return res;
}

static int msmVerifyLabelPrefix(const char* sub_label, const char* domain_name) 
{
    char *tmp = NULL;
    char sep[]= "::";

    tmp = calloc(strlen(domain_name) + 3, sizeof (const char));
    if (!tmp) 
        return -1;

    strncpy(tmp, domain_name, strlen(domain_name));
    strncpy(tmp + strlen(domain_name), sep, 2);

    if (strstr(ASCII(sub_label), tmp) != ASCII(sub_label)) { //sub label name should be prefixed by domain name and "::"
        rpmlog(RPMLOG_ERR, "Label name %s isn't prefixed by domain name %s\n", ASCII(sub_label), domain_name);
        msmFreePointer((void**)&tmp);
        return -1;
    } 

    msmFreePointer((void**)&tmp);
    return 0;
}

static int msmNextChildElement(xmlTextReaderPtr reader, int depth) 
{
    int ret = xmlTextReaderRead(reader);
    int cur = xmlTextReaderDepth(reader);
    while (ret == 1) {
        /* rpmlog(RPMLOG_DEBUG, "node %s %d\n", 
           ASCII(xmlTextReaderConstName(reader)), 
           xmlTextReaderDepth(reader));
        */
        switch (xmlTextReaderNodeType(reader)) {
        case XML_READER_TYPE_ELEMENT:
        case XML_READER_TYPE_TEXT:
            if (cur == depth+1) 
                return 1;
            break;
        case XML_READER_TYPE_END_ELEMENT:
            if (cur == depth) 
                return 0;
            break;
        default:
            if (cur <= depth)
                return 0;
            break;
        }
        ret = xmlTextReaderRead(reader);
        cur = xmlTextReaderDepth(reader);
    }
    return ret;
}

static ac_domain_x *msmFreeACDomain(ac_domain_x *ac_domain)
{
    if (ac_domain) {
        ac_domain_x *prev = ac_domain->prev;
        msmFreePointer((void**)&ac_domain->name);
        msmFreePointer((void**)&ac_domain->type);
        msmFreePointer((void**)&ac_domain->match);
        msmFreePointer((void**)&ac_domain->plist);
        msmFreePointer((void**)&ac_domain);
        return prev;
    } else return NULL;
}

static annotation_x *msmProcessAnnotation(xmlTextReaderPtr reader)
{
    const xmlChar *name, *value;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    value = xmlTextReaderGetAttribute(reader, XMLCHAR("value"));
    rpmlog(RPMLOG_DEBUG, "annotation %s %s\n", ASCII(name), ASCII(value));

    if (name && value) {
        annotation_x *annotation = calloc(1, sizeof(annotation_x));
        if (annotation) {
            annotation->name = ASCII(name);
            annotation->value = ASCII(value);
            return annotation;
        }
    }
    msmFreePointer((void**)&name);
    msmFreePointer((void**)&value);
    return NULL;
}

static int msmProcessMember(xmlTextReaderPtr reader, member_x *member) 
{
    const xmlChar *node, *name;
    int ret, depth;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    rpmlog(RPMLOG_DEBUG, "member %s\n", ASCII(name));
    member->name = ASCII(name);

    if (!name) return -1;

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "annotation")) {
            annotation_x *annotation = msmProcessAnnotation(reader);
            if (annotation) {
                member->annotation = annotation;
            } else return -1;
        } else return -1;

        if (ret < 0) return -1;
    }
    return ret;
}

static int msmProcessInterface(xmlTextReaderPtr reader, interface_x *interface) 
{
    const xmlChar *node, *name;
    int ret, depth;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    rpmlog(RPMLOG_DEBUG, "interface %s\n", ASCII(name));
    interface->name = ASCII(name);

    if (!name) return -1;

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "method")) {
            member_x *member = calloc(1, sizeof(member_x));
            if (member) {
                member->type = DBUS_METHOD;
                ret = msmProcessMember(reader, member);
                LISTADD(interface->members, member);
            } else return -1;
        } else if (!strcmp(ASCII(node), "signal")) {
            member_x *member = calloc(1, sizeof(member_x));
            if (member) {
                member->type = DBUS_SIGNAL;
                ret = msmProcessMember(reader, member);
                LISTADD(interface->members, member);
            } else return -1;
        } else if (!strcmp(ASCII(node), "annotation")) {
            annotation_x *annotation = msmProcessAnnotation(reader);
            if (annotation) {
                interface->annotation = annotation;
            } else return -1;
        } else return -1;

        if (ret < 0) return -1;
    }
    return ret;
}

static int msmProcessNode(xmlTextReaderPtr reader, node_x *nodex) 
{
    const xmlChar *node, *name;
    int ret, depth;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    rpmlog(RPMLOG_DEBUG, "node %s\n", ASCII(name));
    nodex->name = ASCII(name);

    if (!name) return -1;

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "interface")) {
            interface_x *interface = calloc(1, sizeof(interface_x));
            if (interface) {
                ret = msmProcessInterface(reader, interface);
                LISTADD(nodex->interfaces, interface);
            } else return -1;
        } else if (!strcmp(ASCII(node), "method")) {
            member_x *member = calloc(1, sizeof(member_x));
            if (member) {
                member->type = DBUS_METHOD;
                ret = msmProcessMember(reader, member);
                LISTADD(nodex->members, member);
            } else return -1;
        } else if (!strcmp(ASCII(node), "signal")) {
            member_x *member = calloc(1, sizeof(member_x));
            if (member) {
                member->type = DBUS_SIGNAL;
                ret = msmProcessMember(reader, member);
                LISTADD(nodex->members, member);
            } else return -1;
        } else if (!strcmp(ASCII(node), "annotation")) {
            annotation_x *annotation = msmProcessAnnotation(reader);
            if (annotation) {
                nodex->annotation = annotation;
            } else return -1;
        } else return -1;

        if (ret < 0) return -1;
    }
    return ret;
}

static int msmProcessDBus(xmlTextReaderPtr reader, dbus_x *dbus) 
{
    const xmlChar *node, *name, *own, *bus;
    int ret, depth;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    own = xmlTextReaderGetAttribute(reader, XMLCHAR("own"));
    bus = xmlTextReaderGetAttribute(reader, XMLCHAR("bus"));
    rpmlog(RPMLOG_DEBUG, "dbus %s %s %s\n", ASCII(name), ASCII(own), ASCII(bus));
    dbus->name = ASCII(name);
    dbus->own = ASCII(own);
    dbus->bus = ASCII(bus);    

    if (!name || !bus) return -1;
    if (strcmp(dbus->bus, "session") && strcmp(dbus->bus, "system"))
        return -1;

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "node")) {
            node_x *nodex = calloc(1, sizeof(node_x));
            if (nodex) {
                ret = msmProcessNode(reader, nodex);
                LISTADD(dbus->nodes, nodex);
            } else return -1;
        } else if (!strcmp(ASCII(node), "annotation")) {
            annotation_x *annotation = msmProcessAnnotation(reader);
            if (annotation) {
                dbus->annotation = annotation;
            } else return -1;
        } else return -1;

        if (ret < 0) return -1;
    }
    return ret;
}

static ac_domain_x *msmProcessACDomain(xmlTextReaderPtr reader, sw_source_x *sw_source, const char* pkg_name)
{
    const xmlChar *name, *match, *policy, *plist;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    match = xmlTextReaderGetAttribute(reader, XMLCHAR("match"));
    policy = xmlTextReaderGetAttribute(reader, XMLCHAR("policy"));
    plist = xmlTextReaderGetAttribute(reader, XMLCHAR("plist"));
    rpmlog(RPMLOG_DEBUG, "ac_domain %s match %s policy %s plist %s\n", ASCII(name), ASCII(match), ASCII(policy), ASCII(plist));

    if (!((!name && !match) || (name && match))) {
        ac_domain_x *ac_domain = calloc(1, sizeof(ac_domain_x));
        if (ac_domain) {
            ac_domain->name = ASCII(name);
            ac_domain->match = ASCII(match);
            ac_domain->type = ASCII(policy);
            ac_domain->plist = ASCII(plist);
            ac_domain->sw_source = sw_source;
            ac_domain->pkg_name = pkg_name;	
            return ac_domain;
        }
    }
    rpmlog(RPMLOG_ERR, "Mandatory argument is missing for ac domain definition\n");
    rpmlog(RPMLOG_ERR, "ac_domain %s match %s policy %s plist %s\n", ASCII(name), ASCII(match), ASCII(policy), ASCII(plist));
    msmFreePointer((void**)&name);
    msmFreePointer((void**)&match);
    msmFreePointer((void**)&policy);
    msmFreePointer((void**)&plist);
    return NULL;
}

static filesystem_x *msmProcessFilesystem(xmlTextReaderPtr reader)
{
    const xmlChar *path, *label, *type, *exec_label;

    path = xmlTextReaderGetAttribute(reader, XMLCHAR("path"));
    label = xmlTextReaderGetAttribute(reader, XMLCHAR("label"));
    exec_label = xmlTextReaderGetAttribute(reader, XMLCHAR("exec_label"));
    type = xmlTextReaderGetAttribute(reader, XMLCHAR("type"));

    rpmlog(RPMLOG_DEBUG, "filesystem path %s label %s exec label %s type %s\n", 
	   ASCII(path), ASCII(label), ASCII(exec_label), ASCII(type));

   if (path && (label || exec_label)) {
        if ((label) && (msmVerifySmackLabel(ASCII(label)) < 0)) {
            goto fail;
	}
        if ((exec_label) && (msmVerifySmackLabel(ASCII(exec_label)) < 0)) {
            goto fail;
        }

        filesystem_x *filesystem = calloc(1, sizeof(filesystem_x));
        if (filesystem) {
            filesystem->path = ASCII(path);
            filesystem->label = ASCII(label);
            filesystem->exec_label = ASCII(exec_label);
            filesystem->type = ASCII(type);
            return filesystem;
	}

    } else {
        rpmlog(RPMLOG_ERR, "Mandatory argument is missing for filesystem assign request\n");
        rpmlog(RPMLOG_ERR, "filesystem path %s label %s exec label %s\n", 
               ASCII(path), ASCII(label), ASCII(exec_label));
    }

fail:
    msmFreePointer((void**)&path);
    msmFreePointer((void**)&label);
    msmFreePointer((void**)&exec_label);
    msmFreePointer((void**)&type);
    return NULL;
}

static int msmProcessProvide(xmlTextReaderPtr reader, provide_x *provide, sw_source_x *current, manifest_x *mfx, const char* pkg_name)
{
    const xmlChar *node, *name, *origin;
    int ret, depth;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    rpmlog(RPMLOG_DEBUG, "assign %s\n", ASCII(name));
    provide->name = ASCII(name);

    if (provide->name && 
       (strcmp(provide->name, "_system_") || mfx->sw_source->parent))
        return -1; /* only _system_ is accepted from root sw source */

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "dbus")) {
            dbus_x *dbus = calloc(1, sizeof(dbus_x));
            if (dbus) {
                ret = msmProcessDBus(reader, dbus);
                LISTADD(provide->dbuss, dbus);
            } else return -1;
        } else if (!strcmp(ASCII(node), "ac_domain")) {
            ac_domain_x *ac_domain = msmProcessACDomain(reader, current, pkg_name);
            if (ac_domain) {
                const char *name = ac_domain->name;
                LISTADD(provide->ac_domains, ac_domain);
		if (!name) return -1;
                if (mfx && !provide->name) {
                    ac_domain->name = malloc(strlen(mfx->name) + 2 +
					      strlen(name) + 1);
                    sprintf((char *)ac_domain->name, "%s::%s", mfx->name, name);
                    msmFreePointer((void**)&name);
		}
	    } else return -1;

       } else if (!strcmp(ASCII(node), "for")) {
            origin = xmlTextReaderGetAttribute(reader, XMLCHAR("origin"));
            rpmlog(RPMLOG_DEBUG, "for %s\n", ASCII(origin));
            if (!origin) return -1;
            if (provide->origin) { 
                msmFreePointer((void**)&origin);
                return -1;
	    }
            provide->origin = ASCII(origin);
            if (strcmp(ASCII(origin), "trusted") && 
                strcmp(ASCII(origin), "current") &&
                strcmp(ASCII(origin), "all"))
                return -1;

        } else if (!strcmp(ASCII(node), "filesystem")) {
            filesystem_x *filesystem = msmProcessFilesystem(reader);
            if (filesystem) {
                LISTADD(provide->filesystems, filesystem);
            } else return -1;

        } else {
            rpmlog(RPMLOG_ERR, "No allowed element in assign section: %s\n", ASCII(node));
            return -1;
        }

        if (ret < 0) return ret;
    }

    return ret;
}

static int msmProcessPackage(xmlTextReaderPtr reader, package_x *package, sw_source_x *current)
{
    const xmlChar *node, *name, *modified;
    int ret, depth;

    /* config processing */
    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    modified = xmlTextReaderGetAttribute(reader, XMLCHAR("modified"));
    rpmlog(RPMLOG_DEBUG, "package %s %s\n", name, modified);

    package->name = ASCII(name);
    package->modified = ASCII(modified);
    package->sw_source = current;

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "provide")) {
            provide_x *provide = calloc(1, sizeof(provide_x));
            if (provide) {
                LISTADD(package->provides, provide);
                ret = msmProcessProvide(reader, provide, current, NULL, package->name);
            } else return -1;
	} else return -1;

        if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessRequest(xmlTextReaderPtr reader, request_x *request) 
{
    const xmlChar *node, *name;
    int ret, depth, requestPresent = 0;

    rpmlog(RPMLOG_DEBUG, "request \n");
    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

        if (!strcmp(ASCII(node), "domain")) {
            if (requestPresent) {
                rpmlog(RPMLOG_ERR, "A second domain defined inside a request section. Abort package installation\n");
                return -1;
            }
            name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));   
            rpmlog(RPMLOG_DEBUG, "ac domain name %s\n", ASCII(name));
            if (name) {
                request->ac_domain = ASCII(name);
                requestPresent = 1;
            } else {
                rpmlog(RPMLOG_ERR, "No ac domain name defined in request.\n");
                return -1;
            }
        } else {
            rpmlog(RPMLOG_ERR, "Not allowed element in request section: %s\n", ASCII(node));
            return -1;
        }
    }
    return ret;
}

static int msmProcessDRequest(xmlTextReaderPtr reader, define_x *define) 
{
    const xmlChar *node = NULL, *label = NULL, *type = NULL;
    int ret, depth;

    rpmlog(RPMLOG_DEBUG, "request\n");

    if (!define->name) {
        rpmlog(RPMLOG_ERR, "An attempt to define a domain without a name. Abort.\n");
        return -1;
    }

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;

	if (!strcmp(ASCII(node), "smack")) {
	    label = xmlTextReaderGetAttribute(reader, XMLCHAR("request"));
	    type = xmlTextReaderGetAttribute(reader, XMLCHAR("type"));
	    rpmlog(RPMLOG_DEBUG, "request label %s type %s\n", ASCII(label), ASCII(type));
	    if (label && type) {
                if (msmVerifyAccessType(ASCII(type)) < 0) {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&type);	
                    return -1; 
                }
                if (msmVerifySmackLabel(ASCII(label)) < 0) {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&type);
                    return -1;
                }
                d_request_x *request = calloc(1, sizeof(d_request_x));
                if (request) {
                    request->label_name = ASCII(label);
                    request->ac_type = ASCII(type);
                    LISTADD(define->d_requests, request);
                } else {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&type);
                    return -1;
                }
            } else  {
                rpmlog(RPMLOG_ERR, "One of the mandatory arguments for domain request is missing. Abort installation\n");
                rpmlog(RPMLOG_ERR, "smack request label %s type %s\n", ASCII(label), ASCII(type));
                msmFreePointer((void**)&label);
                msmFreePointer((void**)&type);	
                return -1;
            }
	} else {
            rpmlog(RPMLOG_ERR, "Not allowed element in domain request section: %s\n", ASCII(node));
            return -1;
        }
        if (ret < 0) return ret;
    }

    return ret;
}

static int msmProcessDPermit(xmlTextReaderPtr reader, define_x *define) 
{
    const xmlChar *node, *label, *type, *to_label;
    int ret, depth;

    rpmlog(RPMLOG_DEBUG, "permit\n");

    if (!define->name) {
        rpmlog(RPMLOG_ERR, "An attempt to define a domain without a name. Abort.\n");
        return -1;
    }

    depth = xmlTextReaderDepth(reader);

    while ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;

	if (!strcmp(ASCII(node), "smack")) {
	    label = xmlTextReaderGetAttribute(reader, XMLCHAR("permit"));
	    to_label = xmlTextReaderGetAttribute(reader, XMLCHAR("to"));
	    type = xmlTextReaderGetAttribute(reader, XMLCHAR("type"));
	    rpmlog(RPMLOG_DEBUG, "permit %s to %s type %s\n", ASCII(label), ASCII(to_label), ASCII(type));

	    if (label && type) {
                if (msmVerifyAccessType(ASCII(type)) < 0) {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&to_label);
                    msmFreePointer((void**)&type);	
                    return -1; 
                }
                if (msmVerifySmackLabel(ASCII(label)) < 0) {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&to_label);
                    msmFreePointer((void**)&type);
                    return -1;
                }
                if ((to_label) && (msmVerifyLabelPrefix(ASCII(to_label), define->name) < 0)) {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&to_label);
                    msmFreePointer((void**)&type);
                    return -1;
                }
                d_permit_x *permit = calloc(1, sizeof(d_permit_x));
                if (permit) {
                    permit->label_name = ASCII(label);
                    permit->to_label_name = ASCII(to_label);
                    permit->ac_type = ASCII(type);
                    LISTADD(define->d_permits, permit);
                } else {
                    msmFreePointer((void**)&label);
                    msmFreePointer((void**)&to_label);
                    msmFreePointer((void**)&type);
                    return -1;
                }
	    } else  {
                rpmlog(RPMLOG_ERR, "One of the mandatory arguments for domain permit is missing. Abort installation\n");
                rpmlog(RPMLOG_ERR, "smack permit label %s type %s\n", ASCII(label), ASCII(type));
                msmFreePointer((void**)&label);
                msmFreePointer((void**)&to_label);
                msmFreePointer((void**)&type);	
                return -1;
	    }
        } else {
            rpmlog(RPMLOG_ERR, "Not allowed element in domain permit section: %s\n", ASCII(node));
            return -1;
        }
        if (ret < 0) return ret;
    }

    return ret;
}

static int msmProcessDProvide(xmlTextReaderPtr reader, define_x *define) 
{
    const xmlChar *node, *label;
    int ret = 0, depth;

    rpmlog(RPMLOG_DEBUG, "provide\n");

    if (!define->name) {
        rpmlog(RPMLOG_ERR, "An attempt to define a domain without a name. Abort.\n");
        return -1;
    }

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "label")) {
	    label = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
	    rpmlog(RPMLOG_DEBUG, "label %s \n", ASCII(label));

	    if (label) {
                if (msmVerifySmackLabel(ASCII(label)) < 0) {
                    msmFreePointer((void**)&label);
                    return -1;
                }
                if (msmVerifyLabelPrefix(ASCII(label), define->name) < 0) {
                    msmFreePointer((void**)&label);
                    return -1;
                }
                d_provide_x *provide = calloc(1, sizeof(d_provide_x));
                if (provide) {
                    provide->label_name = ASCII(label);
                    LISTADD(define->d_provides, provide);
                } else {
                    msmFreePointer((void**)&label);
                    return -1;
                }
	    } else {
                 rpmlog(RPMLOG_INFO, "Label name is empty. Label provide is ignored\n");
                 continue;
            }
        } else {
            rpmlog(RPMLOG_ERR, "Not allowed element in domain provide section: %s\n", ASCII(node));
            return -1;
        }
        if (ret < 0) return ret;
    }

    return ret;
}

static int msmProcessDefine(xmlTextReaderPtr reader, define_x *define, manifest_x *mfx, sw_source_x *current) 
{
    const xmlChar *node, *name, *policy, *plist;
    int ret, depth, domainPresent = 0;

    rpmlog(RPMLOG_DEBUG, "define\n");

    depth = xmlTextReaderDepth(reader);

    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;
        if (!strcmp(ASCII(node), "domain")) {
            if (domainPresent) {
                rpmlog(RPMLOG_ERR, "Only one domain is allowed per define section. Abort installation\n");
                return -1;
            }
            domainPresent = 1;
            name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
            policy = xmlTextReaderGetAttribute(reader, XMLCHAR("policy"));
            plist = xmlTextReaderGetAttribute(reader, XMLCHAR("plist"));
            rpmlog(RPMLOG_DEBUG, "domain %s policy %s plist %s\n", 
                   ASCII(name), ASCII(policy), ASCII(plist));

            if (name) {	
                if (msmVerifySmackLabel(ASCII(name)) < 0) {
                    msmFreePointer((void**)&name);
                    msmFreePointer((void**)&policy);		
                    msmFreePointer((void**)&plist);
                    return -1; 
                }

                define->name = ASCII(name);
                define->policy = ASCII(policy);
                define->plist = ASCII(plist);
                // store defined ac domain name 
                ac_domain_x *ac_domain = calloc(1, sizeof(ac_domain_x));
                if (ac_domain) {
                    if (define->name) {
                        ac_domain->name = strdup(define->name);
                    }
                    ac_domain->match = strdup("trusted"); // hardcode trusted policy for ac domain definition
                    if (define->policy) {
                        ac_domain->type = strdup(define->policy);
                    }	
                    if (define->plist) {
                        ac_domain->plist = strdup(define->plist);
                    }				  
                    ac_domain->sw_source = current;
                    ac_domain->pkg_name = mfx->name;
                    if (!mfx->provides){
                        provide_x *provide = calloc(1, sizeof(provide_x));
                        if (provide) {
                            LISTADD(mfx->provides, provide);
                        } else { 
                            if (ac_domain) {
                                msmFreeACDomain(ac_domain);
                                return -1;
                            }
                        }
                    }
                    LISTADD(mfx->provides->ac_domains, ac_domain);
                } else return -1;
            } else {
                rpmlog(RPMLOG_ERR, "Domain name must be defined. Abort installation\n");
                msmFreePointer((void**)&policy);	
                msmFreePointer((void**)&plist);
                return -1; 
            }
        } else if (!strcmp(ASCII(node), "request")) {
            int res = msmProcessDRequest(reader, define);
            if (res < 0) return res;
        } else if (!strcmp(ASCII(node), "permit")) {
            int res = msmProcessDPermit(reader, define);
            if (res < 0) return res;
        } else if (!strcmp(ASCII(node), "provide")) {
            int res = msmProcessDProvide(reader, define);
            if (res < 0) return res;
        } else {
            rpmlog(RPMLOG_ERR, "Not allowed element in domain define section: %s\n", ASCII(node));
            return -1;
        }
        if (ret < 0) return ret;
    }
    return ret;
}

int msmFindSWSourceByKey(sw_source_x *sw_source, void *param)
{
    origin_x *origin;
    keyinfo_x *keyinfo;
    keyinfo_x *current_keyinfo = (keyinfo_x*)param;

    for (origin = sw_source->origins; origin; origin = origin->prev) {
	    for (keyinfo = origin->keyinfos; keyinfo; keyinfo = keyinfo->prev) {
	        if (strncmp((const char*)current_keyinfo->keydata, (const char*)keyinfo->keydata, 
		    strlen((const char*)current_keyinfo->keydata)) == 0
                   && (current_keyinfo->keylen == keyinfo->keylen))
			return 0;
	    }
    }
    return 1;
}

int msmFindSWSourceByName(sw_source_x *sw_source, void *param)
{
    const char *name = (const char *)param;
    return strcmp(sw_source->name, name); 
}

int msmFindSWSourceBySignature(sw_source_x *sw_source, void *param, void* param2)
{
    origin_x *origin;
    keyinfo_x *keyinfo;
    pgpDigParams sig = (pgpDigParams)param;
    DIGEST_CTX ctx = (DIGEST_CTX)param2;
    pgpDigParams key = NULL;

    for (origin = sw_source->origins; origin; origin = origin->prev) {
        for (keyinfo = origin->keyinfos; keyinfo; keyinfo = keyinfo->prev) {
	    if (pgpPrtParams(keyinfo->keydata, keyinfo->keylen, PGPTAG_PUBLIC_KEY, &key)) {
                rpmlog(RPMLOG_ERR, "invalid sw source key\n");
                return -1;
            }
            if (pgpVerifySignature(key, sig, ctx) == RPMRC_OK) {
                return 0;
            }
        }
    }
    return 1;
}

static int msmProcessKeyinfo(xmlTextReaderPtr reader, origin_x *origin, sw_source_x *parent) 
{
    const xmlChar *keydata;
    keyinfo_x *keyinfo;
    int ret, depth;

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
	keydata = xmlTextReaderConstValue(reader);
	rpmlog(RPMLOG_DEBUG, "keyinfo %.40s...\n", ASCII(keydata));
	if (!keydata) return -1;
	keyinfo = calloc(1, sizeof(keyinfo_x));
	if (keyinfo) {
	    if ((ret = rpmBase64Decode(ASCII(keydata), (void **)&keyinfo->keydata, &keyinfo->keylen))) {
		rpmlog(RPMLOG_ERR, "Failed to decode keyinfo %s, %d\n", keydata, ret);
		ret = -1;
	    }
	    if (!ret) {
		// check that keys aren't matching
		sw_source_x *old = msmSWSourceTreeTraversal(parent, msmFindSWSourceByKey, (void *)keyinfo, NULL);
		if (old) {
		    rpmlog(RPMLOG_ERR, "SW source with this key has already been installed\n");
		    return -1; 
		}
	    }
	    LISTADD(origin->keyinfos, keyinfo);
	} else return -1;

	if (ret < 0) return ret;
    }
    return ret;
}

static access_x *msmProcessAccess(xmlTextReaderPtr reader, origin_x *origin) 
{
    const xmlChar *data, *type;

    data = xmlTextReaderGetAttribute(reader, XMLCHAR("data"));
    type = xmlTextReaderGetAttribute(reader, XMLCHAR("type"));
    rpmlog(RPMLOG_DEBUG, "access %s %s\n", ASCII(data), ASCII(type));

    if (data) {
	access_x *access = calloc(1, sizeof(access_x));
	if (access) {
	    access->data = ASCII(data);
	    access->type = ASCII(type);
	    return access;
	}
    }
    msmFreePointer((void**)&data);
    msmFreePointer((void**)&type);
    return NULL;
}

static int msmProcessOrigin(xmlTextReaderPtr reader, origin_x *origin, sw_source_x *parent)  
{
    const xmlChar *node, *type;
    int ret, depth;

    type = xmlTextReaderGetAttribute(reader, XMLCHAR("type"));
    rpmlog(RPMLOG_DEBUG, "origin %s\n", ASCII(type));
    origin->type = ASCII(type);

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "keyinfo")) {
	    ret = msmProcessKeyinfo(reader, origin, parent);
	} else if (!strcmp(ASCII(node), "access")) {
	    access_x *access = msmProcessAccess(reader, origin);
	    if (access) {
		LISTADD(origin->accesses, access);
	    } else return -1;
	} else return -1;

	if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessDeny(xmlTextReaderPtr reader, sw_source_x *sw_source) 
{
    const xmlChar *node;
    int ret, depth;

    rpmlog(RPMLOG_DEBUG, "deny\n");

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "ac_domain")) {
	    ac_domain_x *ac_domain = msmProcessACDomain(reader, sw_source, NULL);
	    if (ac_domain) {
		if (ac_domain->name) {
		    HASH_ADD_KEYPTR(hh, sw_source->denys, ac_domain->name, 
				    strlen(ac_domain->name), ac_domain);
		} else {
		    LISTADD(sw_source->denymatches, ac_domain);
		}
	    } else return -1;
	} else return -1;
	if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessAllow(xmlTextReaderPtr reader, sw_source_x *sw_source) 
{
    const xmlChar *node;    
    int ret, depth;

    rpmlog(RPMLOG_DEBUG, "allow\n");

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "deny")) {
	    ret = msmProcessDeny(reader, sw_source);
	} else if (!strcmp(ASCII(node), "ac_domain")) {
	    ac_domain_x *ac_domain = msmProcessACDomain(reader, sw_source, NULL);
	    if (ac_domain) {
		if (ac_domain->name) {
		    HASH_ADD_KEYPTR(hh, sw_source->allows, ac_domain->name, 
				    strlen(ac_domain->name), ac_domain);
		} else {
		    LISTADD(sw_source->allowmatches, ac_domain);
		}
	    } else return -1;
	} else return -1;
	if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessSWSource(xmlTextReaderPtr reader, sw_source_x *sw_source, const char *parentkey, manifest_x *mfx) 
{
    const xmlChar *name, *node, *rank, *rankkey;
    sw_source_x *current;
    int ret, depth, len;
    int rankval = 0;

    /* config processing */
    current = sw_source;

    name = xmlTextReaderGetAttribute(reader, XMLCHAR("name"));
    rank = xmlTextReaderGetAttribute(reader, XMLCHAR("rank"));
    rankkey = xmlTextReaderGetAttribute(reader, XMLCHAR("rankkey"));
    rpmlog(RPMLOG_DEBUG, "sw source %s rank %s key %s\n", 
	   ASCII(name), ASCII(rank), ASCII(rankkey));

    sw_source->name = ASCII(name);

    if (rankkey) {
	/* config processing */
	sw_source->rankkey = ASCII(rankkey);
    } else {
	if (rank) {
	    rankval = atoi(ASCII(rank));
	    msmFreePointer((void**)&rank); /* rankkey is used from now on */
	}
    }
    if (!sw_source->name) return -1; /* sw source must have name */
    if (!mfx && rankkey) return -1; /* manifest cannot set rankkey itself */

    if (!mfx) {
	sw_source_x *old = msmSWSourceTreeTraversal(sw_source->parent, msmFindSWSourceByName, (void *)sw_source->name, NULL);
	if (old && old->parent != sw_source->parent) {
	    if (!old->parent && old == sw_source->parent) {
		/* root sw source upgrade (it's signed by root) */
		parentkey = "";
	    } else {
		rpmlog(RPMLOG_ERR, "SW source called %s has already been installed\n", 
		       sw_source->name);
		return -1; /* sw_source names are unique (allow upgrade though) */
	    }
	}
	/* rank algorithm is copied from harmattan dpkg wrapper */
	if (rankval > RANK_LIMIT) rankval = RANK_LIMIT;
	if (rankval < -RANK_LIMIT) rankval = -RANK_LIMIT;
	rankval += RANK_LIMIT;

	len = strlen(parentkey) + 1 + 5 + 1 + 5 + 1 + strlen(sw_source->name) + 1;
	if (!(sw_source->rankkey = malloc(len))) return -1;
	sprintf((char *)sw_source->rankkey, "%s/%05d/%05d.%s", 
		parentkey, rankval, RANK_LIMIT, sw_source->name);
    }

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "allow")) {
	    ret = msmProcessAllow(reader, sw_source);
	} else if (!strcmp(ASCII(node), "deny")) {
	    ret = msmProcessDeny(reader, sw_source);
	} else if (!strcmp(ASCII(node), "origin")) {
	    origin_x *origin = calloc(1, sizeof(origin_x));
	    if (origin) {
		LISTADD(sw_source->origins, origin);
		ret = msmProcessOrigin(reader, origin, sw_source->parent);
	    } else return -1;
	} else if (!strcmp(ASCII(node), "package")) {
	    /* config processing */
	    if (!mfx) return -1;
	    package_x *package = calloc(1, sizeof(package_x));
	    if (package) {
		LISTADD(sw_source->packages, package);
		ret = msmProcessPackage(reader, package, current);
	    } else return -1;
	} else if (!strcmp(ASCII(node), "sw_source")) {
	    /* config processing */
	    if (!mfx) return -1;
	    sw_source_x *sw_source = calloc(1, sizeof(sw_source_x));
	    if (sw_source) {
		sw_source->parent = current;
		LISTADD(mfx->sw_sources, sw_source);
	    } else return -1;
	    ret = msmProcessSWSource(reader, sw_source, "", mfx);
	} else return -1;

	if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessAttributes(xmlTextReaderPtr reader, manifest_x *mfx) 
{
    const xmlChar *node, *type;
    int ret, depth, attributePresent = 0;

    rpmlog(RPMLOG_DEBUG, "attributes\n");
    depth = xmlTextReaderDepth(reader);

    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
        if (!node) return -1;
        if (!strcmp(ASCII(node), "package")) {
            if (attributePresent) {
                rpmlog(RPMLOG_ERR, "Only one attribute is currently allowed per attribute section. Abort installation\n");
                return -1;
            }
            attributePresent = 1;
            type = xmlTextReaderGetAttribute(reader, XMLCHAR("type"));
            rpmlog(RPMLOG_DEBUG, "package type is %s\n", ASCII(type));

            if (type) {	
                if ((strcmp(type, "system") != 0) &&
                    (strcmp(type, "application") != 0)){
                    rpmlog(RPMLOG_ERR, "Not allowed attribute name in a package type specification. Abort installation.\n");
                    msmFreePointer((void**)&type);
                    return -1; 
                }
                mfx->package_type = ASCII(type);	    
            } else {
                rpmlog(RPMLOG_ERR, "Type name must be defined. Abort installation\n");
                return -1; 
            }
        } else {
            rpmlog(RPMLOG_ERR, "Not allowed element in attribute section: %s\n", ASCII(node));
            return -1;
        }
        if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessMsm(xmlTextReaderPtr reader, manifest_x *mfx, sw_source_x *current)
{
    const xmlChar *node;
    int ret, depth;
    int assignPresent = 0, requestPresent = 0, attributesPresent = 0; /* there must be only one section per manifest */
    mfx->sw_source = current;

    rpmlog(RPMLOG_DEBUG, "manifest\n");

    depth = xmlTextReaderDepth(reader);
    while ((ret = msmNextChildElement(reader, depth))) {
        node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "assign")) {
            if (assignPresent) {
                rpmlog(RPMLOG_ERR, "A second assign section in manifest isn't allowed. Abort installation.\n");
                return -1; 
            }
            assignPresent = 1;
            provide_x *provide = calloc(1, sizeof(provide_x));
            if (provide) {
                LISTADD(mfx->provides, provide);
                ret = msmProcessProvide(reader, provide, current, mfx, NULL);
            } else return -1;
	} else if (!strcmp(ASCII(node), "attributes")) {
            if (attributesPresent) {
                rpmlog(RPMLOG_ERR, "A second attribute section in manifest isn't allowed. Abort installation.\n");
                return -1; 
            }
            attributesPresent = 1;
            ret = msmProcessAttributes(reader, mfx);
	} else if (!strcmp(ASCII(node), "define")) {
            define_x *define = calloc(1, sizeof(define_x));
            if (define) {
                LISTADD(mfx->defines, define);
                ret = msmProcessDefine(reader, define, mfx, current);
            } else return -1;
	} else if (!strcmp(ASCII(node), "request")) {
            if (requestPresent) {
                rpmlog(RPMLOG_ERR, "A second request section in manifest isn't allowed. Abort installation.\n");
                return -1; 
            }
            requestPresent = 1;
            mfx->request = calloc(1, sizeof(request_x));
            if (mfx->request) {
                ret = msmProcessRequest(reader, mfx->request);
            } else return -1;
	} else if (!strcmp(ASCII(node), "sw_source")) {
	    sw_source_x *sw_source = calloc(1, sizeof(sw_source_x));
	    if (sw_source) {
		char parentkey[256] = { 0 };
		sw_source->parent = current;
		if (sw_source->parent) {
		    snprintf(parentkey, sizeof(parentkey), 
			     "%s", sw_source->parent->rankkey);
		    char *sep = strrchr(parentkey, '/');
		    if (sep) *sep = '\0';
		}
		LISTADD(mfx->sw_sources, sw_source);
		ret = msmProcessSWSource(reader, sw_source, parentkey, NULL);
	    } else return -1;
	} else return -1;
	if (ret < 0) return ret;
    }
    return ret;
}

static int msmProcessConfig(xmlTextReaderPtr reader, manifest_x *mfx)
{
    const xmlChar *node;
    int ret, depth;

    rpmlog(RPMLOG_DEBUG, "config\n");

    depth = xmlTextReaderDepth(reader);
    if ((ret = msmNextChildElement(reader, depth))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "sw_source")) {
	    mfx->sw_sources = calloc(1, sizeof(sw_source_x));
	    if (!mfx->sw_sources) return -1;
	    ret = msmProcessSWSource(reader, mfx->sw_sources, "", mfx);
	} else return -1;
    }
    return ret;
}

static int msmProcessManifest(xmlTextReaderPtr reader, manifest_x *mfx, sw_source_x *current) 
{
    const xmlChar *node;
    int ret;

    if ((ret = msmNextChildElement(reader, -1))) {
	node = xmlTextReaderConstName(reader);
	if (!node) return -1;
	if (!strcmp(ASCII(node), "manifest")) {
	    ret = msmProcessMsm(reader, mfx, current);
	} else if (!strcmp(ASCII(node), "config")) {
	    ret = msmProcessConfig(reader, mfx);
	} else return -1;
    }
    return ret;
}

static filesystem_x *msmFreeFilesystem(filesystem_x *filesystem)
{    
    if (filesystem) {
        filesystem_x *prev = filesystem->prev;
        msmFreePointer((void**)&filesystem->path);
        msmFreePointer((void**)&filesystem->label);
        msmFreePointer((void**)&filesystem->exec_label);
        msmFreePointer((void**)&filesystem->type);
        msmFreePointer((void**)&filesystem);
        return prev;
    } else
        return NULL;
}

static member_x *msmFreeMember(member_x *member)
{ 
    if (member) {
        member_x *prev = member->prev;
        msmFreePointer((void**)&member->name);
        if (member->annotation) {
            msmFreePointer((void**)&member->annotation->name);
            msmFreePointer((void**)&member->annotation->value);
            msmFreePointer((void**)&member->annotation);
        }
        msmFreePointer((void**)&member);
        return prev;
    } else
        return NULL;
}

static interface_x *msmFreeInterface(interface_x *interface)
{
    member_x *member;

    if (interface) {
        interface_x *prev = interface->prev;
        msmFreePointer((void**)&interface->name);
        if (interface->annotation) {
            msmFreePointer((void**)&interface->annotation->name);
            msmFreePointer((void**)&interface->annotation->value);
            msmFreePointer((void**)&interface->annotation);
        }
        for (member = interface->members; member; member = msmFreeMember(member));
        msmFreePointer((void**)&interface);
        return prev;
    } else
        return NULL;
}

static node_x *msmFreeNode(node_x *node)
{    
    member_x *member;
    interface_x *interface;

    if (node) {
        node_x *prev = node->prev;
        msmFreePointer((void**)&node->name);
        if (node->annotation) {
            msmFreePointer((void**)&node->annotation->name);
            msmFreePointer((void**)&node->annotation->value);
            msmFreePointer((void**)&node->annotation);
        }
        for (member = node->members; member; member = msmFreeMember(member));
	for (interface = node->interfaces; interface; interface = msmFreeInterface(interface));
        msmFreePointer((void**)&node);
        return prev;
    } else
        return NULL;
}

static dbus_x *msmFreeDBus(dbus_x *dbus)
{
    node_x *node;

    if (dbus) {
        dbus_x *prev = dbus->prev;
        msmFreePointer((void**)&dbus->name);
        msmFreePointer((void**)&dbus->own);
        msmFreePointer((void**)&dbus->bus);
        if (dbus->annotation) {
            msmFreePointer((void**)&dbus->annotation->name);
            msmFreePointer((void**)&dbus->annotation->value);
            msmFreePointer((void**)&dbus->annotation);
        }
        for (node = dbus->nodes; node; node = msmFreeNode(node));
        msmFreePointer((void**)&dbus);
        return prev;
    } else return NULL;
}

static provide_x *msmFreeProvide(provide_x *provide) 
{
    ac_domain_x *ac_domain;
    filesystem_x *filesystem;
    provide_x *prev = provide->prev;
    dbus_x *dbus;

    if (provide) {
        for (ac_domain = provide->ac_domains; ac_domain; ac_domain = msmFreeACDomain(ac_domain));
        if (provide->filesystems)
            for (filesystem = provide->filesystems; filesystem; filesystem = msmFreeFilesystem(filesystem));
        msmFreePointer((void**)&provide->name);
        msmFreePointer((void**)&provide->origin);
        for (dbus = provide->dbuss; dbus; dbus = msmFreeDBus(dbus));
        msmFreePointer((void**)&provide);
    }
    return prev;
}

static file_x *msmFreeFile(file_x *file)
{
    file_x *prev = file->prev;
    msmFreePointer((void**)&file->path);
    msmFreePointer((void**)&file);
    return prev;
}

package_x *msmFreePackage(package_x *package)
{
    provide_x *provide;
    package_x *prev = package->prev;
    for (provide = package->provides; provide; provide = msmFreeProvide(provide));
    msmFreePointer((void**)&package->name);
    msmFreePointer((void**)&package->modified);
    msmFreePointer((void**)&package);
    return prev;
}

static keyinfo_x *msmFreeKeyinfo(keyinfo_x *keyinfo)
{
    keyinfo_x *prev = keyinfo->prev;
    msmFreePointer((void**)&keyinfo->keydata);
    msmFreePointer((void**)&keyinfo);
    return prev;
}

static access_x *msmFreeAccess(access_x *access)
{
    access_x *prev = access->prev;
    msmFreePointer((void**)&access->data);
    msmFreePointer((void**)&access->type);
    msmFreePointer((void**)&access);
    return prev;
}

static origin_x *msmFreeOrigin(origin_x *origin)
{
    keyinfo_x *keyinfo;
    access_x *access;
    origin_x *prev = origin->prev;
    for (keyinfo = origin->keyinfos; keyinfo; keyinfo = msmFreeKeyinfo(keyinfo));
    for (access = origin->accesses; access; access = msmFreeAccess(access));
    msmFreePointer((void**)&origin->type);
    msmFreePointer((void**)&origin);
    return prev;
}

static sw_source_x *msmFreeSWSource(sw_source_x *sw_source)
{
    package_x *package;
    ac_domain_x *ac_domain, *temp;
    origin_x *origin;
    sw_source_x *next = sw_source->next;

    rpmlog(RPMLOG_DEBUG, "freeing sw source %s\n", sw_source->name);

    for (package = sw_source->packages; package; package = msmFreePackage(package));
    for (ac_domain = sw_source->allowmatches; ac_domain; ac_domain = msmFreeACDomain(ac_domain));
    if (sw_source->allows) {
	HASH_ITER(hh, sw_source->allows, ac_domain, temp) {
	    HASH_DELETE(hh, sw_source->allows, ac_domain);
	    msmFreeACDomain(ac_domain);
	}
    }

    for (ac_domain = sw_source->denymatches; ac_domain; ac_domain = msmFreeACDomain(ac_domain));
    if (sw_source->denys) {
	HASH_ITER(hh, sw_source->denys, ac_domain, temp) {
	    HASH_DELETE(hh, sw_source->denys, ac_domain);
	    msmFreeACDomain(ac_domain);
	}
    }
    for (origin = sw_source->origins; origin; origin = msmFreeOrigin(origin));
    msmFreePointer((void**)&sw_source->name);
    msmFreePointer((void**)&sw_source->rankkey);
    msmFreePointer((void**)&sw_source);
    return next;
}

static d_request_x *msmFreeDRequest(d_request_x *d_request)
{
    d_request_x *next = d_request->next;
    rpmlog(RPMLOG_DEBUG, "freeing domain request %s\n", d_request->label_name);
    msmFreePointer((void**)&d_request->label_name);
    msmFreePointer((void**)&d_request->ac_type);
    msmFreePointer((void**)&d_request);
    return next;
}

static d_permit_x *msmFreeDPermit(d_permit_x *d_permit)
{
    d_permit_x *next = d_permit->next;
    rpmlog(RPMLOG_DEBUG, "freeing domain permit %s\n", d_permit->label_name);
    msmFreePointer((void**)&d_permit->label_name);
    msmFreePointer((void**)&d_permit->to_label_name);
    msmFreePointer((void**)&d_permit->ac_type);
    msmFreePointer((void**)&d_permit);
    return next;
}

static d_provide_x *msmFreeDProvide(d_provide_x *d_provide)
{
    d_provide_x *next = d_provide->next;
    rpmlog(RPMLOG_DEBUG, "freeing domain provide %s\n", d_provide->label_name);
    msmFreePointer((void**)&d_provide->label_name);
    msmFreePointer((void**)&d_provide);
    return next;
}

static define_x *msmFreeDefine(define_x *define)
{
    define_x *next = define->next;
    d_request_x *d_request;
    d_permit_x *d_permit;
    d_provide_x *d_provide;

    msmFreePointer((void**)&define->name);
    msmFreePointer((void**)&define->policy);
    msmFreePointer((void**)&define->plist);

    if (define->d_requests) {
	LISTHEAD(define->d_requests, d_request);	
	for (; d_request; d_request = msmFreeDRequest(d_request));
    }
    rpmlog(RPMLOG_DEBUG, "after freeing define requests\n");
    if (define->d_permits) {
	LISTHEAD(define->d_permits, d_permit);	
	for (; d_permit; d_permit = msmFreeDPermit(d_permit));
    }
    rpmlog(RPMLOG_DEBUG, "after freeing define permits\n");
    if (define->d_provides) {
	LISTHEAD(define->d_provides, d_provide);	
	for (; d_provide; d_provide = msmFreeDProvide(d_provide));
    }
    rpmlog(RPMLOG_DEBUG, "after freeing provides\n");
    msmFreePointer((void**)&define); 
    return next;
}

manifest_x* msmFreeManifestXml(manifest_x* mfx)
{
    provide_x *provide;
    file_x *file;
    sw_source_x *sw_source;
    define_x *define;

    rpmlog(RPMLOG_DEBUG, "in msmFreeManifestXml\n");
    if (mfx) {
	if (mfx->provides)
            for (provide = mfx->provides; provide; provide = msmFreeProvide(provide));
        rpmlog(RPMLOG_DEBUG, "after freeing provides\n");
        if (mfx->request) {
            msmFreePointer((void**)&mfx->request->ac_domain);
            msmFreePointer((void**)&mfx->request);
        }
        rpmlog(RPMLOG_DEBUG, "after freeing requests\n");
	for (file = mfx->files; file; file = msmFreeFile(file));
        rpmlog(RPMLOG_DEBUG, "after freeing files\n");
	if (mfx->sw_sources) {
            LISTHEAD(mfx->sw_sources, sw_source);	
            for (; sw_source; sw_source = msmFreeSWSource(sw_source));
	}
	msmFreePointer((void**)&mfx->name);
        rpmlog(RPMLOG_DEBUG, "after freeing name\n");
        if (mfx->defines) {
            LISTHEAD(mfx->defines, define);
            for (; define; define = msmFreeDefine(define));            
        }
        rpmlog(RPMLOG_DEBUG, "after freeing defines \n");
        msmFreePointer((void**)&mfx);
    }
    return mfx; 
}

manifest_x *msmProcessManifestXml(const char *buffer, int size, sw_source_x *current, const char *packagename) 
{
    xmlTextReaderPtr reader;
    manifest_x *mfx = NULL;

    reader = xmlReaderForMemory(buffer, size, NULL, NULL, 0);
    if (reader) {
	mfx = calloc(1, sizeof(manifest_x));
	if (mfx) {
	    mfx->name = strdup(packagename);
	    if (msmProcessManifest(reader, mfx, current) < 0) {
            /* error in parcing. Let's display some hint where we failed */
           	rpmlog(RPMLOG_DEBUG, "Syntax error in processing manifest in the above line\n");
		mfx = msmFreeManifestXml(mfx);
	    }
	}
	xmlFreeTextReader(reader);
    } else {
        rpmlog(RPMLOG_ERR, "Unable to create xml reader\n");
    }
    return mfx;
}

manifest_x *msmProcessDevSecPolicyXml(const char *filename) 
{
    xmlTextReaderPtr reader;
    manifest_x *mfx = NULL;

    reader = xmlReaderForFile(filename, NULL, 0);
    if (reader) {
	mfx = calloc(1, sizeof(manifest_x));
	if (mfx) {
	    if (msmProcessManifest(reader, mfx, NULL) < 0) {
		mfx = msmFreeManifestXml(mfx);
	    }
	}
        xmlFreeTextReader(reader);
    } else {
        rpmlog(RPMLOG_ERR, "Unable to open device security policy %s\n", filename);
    }
    return mfx;
}
