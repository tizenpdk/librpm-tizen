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

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <sys/stat.h>

#include <rpm/rpmfileutil.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmpgp.h>
#include <rpm/rpmkeyring.h>
#include <rpm/rpmdb.h>
#include <lib/rpmts_internal.h>

#include "rpmio/rpmbase64.h"
#include "rpmio/rpmlog.h"
#include "rpm/rpmfileutil.h"

#include "msm.h"

/*hooks that are used in msm plugin */

rpmPluginHook PLUGIN_HOOKS = \
	PLUGINHOOK_INIT | \
	PLUGINHOOK_CLEANUP | \
        PLUGINHOOK_TSM_PRE | \
        PLUGINHOOK_TSM_POST | \
        PLUGINHOOK_PSM_PRE | \
        PLUGINHOOK_PSM_POST | \
        PLUGINHOOK_FSM_COMMIT | \
        PLUGINHOOK_FSM_INIT | \
        PLUGINHOOK_FILE_CONFLICT | \
        PLUGINHOOK_VERIFY;


typedef struct fileconflict {
    const char *path;
    const char *pkg_name;
    sw_source_x *sw_source;
    UT_hash_handle hh;
} fileconflict;

typedef struct packagecontext {
    char *data;	        			/*!< base64 manifest data */
    manifest_x *mfx;     			/*!< parsed manifest data */
    rpmte te;                   		/*!< related te */
    struct packagecontext *next;		/*!< next in linked list */
    struct smack_accesses *smack_accesses;	/*!<  handle to smack_accesses */
} packagecontext;

static rpmts ts = NULL;
static int rootSWSource= 0;
static manifest_x *root = NULL; /* pointer to device security policy file */
static packagecontext *context = NULL;
static sw_source_x *current = NULL;
static packagecontext *contextsHead = NULL;
static packagecontext *contextsTail = NULL;
static fileconflict *allfileconflicts = NULL;
static char* ownSmackLabel = NULL;
static int SmackEnabled = 0;
static magic_t cookie = NULL;
static int package_created = 0;

rpmRC PLUGINHOOK_INIT_FUNC(rpmts _ts, const char *name, const char *opts)
{
    ts = _ts;
    int res = 0;
    char *fullPath = NULL, *fullPath1 = NULL;

    if (!ts)
        return RPMRC_FAIL;
    
    fullPath = rpmGenPath(ts->rootDir, DEVICE_SECURITY_POLICY, NULL);
    rpmlog(RPMLOG_DEBUG, "fullPath %s\n", fullPath);
    if (!fullPath) {
        rpmlog(RPMLOG_ERR, "Building a full path failed for device security policy\n");
        return RPMRC_FAIL;
    }

#ifndef ENABLE_DCHECKS
    rpmlog(RPMLOG_DEBUG, "ENABLE_DCHECKS is undefined!\n");
#else
    rpmlog(RPMLOG_DEBUG, "ENABLE_DCHECKS is defined!\n");
#endif

    rpmlog(RPMLOG_DEBUG, "reading device security policy from %s\n", fullPath);
    root = msmProcessDevSecPolicyXml(fullPath);

    if (root) {
        if (msmSetupSWSources(NULL, root, NULL)) {
	    rpmlog(RPMLOG_ERR, "Failed to setup device security policy from %s\n", 
		   fullPath);
            return RPMRC_FAIL;
        }
    } else {
        /* Do not allow plug-in to proceed without security policy existing */
        rpmlog(RPMLOG_ERR, "Failed to process sw sources from %s\n", 
	       fullPath);
        return RPMRC_FAIL;
    }

    msmFreePointer((void**)&fullPath);

    fullPath = rpmGenPath(ts->rootDir, SMACK_LOAD_PATH, NULL);
    rpmlog(RPMLOG_DEBUG, "fullPath for SMACK_LOAD_PATH %s\n", fullPath);
    if (!fullPath) {
        rpmlog(RPMLOG_ERR, "Building a full path for smack load failed\n");
        return RPMRC_FAIL;
    }

    /* check its own security context and store it for the case when packages without manifest will be installed */
    struct stat buf;

    if (stat(fullPath, &buf) == 0) {
        res = smack_new_label_from_self(&ownSmackLabel);
        SmackEnabled = 1;
        if (res != 0) {
            rpmlog(RPMLOG_ERR, "Failed to obtain rpm security context\n");
            return RPMRC_FAIL;
        }
    } else {
        rpmlog(RPMLOG_INFO, "Smackfs isn't mounted at %s. Going to the image build mode. \n", fullPath);
        ownSmackLabel = strdup("_");
        SmackEnabled = 0;
    }

    msmFreePointer((void**)&fullPath);    
    fullPath = rpmGenPath(ts->rootDir, SMACK_RULES_PATH, NULL);
    fullPath1 = rpmGenPath(ts->rootDir, SMACK_RULES_PATH_BEG, NULL);
    rpmlog(RPMLOG_DEBUG, "fullPath for SMACK_RULES_PATH %s\n", fullPath);
    rpmlog(RPMLOG_DEBUG, "fullPath1 for SMACK_RULES_PATH_BEG %s\n", fullPath1);
    if ((!fullPath) || (!fullPath1)){
        rpmlog(RPMLOG_ERR, "Building a full path failed for smack rules path\n");
    	return RPMRC_FAIL;
    }

    if (stat(fullPath, &buf) != 0) {
        rpmlog(RPMLOG_DEBUG, "A directory for writing smack rules is missing. Creating one.\n");
        mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH; // 644 -rwer--r--
        if (stat(fullPath1, &buf) != 0) {
            if (mkdir(fullPath1, mode) != 0) {
                rpmlog(RPMLOG_ERR, "Failed to create a sub-directory for smack rules\n");
                return RPMRC_FAIL;
            }    
        }
        if (mkdir(fullPath, mode) != 0){
            rpmlog(RPMLOG_ERR, "Failed to create a directory for smack rules\n");
            return RPMRC_FAIL;
        } 
    }

    msmFreePointer((void**)&fullPath);    
    msmFreePointer((void**)&fullPath1);    

    rpmlog(RPMLOG_DEBUG, "rpm security context: %s\n", ownSmackLabel);

    cookie = magic_open(0); 
    if (!cookie)
        return RPMRC_FAIL; 

    if (magic_load(cookie, NULL) != 0) {
        rpmlog(RPMLOG_ERR, "cannot load magic database - %s\n", magic_error(cookie));	
        magic_close(cookie);
        cookie = NULL;
        return RPMRC_FAIL;
    }

    return RPMRC_OK;
}

