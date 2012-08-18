/* 
   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License  
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.

*/

#include "cf3.defs.h"

#include "env_context.h"
#include "constraints.h"
#include "vars.h"
#include "expand.h"

/*****************************************************************************/

void VerifyMethodsPromise(Promise *pp, const ReportContext *report_context)
{
    Attributes a = { {0} };

    a = GetMethodAttributes(pp);

    VerifyMethod("usebundle", a, pp, report_context);
    DeleteScalar("this", "promiser");
}

/*****************************************************************************/

int VerifyMethod(char *attrname, Attributes a, Promise *pp, const ReportContext *report_context)
{
    Bundle *bp;
    void *vp;
    FnCall *fp;
    char method_name[CF_EXPANDSIZE],*method_deref;
    Rlist *params = NULL;
    int retval = false;
    CfLock thislock;
    char lockname[CF_BUFSIZE];

    if (a.havebundle)
    {
        if ((vp = GetConstraintValue(attrname, pp, CF_FNCALL)))
        {
            fp = (FnCall *) vp;
            ExpandScalar(fp->name, method_name);
            params = fp->args;
        }
        else if ((vp = GetConstraintValue(attrname, pp, CF_SCALAR)))
        {
            ExpandScalar((char *) vp, method_name);
            params = NULL;
        }
        else
        {
            return false;
        }
    }

    GetLockName(lockname, "method", pp->promiser, params);

    thislock = AcquireLock(lockname, VUQNAME, CFSTARTTIME, a, pp, false);

    if (thislock.lock == NULL)
    {
        return false;
    }

    PromiseBanner(pp);

    if (strncmp(method_name,"default.",strlen("default.")) == 0)
       {
       method_deref = strchr(method_name,'.') + 1;
       }
    else
       {
       method_deref = method_name;
       }

    if ((bp = GetBundle(PolicyFromPromise(pp), method_deref, "agent")))
    {
        const char *bp_stack = THIS_BUNDLE;

        BannerSubBundle(bp, params);

        DeleteScope(bp->name);
        NewScope(bp->name);
        HashVariables(PolicyFromPromise(pp), bp->name, report_context);

        char namespace[CF_BUFSIZE];
        snprintf(namespace,CF_BUFSIZE,"%s_meta",bp->name);
        NewScope(namespace);
        
        SetBundleOutputs(bp->name);

        AugmentScope(bp->name, bp->args, params);

        THIS_BUNDLE = bp->name;
        PushPrivateClassContext(a.inherit);

        retval = ScheduleAgentOperations(bp, report_context);

        PopPrivateClassContext();
        THIS_BUNDLE = bp_stack;

        switch (retval)
        {
        case CF_FAIL:
            cfPS(cf_inform, CF_FAIL, "", pp, a, " !! Method failed in some repairs or aborted\n");
            break;

        case CF_CHG:
            cfPS(cf_inform, CF_CHG, "", pp, a, " !! Method invoked repairs\n");
            break;

        default:
            cfPS(cf_verbose, CF_NOP, "", pp, a, " -> Method verified\n");
            break;

        }

        DeleteFromScope(bp->name, bp->args);
    }
    else
    {
        if (IsCf3VarString(method_name))
        {
            CfOut(cf_error, "",
                  " !! A variable seems to have been used for the name of the method. In this case, the promiser also needs to contain the unique name of the method");
        }
        if (bp && bp->name)
        {
            cfPS(cf_error, CF_FAIL, "", pp, a, " !! Method \"%s\" was used but was not defined!\n", bp->name);
        }
        else
        {
            cfPS(cf_error, CF_FAIL, "", pp, a,
                 " !! A method attempted to use a bundle \"%s\" that was apparently not defined!\n", method_name);
        }
    }

    YieldCurrentLock(thislock);
    return retval;
}
