/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAndroidNetworkLinkService.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/Services.h"

#include "AndroidBridge.h"

using namespace mozilla::widget::android;

NS_IMPL_ISUPPORTS(nsAndroidNetworkLinkService,
                  nsINetworkLinkService)

nsAndroidNetworkLinkService::nsAndroidNetworkLinkService()
{
}

nsAndroidNetworkLinkService::~nsAndroidNetworkLinkService()
{
}

NS_IMETHODIMP
nsAndroidNetworkLinkService::GetIsLinkUp(bool *aIsUp)
{
  if (!mozilla::AndroidBridge::Bridge()) {
    // Fail soft here and assume a connection exists
    NS_WARNING("GetIsLinkUp is not supported without a bridge connection");
    *aIsUp = true;
    return NS_OK;
  }

  *aIsUp = mozilla::widget::android::GeckoAppShell::IsNetworkLinkUp();
  return NS_OK;
}

NS_IMETHODIMP
nsAndroidNetworkLinkService::GetLinkStatusKnown(bool *aIsKnown)
{
  NS_ENSURE_TRUE(mozilla::AndroidBridge::Bridge(), NS_ERROR_NOT_IMPLEMENTED);

  *aIsKnown = mozilla::widget::android::GeckoAppShell::IsNetworkLinkKnown();
  return NS_OK;
}

NS_IMETHODIMP
nsAndroidNetworkLinkService::GetLinkType(uint32_t *aLinkType)
{
  NS_ENSURE_ARG_POINTER(aLinkType);

  if (!mozilla::AndroidBridge::Bridge()) {
    // Fail soft here and assume a connection exists
    NS_WARNING("GetLinkType is not supported without a bridge connection");
    *aLinkType = nsINetworkLinkService::LINK_TYPE_UNKNOWN;
    return NS_OK;
  }

  *aLinkType = mozilla::widget::android::GeckoAppShell::NetworkLinkType();
  return NS_OK;
}