static int findSWSourceByName(sw_source_x *sw_source, void *param, void* param2)
{
    const char *name = (const char *)param;
    return strcmp(sw_source->name, name); 
}

rpmRC PLUGINHOOK_FILE_CONFLICT_FUNC(rpmts ts, char* path,
				      Header oldHeader, rpmfi oldFi, 
				      int rpmrc)
{
    fileconflict *fc;
    if (!path)
        return rpmrc; 
    rpmlog(RPMLOG_DEBUG, "FILE_CONFLICT_FUNC hook  path %s\n",path);

    const char *name = headerGetString(oldHeader, RPMTAG_SECSWSOURCE);    
    if (!name || !root) {
        return rpmrc; /* no sw source(s) - abnormal state */
    }
    const char *pkg_name = headerGetString(oldHeader, RPMTAG_NAME);

    sw_source_x *sw_source = msmSWSourceTreeTraversal(root->sw_sources, findSWSourceByName, (void *)name, NULL);
    if (!sw_source)
        return rpmrc; /* no old sw_source - abnormal state */

    HASH_FIND(hh, allfileconflicts, path, strlen(path), fc);
    if (!fc) {
        /* Add new file conflict into hash */
        fc = xcalloc(1, sizeof(*fc));
        if (!fc) return RPMRC_FAIL;
        fc->path = path;
        fc->sw_source = sw_source;
        fc->pkg_name = pkg_name;
        HASH_ADD_KEYPTR(hh, allfileconflicts, path, strlen(path), fc);
    } else {
        /* Many packages have installed the same file */
        if (strcmp(sw_source->rankkey, fc->sw_source->rankkey) <= 0) {
            /* Change sw source to the higher ranked one */
            fc->sw_source = sw_source;
        }
        msmFreePointer((void**)&path);
    }

    if (rpmtsFilterFlags(ts) & RPMPROB_FILTER_REPLACEOLDFILES) {
        /* Conflict has been noted, now return ok. It will be actually */
        /* resolved later when conflicting package signature is verified */
        /* and sw_source is known. */
        return rpmrc;
    }
    return rpmrc;
}

