// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ============================================================
//
// BindingLog.cpp
//


//
// Implements the fusion-like BindingLog class
//
// ============================================================

#ifdef FEATURE_VERSIONING_LOG

#include "bindinglog.hpp"
#include "assemblyname.hpp"
#include "assembly.hpp"
#include "applicationcontext.hpp"
#include "bindresult.hpp"
#include "cdebuglog.hpp"
#include "variables.hpp"
#include "bindresult.inl"

#include "strsafe.h"

#define SIZE_OF_TOKEN_INFORMATION                   \
    (sizeof( TOKEN_USER )                           \
    + sizeof( SID )                                 \
    + sizeof( ULONG ) * SID_MAX_SUB_AUTHORITIES)

#include "../dlls/mscorrc/fusres.h"

STDAPI BinderGetDisplayName(PEAssembly *pAssembly,
                            SString    &displayName);

namespace BINDER_SPACE
{
    namespace
    {
        inline UINT GetPreBindStateName(AssemblyName *pAssemblyName)
        {
            if (pAssemblyName->HaveAssemblyVersion())
            {
                return ID_FUSLOG_BINDING_PRE_BIND_STATE_BY_NAME;
            }
            else
            {
                return ID_FUSLOG_BINDING_PRE_BIND_STATE_BY_NAME_PARTIAL;
            }
        }
    };

    BindingLog::BindingLog()
    {
        m_pCDebugLog = NULL;
    }

    BindingLog::~BindingLog()
    {
        SAFE_RELEASE(m_pCDebugLog);
    }

    /* static */
    HRESULT BindingLog::CreateInContext(ApplicationContext *pApplicationContext,
                                        SString            &assemblyPath,
                                        PEAssembly         *pParentAssembly)
    {
        if (!IsLoggingNeeded())
            return S_OK;
        
        return CreateInContext(pApplicationContext, NULL, assemblyPath, pParentAssembly);
    }                               

    /* static */
    BOOL BindingLog::IsLoggingNeeded()
    {
#ifdef FEATURE_VERSIONING_LOG
        return g_BinderVariables->fLoggingNeeded;
#else // FEATURE_VERSIONING_LOG
        return FALSE;
#endif // FEATURE_VERSIONING_LOG
    }

    /* static */
    HRESULT BindingLog::CreateInContext(ApplicationContext *pApplicationContext,
                                        AssemblyName       *pAssemblyName,
                                        PEAssembly         *pParentAssembly)
    {
        HRESULT hr = S_OK;

        if (IsLoggingNeeded())
        {
            SmallStackSString emptyString;

            IF_FALSE_GO(pAssemblyName != NULL);
            IF_FAIL_GO(CreateInContext(pApplicationContext,
                                       pAssemblyName,
                                       emptyString,
                                       pParentAssembly));
        }

    Exit:
        return hr;
    }                               

    HRESULT BindingLog::Log(SString &info)
    {
        return GetDebugLog()->LogMessage(0, FUSION_BIND_LOG_CATEGORY_DEFAULT, info);
    }

    HRESULT BindingLog::LogAssemblyName(LPCWSTR       pwzPrefix,
                                        AssemblyName *pAssemblyName)
    {
        HRESULT hr = S_OK;
        PathString assemblyDisplayName;

        // Verify input arguments
        IF_FALSE_GO(pwzPrefix != NULL);
        IF_FALSE_GO(pAssemblyName != NULL);

        pAssemblyName->GetDisplayName(assemblyDisplayName,
                                      AssemblyName::INCLUDE_VERSION |
                                      AssemblyName::INCLUDE_ARCHITECTURE);
        IF_FAIL_GO(Log(pwzPrefix, assemblyDisplayName));

    Exit:
        return hr;
    }

    HRESULT BindingLog::LogHR(HRESULT logHR)
    {
        return GetDebugLog()->SetResultCode(0, logHR);
    }

    HRESULT BindingLog::LogResult(BindResult *pBindResult)
    {
        HRESULT hr = S_OK;
        PathString assemblyDisplayName;
        PathString format;
        PathString info;

        pBindResult->GetAssemblyName()->GetDisplayName(assemblyDisplayName,
                                                       AssemblyName::INCLUDE_VERSION |
                                                       AssemblyName::INCLUDE_ARCHITECTURE);

        IF_FAIL_GO(format.LoadResourceAndReturnHR(CCompRC::Debugging,
                                                  ID_FUSLOG_ASSEMBLY_STATUS_BOUND_TO_ID));
        info.Printf(format.GetUnicode(), assemblyDisplayName.GetUnicode());
        IF_FAIL_GO(Log(info));

        IUnknown *pIUnknownAssembly;
        pIUnknownAssembly = pBindResult->GetAssembly(FALSE /* fAddRef */);
        Assembly *pAssembly;
        pAssembly = static_cast<Assembly *>(static_cast<void *>(pIUnknownAssembly));
        _ASSERTE(pAssembly != NULL);

        if (pAssembly->GetIsInGAC())
        {
            IF_FAIL_GO(info.
                       LoadResourceAndReturnHR(CCompRC::Debugging,
                                               ID_FUSLOG_ASSEMBLY_STATUS_BOUND_GAC));
        }
        else if (pAssembly->GetIsByteArray())
        {
            IF_FAIL_GO(info.
                       LoadResourceAndReturnHR(CCompRC::Debugging,
                                               ID_FUSLOG_ASSEMBLY_STATUS_BOUND_BYTE_ARRAY));
        }
        else
        {
            PathString assemblyPath;

            BinderGetImagePath(pAssembly->GetPEImage(), assemblyPath);
            IF_FAIL_GO(format.
                       LoadResourceAndReturnHR(CCompRC::Debugging,
                                               ID_FUSLOG_ASSEMBLY_STATUS_BOUND_TO_LOCATION));
            info.Printf(format.GetUnicode(), assemblyPath.GetUnicode());
        }
        IF_FAIL_GO(Log(info));

    Exit:
        return hr;
    }

