/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation. Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#ifndef nsFileChannel_h__
#define nsFileChannel_h__

#include "nsIFileChannel.h"
#include "nsIFileProtocolHandler.h"
#include "nsIInterfaceRequestor.h"
#include "nsILoadGroup.h"
#include "nsIStreamListener.h"
#include "nsIChannel.h"
#include "nsFileSpec.h"
#include "nsIURI.h"
#include "nsCOMPtr.h"

#include "nsIFileChannel.h"
#include "nsIRunnable.h"
#include "nsIThread.h"
#include "nsFileSpec.h"
#include "prlock.h"
#include "nsIEventQueueService.h"
#include "nsIPipe.h"
#include "nsILoadGroup.h"
#include "nsIStreamListener.h"
#include "nsCOMPtr.h"

class nsFileChannel : public nsIFileChannel,
                      public nsIStreamListener
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIREQUEST
    NS_DECL_NSICHANNEL
    NS_DECL_NSIFILECHANNEL
    NS_DECL_NSISTREAMOBSERVER
    NS_DECL_NSISTREAMLISTENER

    nsFileChannel();
    // Always make the destructor virtual: 
    virtual ~nsFileChannel();

    // Define a Create method to be used with a factory:
    static NS_METHOD
    Create(nsISupports* aOuter, const nsIID& aIID, void* *aResult);
    
    nsresult Init(nsIFileProtocolHandler* handler, 
                  const char* command, 
                  nsIURI* uri,
                  nsILoadGroup* aLoadGroup, 
                  nsIInterfaceRequestor* notificationCallbacks, 
                  nsLoadFlags loadAttributes,
                  nsIURI* originalURI);

protected:
    nsresult CreateFileChannelFromFileSpec(nsFileSpec& spec, nsIFileChannel** result);

protected:
    nsCOMPtr<nsIURI>                    mOriginalURI;
    nsCOMPtr<nsIURI>                    mURI;
    nsCOMPtr<nsIFileProtocolHandler>    mHandler;
    nsCOMPtr<nsIInterfaceRequestor>     mCallbacks;
    char*                               mCommand;
    nsFileSpec                          mSpec;
    nsCOMPtr<nsIChannel>                mFileTransport;
    PRUint32                            mLoadAttributes;
    nsCOMPtr<nsILoadGroup>              mLoadGroup;
    nsCOMPtr<nsISupports>               mOwner;
    nsCOMPtr<nsIStreamListener>         mRealListener;
};

#endif // nsFileChannel_h__