rpmRC PLUGINHOOK_TSM_PRE_FUNC(rpmts ts)
{
    if (!root) {
        rpmlog(RPMLOG_DEBUG, "Policy is missing. Ending transaction\n");
        return RPMRC_FAIL;
    }

    return RPMRC_OK;
}

static int findSWSourceBySignature(sw_source_x *sw_source, void *param, void* param2)
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

rpmRC PLUGINHOOK_VERIFY_FUNC(rpmKeyring keyring, rpmtd sigtd, pgpDigParams sig, DIGEST_CTX ctx, int rpmrc)
{
    current = NULL;

#if 0 
    if (!root) {
        if (rpmrc == RPMRC_NOKEY) {
            rpmlog(RPMLOG_INFO, "package verified as root sw source\n");
            rootSWSource = 1; /* accept any signed package as root */
            return RPMRC_OK;
        }
        rpmlog(RPMLOG_ERR, "No device security policy, cannot verify signature\n");
        return rpmrc;
    } 

    // make currently that even non-signed package with root policy will be treated as trusted

    if (!root) {
        rpmlog(RPMLOG_INFO, "package verified as root sw source\n");
        rootSWSource = 1; /* accept any signed package as root */
        return RPMRC_OK;
    } 

    //------------------

#endif

    if (!root) {
        rpmlog(RPMLOG_ERR, "No device policy found\n");
        rootSWSource = 1; /* accept any signed package as root */
        return rpmrc;
    } 

    if (rpmrc == RPMRC_NOKEY) {
        /* No key, revert to unknown sw source. */
        rpmlog(RPMLOG_ERR, "no key for signature, cannot search sw source\n");
        goto exit;
    }
    if (rpmrc) {
        /* RPM failed to verify signature */
        rpmlog(RPMLOG_ERR, "Invalid signature, cannot search sw source\n");
        return rpmrc;
    }
    if (sigtd->tag != RPMSIGTAG_RSA) {
        /* Not RSA, revert to unknown sw source. */
        rpmlog(RPMLOG_DEBUG, "no RSA signature, cannot search sw source\n");
        goto exit;
    }

    current = msmSWSourceTreeTraversal(root->sw_sources, findSWSourceBySignature, sig, ctx);
    if (current)
        rpmlog(RPMLOG_DEBUG, "signature matches sw source %s\n", current->name);
    else
        rpmlog(RPMLOG_DEBUG, "valid signature but no matching sw source\n");

 exit:
    if (!current) {
        current = msmSWSourceTreeTraversal(root->sw_sources, findSWSourceByName, (void *)"_default_", NULL);
        if (current) {
            rpmlog(RPMLOG_DEBUG, "using _default_ sw source\n");
        } else { // for now in case default sw source isn't there yet, allow to think that it is coming from root
            current = msmSWSourceTreeTraversal(root->sw_sources, findSWSourceByName, (void *)"root", NULL);
            if (current)
                rpmlog(RPMLOG_DEBUG, "using _root_ sw source now for testing\n");
        }
    }

    return rpmrc;
}

static packagecontext *msmNew(rpmte te)
{
    Header h;
    struct rpmtd_s msm;
    int count;
    packagecontext *ctx = NULL;
    const char *sw_source = NULL;

    rpmtdReset(&msm);
    
    h = rpmteHeader(te);
    if (!h) return NULL;
    
    ctx = xcalloc(1, sizeof(*ctx));
    if (!ctx) {
        goto exit1;
    }
    ctx->te = te;

    if (!headerIsEntry(h, RPMTAG_SECMANIFEST)) {
        goto exit1;
    }

    if (!headerGet(h, RPMTAG_SECMANIFEST, &msm, HEADERGET_MINMEM)) {
        goto exit1;
    }

    count = rpmtdCount(&msm);
    if (count != 1) {
        goto exit2;
    }

    ctx->data = xstrdup(rpmtdNextString(&msm));
    rpmlog(RPMLOG_DEBUG, "%s manifest b64 data: %.40s...\n", 
	   rpmteN(ctx->te), ctx->data);
 exit2:
    rpmtdFreeData(&msm);
 exit1:
    if (rpmteType(ctx->te) == TR_ADDED) {
        /* Save sw_source name into database, we need it when package */
        /* is removed because signature verify is not called then. */
        if (current) sw_source = current->name;
        else if (rootSWSource) sw_source = rpmteN(ctx->te);

        if (!sw_source || !headerPutString(h, RPMTAG_SECSWSOURCE, sw_source)) {
            rpmlog(RPMLOG_ERR, "Failed to save sw source for %s, sw_source: %s\n", 
		   rpmteN(ctx->te), sw_source);
            msmFreePointer((void**)&ctx->data);
            msmFreePointer((void**)&ctx);
        }
    }
    headerFree(h);

    return ctx;
}