    HRESULT BindingLog::Flush()
    {
        HRESULT hr = S_OK;

        hr = GetDebugLog()->Flush(0, FUSION_BIND_LOG_CATEGORY_DEFAULT);
        if (hr == E_ACCESSDENIED)
        {
            // We've been impersonated differently and have a old log entry
            hr = S_OK;
        }

        return hr;
    }

    /* static */
    HRESULT BindingLog::CreateInContext(ApplicationContext *pApplicationContext,
                                        AssemblyName       *pAssemblyName,
                                        SString            &assemblyPath,
                                        PEAssembly         *pParentAssembly)
    {
        HRESULT hr = S_OK;

        BindingLog *pBindingLog = pApplicationContext->GetBindingLog();

        // Invalidate existing debug log
        pBindingLog->SetDebugLog(NULL);

        IF_FAIL_GO(CDebugLog::Create(pApplicationContext,
                                     pAssemblyName,
                                     assemblyPath,
                                     &pBindingLog->m_pCDebugLog));

        IF_FAIL_GO(pBindingLog->LogPreBindState(pApplicationContext,
                                                pAssemblyName,
                                                assemblyPath,
                                                pParentAssembly));
    Exit:
        return hr;
    }

    HRESULT BindingLog::LogPreBindState(ApplicationContext *pApplicationContext,
                                        AssemblyName       *pAssemblyName,
                                        SString            &assemblyPath,
                                        PEAssembly         *pParentAssembly)
    {
        HRESULT hr = S_OK;
        PathString format;
        PathString info;

        IF_FAIL_GO(info.LoadResourceAndReturnHR(CCompRC::Debugging,
                                                ID_FUSLOG_BINDING_PRE_BIND_STATE_BEGIN));
        IF_FAIL_GO(Log(info));

        if (pAssemblyName != NULL)
        {
            PathString assemblyDisplayName;

            pAssemblyName->GetDisplayName(assemblyDisplayName,
                                          AssemblyName::INCLUDE_VERSION |
                                          AssemblyName::INCLUDE_ARCHITECTURE |
                                          AssemblyName::INCLUDE_RETARGETABLE);

            IF_FAIL_GO(format.LoadResourceAndReturnHR(CCompRC::Debugging,
                                                      GetPreBindStateName(pAssemblyName)));
            info.Printf(format.GetUnicode(), assemblyDisplayName.GetUnicode());
        }
        else
        {
            IF_FAIL_GO(format.
                       LoadResourceAndReturnHR(CCompRC::Debugging,
                                               ID_FUSLOG_BINDING_PRE_BIND_STATE_WHERE_REF));
            info.Printf(format.GetUnicode(), assemblyPath.GetUnicode());
        }
        IF_FAIL_GO(Log(info));

        if (pParentAssembly != NULL)
        {
            PathString parentAssemblyDisplayName;

            IF_FAIL_GO(BinderGetDisplayName(pParentAssembly, parentAssemblyDisplayName));
            IF_FAIL_GO(format.LoadResourceAndReturnHR(CCompRC::Debugging,
                                                      ID_FUSLOG_BINDING_PRE_BIND_STATE_CALLER));
            info.Printf(format.GetUnicode(), parentAssemblyDisplayName.GetUnicode());
        }
        else
        {
            IF_FAIL_GO(info.
                       LoadResourceAndReturnHR(CCompRC::Debugging,
                                               ID_FUSLOG_BINDING_PRE_BIND_STATE_CALLER_UNKNOWN));
        }
        IF_FAIL_GO(Log(info));

        IF_FAIL_GO(info.LoadResourceAndReturnHR(CCompRC::Debugging,
                                                ID_FUSLOG_BINDING_PRE_BIND_STATE_END));
        IF_FAIL_GO(Log(info));

    Exit:
        return hr;
    }

    void BindingLog::SetDebugLog(CDebugLog *pCDebugLog)
    {
        SAFE_RELEASE(m_pCDebugLog);
        m_pCDebugLog = pCDebugLog;
    }
};

#endif // FEATURE_VERSIONING_LOG