static packagecontext *msmAddTE(rpmte te)
{
    packagecontext *ctx = msmNew(te);
    if (ctx) {
        /* add the new policy to the list */
        if (!contextsHead) {
            contextsHead = ctx;
            contextsTail = ctx;
        } else {
            if (rpmteType(te) == TR_ADDED) {
                /* add to the end of the list */
                contextsTail->next = ctx;
                contextsTail = ctx;
            } else {
                /* add to the beginning of the list */
                ctx->next = contextsHead;
                contextsHead = ctx;
            }
        }
    }
    return ctx;
}

rpmRC PLUGINHOOK_PSM_PRE_FUNC(rpmte te)
{
    packagecontext *ctx = NULL;
    manifest_x *mfx = NULL;
    char *xml = NULL;
    size_t xmllen;
    rpmRC rc = RPMRC_OK;
    int ret = 0;

    if (!root && !rootSWSource) {
        /* no sw source config, just exit */
        goto exit;
    }

    if (!current) {
        /* this means that verify hook has not been called */
        current = msmSWSourceTreeTraversal(root->sw_sources, findSWSourceByName, (void *)"_default_", NULL);
        if (current) {
            rpmlog(RPMLOG_DEBUG, "using _default_ sw source\n");
        } else { 
            rpmlog(RPMLOG_ERR, "Default source isn't availiable. Package source can't be determined. Abort installation\n");
            goto fail;
        }
    }

    ctx = msmAddTE(te);
    if (!ctx) {
        rpmlog(RPMLOG_ERR, "Failed to create security context for %s\n", rpmteNEVRA(te));
        goto fail;
    }

    if (rpmteType(ctx->te) == TR_REMOVED) {
        /* Verify hook is not called before remove, */
        /* so get the sw_source name from package header */
        Header h = rpmteHeader(te);
        if (h) {
            const char *name = headerGetString(h, RPMTAG_SECSWSOURCE);
            if (name) { 
                current = msmSWSourceTreeTraversal(root->sw_sources, findSWSourceByName, (void *)name, NULL);
                rpmlog(RPMLOG_DEBUG, "removing %s from sw source %s\n",
		       rpmteN(ctx->te), name);
            }
            headerFree(h);
        }
        /* if (!current) {
            rpmlog(RPMLOG_INFO, "no sw source for removing %s\n", rpmteN(ctx->te));
            goto exit;
        }*/
    }

    if (!ctx->data) {
        rpmlog(RPMLOG_INFO, "No manifest in this package. Creating default one\n");

        /* create default manifest manually. Make the package to belong to the domain where rpm is running */

        mfx = calloc(1, sizeof(manifest_x));
        if (!mfx)  goto fail;
        mfx->sw_source = current;
        mfx->name = strdup(rpmteN(ctx->te));
        mfx->request = calloc(1, sizeof(request_x));
        if (!mfx->request) {
            msmFreePointer((void**)&mfx->name);
            msmFreePointer((void**)&mfx);
            goto fail;
        }
        mfx->request->ac_domain = strdup(ownSmackLabel);
        rpmlog(RPMLOG_DEBUG, "Done with manifest creation\n");
    } else {
        if (rpmBase64Decode(ctx->data, (void **) &xml, &xmllen) != 0) {
            rpmlog(RPMLOG_ERR, "Failed to decode manifest for %s\n",
	           rpmteN(ctx->te));
            goto fail;
        }

        rpmlog(RPMLOG_DEBUG, "parsing %s manifest: \n%s", rpmteN(ctx->te), xml);
        mfx = msmProcessManifestXml(xml, xmllen, current, rpmteN(ctx->te));

        if (!mfx) {
            rpmlog(RPMLOG_ERR, "Failed to parse manifest for %s\n",
            rpmteN(ctx->te));
            goto fail;
        }
    }

    ctx->mfx = mfx;

    int res = smack_accesses_new(&(ctx->smack_accesses)); 
    if (res != 0) {
        rpmlog(RPMLOG_ERR, "Failed to create smack access set\n");
        goto fail;
    }

    if (rpmteType(ctx->te) == TR_ADDED) {
        rpmlog(RPMLOG_DEBUG, "Installing the package\n");
        package_x *package = NULL;

        if (rootSWSource) {
            /* this is the first package */
            package = msmCreatePackage(mfx->name, mfx->sw_sources, 
					    mfx->provides, NULL);
        } else if (mfx->sw_source) {
            /* all packages must have sw_source */
            package = msmCreatePackage(mfx->name, mfx->sw_source, 
					    mfx->provides, NULL);
        } else {
            rpmlog(RPMLOG_ERR, "Package doesn't have a sw source. Abnormal situation. Abort.\n");
            goto fail;
        }

        if (!package) {
            rpmlog(RPMLOG_ERR, "Package could not be created. \n");
            goto fail; 
        }
 
        mfx->provides = NULL; /* owned by package now */

        if (!package->sw_source) { /* this must never happen */
            rpmlog(RPMLOG_ERR, "Install failed. Check that configuration has at least root sw source installed.\n");
            msmFreePackage(package);
            package = NULL;
            goto fail;
	}

        rpmlog(RPMLOG_DEBUG, "adding %s manifest data to system, package_name %s\n", 
	       rpmteN(ctx->te), package->name);

        if (msmSetupPackages(ctx->smack_accesses, package, package->sw_source)) {
            rpmlog(RPMLOG_ERR, "Package setup failed for %s\n", rpmteN(ctx->te) );
            msmFreePackage(package);
            package = NULL;
            goto fail;
        }

        if (rootSWSource) {
            /* current is root */
            root = ctx->mfx;
        } 

        rpmlog(RPMLOG_DEBUG, "Starting the security setup...\n");
        unsigned int smackLabel = 0;

        if (rootSWSource || ctx->mfx->sw_source) {
            if (ctx->mfx->sw_sources) {
                smackLabel = 1; /* setting this one on since this manifest doesn't have any define/request section */
                ret = msmSetupSWSources(ctx->smack_accesses, ctx->mfx, ts);
                if (ret) {
                    rpmlog(RPMLOG_ERR, "SW source setup failed for %s\n",
			    rpmteN(ctx->te));
                    msmCancelPackage(ctx->mfx->name);
                    goto fail;
                }
            }           
            if (ctx->mfx->defines) {
                ret = msmSetupDefines(ctx->smack_accesses, ctx->mfx);
                if (ret) {
                    rpmlog(RPMLOG_ERR, "AC domain setup failed for %s\n",
                           rpmteN(ctx->te));
                    msmCancelPackage(ctx->mfx->name);
                    goto fail;
                } else {
		    smackLabel = 1;
		}
            }           
            if (ctx->mfx->request) {	
                if (ctx->mfx->request->ac_domain)
                    smackLabel = 1;
                ret = msmSetupRequests(ctx->mfx);
                if (ret) {
                    rpmlog(RPMLOG_ERR, "Request setup failed for %s\n",
                           rpmteN(ctx->te));
                    msmCancelPackage(ctx->mfx->name);
                    goto fail;
                }
            }
            if (ctx->smack_accesses) {
                ret = msmSetupSmackRules(ctx->smack_accesses, ctx->mfx->name, 0, SmackEnabled);
                smack_accesses_free(ctx->smack_accesses);
                ctx->smack_accesses = NULL;
                if (ret) {
                    rpmlog(RPMLOG_ERR, "Setting up smack rules for %s failed\n",
                           rpmteN(ctx->te));
                    msmCancelPackage(ctx->mfx->name);
                    goto fail; 
                }
            }
            if (package->provides) {
                ret = msmSetupDBusPolicies(package, ctx->mfx);
                if (ret) {
                    rpmlog(RPMLOG_ERR, "Setting up dbus policies for %s failed\n",
                           rpmteN(ctx->te));
                    msmCancelPackage(ctx->mfx->name);
                    goto fail;
                }
            }

            /* last check is needed in order to catch in advance 
               the situation when no ac domain defined or requested */
            if (smackLabel == 0) {
                rpmlog(RPMLOG_ERR, "No ac domain defined or requested for package %s. Abort.\n",   rpmteN(ctx->te));
                msmCancelPackage(ctx->mfx->name);
                goto fail;
            }
        }

    } else if (rpmteDependsOn(ctx->te)) { /* TR_REMOVED */
        rpmlog(RPMLOG_DEBUG, "upgrading package %s by %s\n",
               rpmteNEVR(ctx->te), rpmteNEVR(rpmteDependsOn(ctx->te)));
    } else if (mfx->sw_sources) {
        rpmlog(RPMLOG_ERR, "Cannot remove sw source package %s\n",
               rpmteN(ctx->te));
        goto fail;
    }

    rpmlog(RPMLOG_DEBUG, "Finished with pre psm hook \n");
    package_created = 1;

    goto exit;

 fail: /* error, cancel the rpm operation */
    rc = RPMRC_FAIL;

 exit: /* success, continue rpm operation */
    context = ctx;
    msmFreePointer((void**)&xml);

    return rc;
}

rpmRC PLUGINHOOK_FSM_INIT_FUNC(const char* path, mode_t mode)
{
    //check if there any conflicts that prevent file being written to the disk

    fileconflict *fc;
    packagecontext *ctx = context;
    char *cleanedPath = NULL, *dupPath = NULL;

    rpmlog(RPMLOG_DEBUG, "Started with FSM_INIT_FUNC hook for file: %s\n", path);

    if (!ctx) return RPMRC_FAIL; 
    if (!path) return RPMRC_FAIL; 

    dupPath = strdup(path);
    cleanedPath = strchr(dupPath, ';');
    if (cleanedPath)
        *cleanedPath = '\0';

    //rpmlog(RPMLOG_DEBUG, "dupapth: %s\n", dupPath);

    HASH_FIND(hh, allfileconflicts, dupPath, strlen(dupPath), fc);
    msmFreePointer((void**)&dupPath);

    if (fc) {
        //rpmlog(RPMLOG_DEBUG, "rpmteN(ctx->te) %s fc->pkg_name: %s\n", rpmteN(ctx->te), fc->pkg_name);
        /* There is a conflict, see if we are not allowed to overwrite */
        if ((!current || 
           (strcmp(current->rankkey, fc->sw_source->rankkey) >= 0)) &&
           (strcmp(rpmteN(ctx->te), fc->pkg_name))) {
            rpmlog(RPMLOG_ERR, "%s has file conflict in %s from sw source %s\n",
                   rpmteN(ctx->te), fc->path, fc->sw_source->name);
            return RPMRC_FAIL;
        }
        rpmlog(RPMLOG_DEBUG, "%s from %s overwrites %s from %s\n",
               rpmteN(ctx->te), current->name, fc->path, fc->sw_source->name);
    }

    rpmlog(RPMLOG_DEBUG, "Finished with FSM_INIT_FUNC hook for file: %s\n", path);

    return RPMRC_OK;
}

rpmRC PLUGINHOOK_FSM_COMMIT_FUNC(const char* path, mode_t mode, int type)
{
    packagecontext *ctx = context;
    if (!ctx) return RPMRC_FAIL;
    if (!path) return RPMRC_FAIL;

    /* the type is ignored for now */

    rpmlog(RPMLOG_DEBUG, "Started with FSM_COMMIT_FUNC hook for file: %s\n", path);

    if (ctx->mfx) {
        file_x *file = xcalloc(1, sizeof(*file));
        if (file) {
            file->path = strndup(path, strlen(path) + 1);
            LISTADD(ctx->mfx->files, file);
            if (rpmteType(ctx->te) == TR_ADDED) {
                if (msmSetFileXAttributes(ctx->mfx, file->path, cookie) < 0) {
                    rpmlog(RPMLOG_ERR, "Setting of extended attributes failed for file %s from package %s\n",
                           file->path, rpmteN(ctx->te));
                    return RPMRC_FAIL;
                }
            } 

        } else
            return RPMRC_FAIL;
    } else {
        rpmlog(RPMLOG_ERR, "Manifest is missing while it should be present for the package %s\n",
               rpmteN(ctx->te));
        return RPMRC_FAIL;
    }

    rpmlog(RPMLOG_DEBUG, "Finished with FSM_COMMIT_FUNC hook for file: %s\n", path);
    return RPMRC_OK;
}

rpmRC PLUGINHOOK_PSM_POST_FUNC(rpmte te, int rpmrc)
{

    int ret = 0;
    packagecontext *ctx = context;
    if (!ctx) return RPMRC_FAIL;

    if (!package_created) {
        /* failure in rpm pre psm hook, rollback */
        return RPMRC_FAIL;
    }

    if (rpmrc) {
        /* failure in rpm psm, rollback */
        if (rpmteType(ctx->te) == TR_ADDED)
            msmCancelPackage(ctx->mfx->name);
        goto exit;
    }

    if (!ctx->mfx){
        rpmlog(RPMLOG_ERR, "Manifest is missing while it should be present for the package %s\n",
               rpmteN(ctx->te));
        goto exit;
    }

    if (rootSWSource) {
        /* current is root */
        root = context->mfx;
    } 

    if (rpmteType(ctx->te) == TR_REMOVED) {
        if (ctx->mfx->sw_source) {
            if (rpmteDependsOn(ctx->te)) {
                rpmlog(RPMLOG_DEBUG, "upgrading %s manifest data\n", 
                       rpmteN(ctx->te));
            } else {
                rpmlog(RPMLOG_DEBUG, "removing %s manifest data\n", 
                       rpmteN(ctx->te));
                if (ctx->mfx->defines || ctx->mfx->provides || ctx->mfx->sw_sources) {
                    msmRemoveRules(ctx->smack_accesses, ctx->mfx, SmackEnabled);
                } 	    
                msmRemoveConfig(ctx->mfx);
            }
        }
    }

 exit:
    current = NULL;

    if (ret) {
        return RPMRC_FAIL;
    }
    return rpmrc;
}

rpmRC PLUGINHOOK_TSM_POST_FUNC(rpmts ts, int rpmrc)
{
    packagecontext *ctx = context;
    if (!ctx) return RPMRC_FAIL;
    return RPMRC_OK;
}

static packagecontext *msmFree(packagecontext *ctx)
{
    while (ctx) {
        packagecontext *next = ctx->next;
        msmFreePointer((void**)&ctx->data);
        ctx->mfx = msmFreeManifestXml(ctx->mfx);
        if (ctx->smack_accesses) smack_accesses_free(ctx->smack_accesses);
        msmFreePointer((void**)&ctx);
        ctx = next;
    }
    return NULL;
}

rpmRC PLUGINHOOK_CLEANUP_FUNC(void)
{

    msmFreeInternalHashes(); // free hash structures first

    if (root) {
        msmSaveDeviceSecPolicyXml(root);
        if (!rootSWSource) root = msmFreeManifestXml(root);
    }

    ts = NULL;

    contextsHead = contextsTail = msmFree(contextsHead);
    contextsHead = contextsTail = NULL;

    if (allfileconflicts) {
        fileconflict *fc, *temp;
        HASH_ITER(hh, allfileconflicts, fc, temp) {
            HASH_DELETE(hh, allfileconflicts, fc);
            msmFreePointer((void**)&fc->path);
            msmFreePointer((void**)&fc);
        }
    }

    msmFreePointer((void**)&ownSmackLabel);
    if (cookie) magic_close(cookie);

    return RPMRC_OK;
}

const char *msmQueryPackageFile(const char *rfor, 
				 const char **dname, const char **pname)
{
    int match = 0;
    const char *path = NULL;

    if (ts) {
        char *sep = strchr(rfor, ':');
        if (sep && sep[1] == ':' && sep[2] == '/') 
            path = &sep[2];
        if (!path) return NULL;

        rpmdbMatchIterator mi = rpmtsInitIterator(ts, RPMTAG_BASENAMES, path, 0);
        if (!mi)
            mi = rpmtsInitIterator(ts, RPMTAG_PROVIDENAME, path, 0);
        if (mi) {
            Header h;
            const char *name, *sw_source;
            while ((h = rpmdbNextIterator(mi))) {
                rpmdbCheckSignals();
                name = headerGetString(h, RPMTAG_NAME);
                sw_source = headerGetString(h, RPMTAG_SECSWSOURCE);
                if (name && sw_source) {
                    match = !strncmp(rfor, name, path - rfor - 2);
                    rpmlog(RPMLOG_DEBUG, "file %s belongs to package %s in sw source %s %s\n", 
                           path, name, sw_source, (match ? "(matched request)" : ""));
                    if (match) {
                        *pname = xstrdup(name);
                        *dname = xstrdup(sw_source);
                        break;
                    }
                }
            }
            mi = rpmdbFreeIterator(mi);
        }
    }
    return match ? path : NULL;
}

void msmFreePointer(void** ptr)
{
    if (*ptr)
        free(*ptr);
    *ptr = NULL;
    return;
}
