/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/MediaKeys.h"
#include "mozilla/dom/MediaKeysBinding.h"
#include "mozilla/dom/MediaKeyMessageEvent.h"
#include "mozilla/dom/MediaKeyError.h"
#include "mozilla/dom/MediaKeySession.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/CDMProxy.h"
#include "mozilla/EMELog.h"
#include "nsContentUtils.h"
#include "nsIScriptObjectPrincipal.h"
#include "mozilla/Preferences.h"
#include "nsContentTypeParser.h"
#ifdef MOZ_FMP4
#include "MP4Decoder.h"
#endif
#ifdef XP_WIN
#include "mozilla/WindowsVersion.h"
#endif
#include "nsContentCID.h"
#include "nsServiceManagerUtils.h"
#include "mozIGeckoMediaPluginService.h"

namespace mozilla {

namespace dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaKeys,
                                      mElement,
                                      mParent,
                                      mKeySessions,
                                      mPromises,
                                      mPendingSessions);
NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaKeys)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaKeys)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaKeys)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

MediaKeys::MediaKeys(nsPIDOMWindow* aParent, const nsAString& aKeySystem)
  : mParent(aParent)
  , mKeySystem(aKeySystem)
  , mCreatePromiseId(0)
{
}

static PLDHashOperator
RejectPromises(const uint32_t& aKey,
               nsRefPtr<dom::Promise>& aPromise,
               void* aClosure)
{
  aPromise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
  return PL_DHASH_NEXT;
}

MediaKeys::~MediaKeys()
{
  Shutdown();
}

void
MediaKeys::Shutdown()
{
  if (mProxy) {
    mProxy->Shutdown();
    mProxy = nullptr;
  }

  mPromises.Enumerate(&RejectPromises, nullptr);
  mPromises.Clear();
}

nsPIDOMWindow*
MediaKeys::GetParentObject() const
{
  return mParent;
}

JSObject*
MediaKeys::WrapObject(JSContext* aCx)
{
  return MediaKeysBinding::Wrap(aCx, this);
}

void
MediaKeys::GetKeySystem(nsString& retval) const
{
  retval = mKeySystem;
}

already_AddRefed<Promise>
MediaKeys::SetServerCertificate(const ArrayBufferViewOrArrayBuffer& aCert, ErrorResult& aRv)
{
  nsRefPtr<Promise> promise(MakePromise(aRv));
  if (aRv.Failed()) {
    return nullptr;
  }

  nsTArray<uint8_t> data;
  if (!CopyArrayBufferViewOrArrayBufferData(aCert, data)) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return promise.forget();
  }

  mProxy->SetServerCertificate(StorePromise(promise), data);
  return promise.forget();
}

static bool
HaveGMPFor(const nsCString& aKeySystem,
           const nsCString& aAPI,
           const nsCString& aTag = EmptyCString())
{
  nsCOMPtr<mozIGeckoMediaPluginService> mps =
    do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  if (NS_WARN_IF(!mps)) {
    return false;
  }

  nsTArray<nsCString> tags;
  tags.AppendElement(aKeySystem);
  if (!aTag.IsEmpty()) {
    tags.AppendElement(aTag);
  }
  // Note: EME plugins need a non-null nodeId here, as they must
  // not be shared across origins.
  bool hasPlugin = false;
  if (NS_FAILED(mps->HasPluginForAPI(aAPI,
                                     &tags,
                                     &hasPlugin))) {
    return false;
  }
  return hasPlugin;
}

static bool
IsPlayableMP4Type(const nsAString& aContentType)
{
#ifdef MOZ_FMP4
  nsContentTypeParser parser(aContentType);
  nsAutoString mimeType;
  nsresult rv = parser.GetType(mimeType);
  if (NS_FAILED(rv)) {
    return false;
  }
  nsAutoString codecs;
  parser.GetParameter("codecs", codecs);

  NS_ConvertUTF16toUTF8 mimeTypeUTF8(mimeType);
  return MP4Decoder::CanHandleMediaType(mimeTypeUTF8, codecs);
#else
  return false;
#endif
}

bool
MediaKeys::IsTypeSupported(const nsAString& aKeySystem,
                           const Optional<nsAString>& aInitDataType,
                           const Optional<nsAString>& aContentType)
{
  if (aKeySystem.EqualsLiteral("org.w3.clearkey") &&
      (!aInitDataType.WasPassed() || aInitDataType.Value().EqualsLiteral("cenc")) &&
      (!aContentType.WasPassed() || IsPlayableMP4Type(aContentType.Value())) &&
      HaveGMPFor(NS_LITERAL_CSTRING("org.w3.clearkey"),
                 NS_LITERAL_CSTRING("eme-decrypt"))) {
    return true;
  }

#ifdef XP_WIN
  // Note: Adobe Access's GMP uses WMF to decode, so anything our MP4Reader
  // thinks it can play on Windows, the Access GMP should be able to play.
  if (aKeySystem.EqualsLiteral("com.adobe.access") &&
      Preferences::GetBool("media.eme.adobe-access.enabled", false) &&
      IsVistaOrLater() && // Win Vista and later only.
      (!aInitDataType.WasPassed() || aInitDataType.Value().EqualsLiteral("cenc")) &&
      (!aContentType.WasPassed() || IsPlayableMP4Type(aContentType.Value())) &&
      HaveGMPFor(NS_LITERAL_CSTRING("com.adobe.access"),
                 NS_LITERAL_CSTRING("eme-decrypt")) &&
      HaveGMPFor(NS_LITERAL_CSTRING("com.adobe.access"),
                 NS_LITERAL_CSTRING("decode-video"),
                 NS_LITERAL_CSTRING("h264")) &&
      HaveGMPFor(NS_LITERAL_CSTRING("com.adobe.access"),
                 NS_LITERAL_CSTRING("decode-audio"),
                 NS_LITERAL_CSTRING("aac"))) {
      return true;
  }
#endif

  return false;
}

/* static */
IsTypeSupportedResult
MediaKeys::IsTypeSupported(const GlobalObject& aGlobal,
                           const nsAString& aKeySystem,
                           const Optional<nsAString>& aInitDataType,
                           const Optional<nsAString>& aContentType,
                           const Optional<nsAString>& aCapability)
{
  // TODO: Should really get spec changed to this is async, so we can wait
  //       for user to consent to running plugin.
  bool supported = IsTypeSupported(aKeySystem, aInitDataType, aContentType);

  EME_LOG("MediaKeys::IsTypeSupported keySystem='%s' initDataType='%s' contentType='%s' supported=%d",
          NS_ConvertUTF16toUTF8(aKeySystem).get(),
          (aInitDataType.WasPassed() ? NS_ConvertUTF16toUTF8(aInitDataType.Value()).get() : ""),
          (aContentType.WasPassed() ? NS_ConvertUTF16toUTF8(aContentType.Value()).get() : ""),
          supported);

  return supported ? IsTypeSupportedResult::Probably
                   : IsTypeSupportedResult::_empty;
}

already_AddRefed<Promise>
MediaKeys::MakePromise(ErrorResult& aRv)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetParentObject());
  if (!global) {
    NS_WARNING("Passed non-global to MediaKeys ctor!");
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  return Promise::Create(global, aRv);
}

PromiseId
MediaKeys::StorePromise(Promise* aPromise)
{
  static uint32_t sEMEPromiseCount = 1;
  MOZ_ASSERT(aPromise);
  uint32_t id = sEMEPromiseCount++;
  mPromises.Put(id, aPromise);
  return id;
}

already_AddRefed<Promise>
MediaKeys::RetrievePromise(PromiseId aId)
{
  MOZ_ASSERT(mPromises.Contains(aId));
  nsRefPtr<Promise> promise;
  mPromises.Remove(aId, getter_AddRefs(promise));
  return promise.forget();
}

void
MediaKeys::RejectPromise(PromiseId aId, nsresult aExceptionCode)
{
  nsRefPtr<Promise> promise(RetrievePromise(aId));
  if (!promise) {
    NS_WARNING("MediaKeys tried to reject a non-existent promise");
    return;
  }
  if (mPendingSessions.Contains(aId)) {
    // This promise could be a createSession or loadSession promise,
    // so we might have a pending session waiting to be resolved into
    // the promise on success. We've been directed to reject to promise,
    // so we can throw away the corresponding session object.
    mPendingSessions.Remove(aId);
  }

  MOZ_ASSERT(NS_FAILED(aExceptionCode));
  promise->MaybeReject(aExceptionCode);

  if (mCreatePromiseId == aId) {
    // Note: This will probably destroy the MediaKeys object!
    Release();
  }
}

void
MediaKeys::ResolvePromise(PromiseId aId)
{
  nsRefPtr<Promise> promise(RetrievePromise(aId));
  if (!promise) {
    NS_WARNING("MediaKeys tried to resolve a non-existent promise");
    return;
  }
  if (mPendingSessions.Contains(aId)) {
    // We should only resolve LoadSession calls via this path,
    // not CreateSession() promises.
    nsRefPtr<MediaKeySession> session;
    if (!mPendingSessions.Get(aId, getter_AddRefs(session)) ||
        !session ||
        session->GetSessionId().IsEmpty()) {
      NS_WARNING("Received activation for non-existent session!");
      promise->MaybeReject(NS_ERROR_DOM_INVALID_ACCESS_ERR);
      mPendingSessions.Remove(aId);
      return;
    }
    mPendingSessions.Remove(aId);
    mKeySessions.Put(session->GetSessionId(), session);
    promise->MaybeResolve(session);
  } else {
    promise->MaybeResolve(JS::UndefinedHandleValue);
  }
}

/* static */
already_AddRefed<Promise>
MediaKeys::Create(const GlobalObject& aGlobal,
                  const nsAString& aKeySystem,
                  ErrorResult& aRv)
{
  // CDMProxy keeps MediaKeys alive until it resolves the promise and thus
  // returns the MediaKeys object to JS.
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aGlobal.GetAsSupports());
  if (!window || !window->GetExtantDoc()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsRefPtr<MediaKeys> keys = new MediaKeys(window, aKeySystem);
  return keys->Init(aRv);
}

already_AddRefed<Promise>
MediaKeys::Init(ErrorResult& aRv)
{
  nsRefPtr<Promise> promise(MakePromise(aRv));
  if (aRv.Failed()) {
    return nullptr;
  }

  if (!IsTypeSupported(mKeySystem)) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  mProxy = new CDMProxy(this, mKeySystem);

  // Determine principal (at creation time) of the MediaKeys object.
  nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(GetParentObject());
  if (!sop) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }
  mPrincipal = sop->GetPrincipal();

  // Determine principal of the "top-level" window; the principal of the
  // page that will display in the URL bar.
  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(GetParentObject());
  if (!window) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }
  nsCOMPtr<nsIDOMWindow> topWindow;
  window->GetTop(getter_AddRefs(topWindow));
  nsCOMPtr<nsPIDOMWindow> top = do_QueryInterface(topWindow);
  if (!top || !top->GetExtantDoc()) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  mTopLevelPrincipal = top->GetExtantDoc()->NodePrincipal();

  if (!mPrincipal || !mTopLevelPrincipal) {
    NS_WARNING("Failed to get principals when creating MediaKeys");
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  nsAutoString origin;
  nsresult rv = nsContentUtils::GetUTFOrigin(mPrincipal, origin);
  if (NS_FAILED(rv)) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }
  nsAutoString topLevelOrigin;
  rv = nsContentUtils::GetUTFOrigin(mTopLevelPrincipal, topLevelOrigin);
  if (NS_FAILED(rv)) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  if (!window) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }
  nsIDocument* doc = window->GetExtantDoc();
  const bool inPrivateBrowsing = nsContentUtils::IsInPrivateBrowsing(doc);

  EME_LOG("MediaKeys::Create() (%s, %s), %s",
          NS_ConvertUTF16toUTF8(origin).get(),
          NS_ConvertUTF16toUTF8(topLevelOrigin).get(),
          (inPrivateBrowsing ? "PrivateBrowsing" : "NonPrivateBrowsing"));

  // The CDMProxy's initialization is asynchronous. The MediaKeys is
  // refcounted, and its instance is returned to JS by promise once
  // it's been initialized. No external refs exist to the MediaKeys while
  // we're waiting for the promise to be resolved, so we must hold a
  // reference to the new MediaKeys object until it's been created,
  // or its creation has failed. Store the id of the promise returned
  // here, and hold a self-reference until that promise is resolved or
  // rejected.
  MOZ_ASSERT(!mCreatePromiseId, "Should only be created once!");
  mCreatePromiseId = StorePromise(promise);
  AddRef();
  mProxy->Init(mCreatePromiseId,
               origin,
               topLevelOrigin,
               inPrivateBrowsing);

  return promise.forget();
}

void
MediaKeys::OnCDMCreated(PromiseId aId, const nsACString& aNodeId)
{
  nsRefPtr<Promise> promise(RetrievePromise(aId));
  if (!promise) {
    NS_WARNING("MediaKeys tried to resolve a non-existent promise");
    return;
  }
  mNodeId = aNodeId;
  nsRefPtr<MediaKeys> keys(this);
  promise->MaybeResolve(keys);
  if (mCreatePromiseId == aId) {
    Release();
  }
}

already_AddRefed<MediaKeySession>
MediaKeys::CreateSession(SessionType aSessionType,
                         ErrorResult& aRv)
{
  nsRefPtr<MediaKeySession> session = new MediaKeySession(GetParentObject(),
                                                          this,
                                                          mKeySystem,
                                                          aSessionType,
                                                          aRv);

  return session.forget();
}

void
MediaKeys::OnSessionPending(PromiseId aId, MediaKeySession* aSession)
{
  MOZ_ASSERT(mPromises.Contains(aId));
  MOZ_ASSERT(!mPendingSessions.Contains(aId));
  mPendingSessions.Put(aId, aSession);
}

void
MediaKeys::OnSessionCreated(PromiseId aId, const nsAString& aSessionId)
{
  nsRefPtr<Promise> promise(RetrievePromise(aId));
  if (!promise) {
    NS_WARNING("MediaKeys tried to resolve a non-existent promise");
    return;
  }
  MOZ_ASSERT(mPendingSessions.Contains(aId));

  nsRefPtr<MediaKeySession> session;
  bool gotSession = mPendingSessions.Get(aId, getter_AddRefs(session));
  // Session has completed creation/loading, remove it from mPendingSessions,
  // and resolve the promise with it. We store it in mKeySessions, so we can
  // find it again if we need to send messages to it etc.
  mPendingSessions.Remove(aId);
  if (!gotSession || !session) {
    NS_WARNING("Received activation for non-existent session!");
    promise->MaybeReject(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return;
  }

  session->Init(aSessionId);
  mKeySessions.Put(aSessionId, session);
  promise->MaybeResolve(session);
}

void
MediaKeys::OnSessionLoaded(PromiseId aId, bool aSuccess)
{
  nsRefPtr<Promise> promise(RetrievePromise(aId));
  if (!promise) {
    NS_WARNING("MediaKeys tried to resolve a non-existent promise");
    return;
  }
  MOZ_ASSERT(mPendingSessions.Contains(aId));

  nsRefPtr<MediaKeySession> session;
  bool gotSession = mPendingSessions.Get(aId, getter_AddRefs(session));
  // Session has completed creation/loading, remove it from mPendingSessions,
  // and resolve the promise with it. We store it in mKeySessions, so we can
  // find it again if we need to send messages to it etc.
  mPendingSessions.Remove(aId);
  if (!gotSession || !session) {
    NS_WARNING("Received activation for non-existent session!");
    promise->MaybeReject(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return;
  }

  MOZ_ASSERT(!session->GetSessionId().IsEmpty() &&
             !mKeySessions.Contains(session->GetSessionId()));

  mKeySessions.Put(session->GetSessionId(), session);
  promise->MaybeResolve(aSuccess);
}

void
MediaKeys::OnSessionClosed(MediaKeySession* aSession)
{
  nsAutoString id;
  aSession->GetSessionId(id);
  mKeySessions.Remove(id);
}

already_AddRefed<MediaKeySession>
MediaKeys::GetSession(const nsAString& aSessionId)
{
  nsRefPtr<MediaKeySession> session;
  mKeySessions.Get(aSessionId, getter_AddRefs(session));
  return session.forget();
}

const nsCString&
MediaKeys::GetNodeId() const
{
  MOZ_ASSERT(NS_IsMainThread());
  return mNodeId;
}

bool
MediaKeys::IsBoundToMediaElement() const
{
  MOZ_ASSERT(NS_IsMainThread());
  return mElement != nullptr;
}

nsresult
MediaKeys::Bind(HTMLMediaElement* aElement)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (IsBoundToMediaElement()) {
    return NS_ERROR_FAILURE;
  }

  mElement = aElement;
  nsresult rv = CheckPrincipals();
  if (NS_FAILED(rv)) {
    mElement = nullptr;
    return rv;
  }

  return NS_OK;
}

nsresult
MediaKeys::CheckPrincipals()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!IsBoundToMediaElement()) {
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<nsIPrincipal> elementPrincipal(mElement->GetCurrentPrincipal());
  nsRefPtr<nsIPrincipal> elementTopLevelPrincipal(mElement->GetTopLevelPrincipal());
  if (!elementPrincipal ||
      !mPrincipal ||
      !elementPrincipal->Equals(mPrincipal) ||
      !elementTopLevelPrincipal ||
      !mTopLevelPrincipal ||
      !elementTopLevelPrincipal->Equals(mTopLevelPrincipal)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool
CopyArrayBufferViewOrArrayBufferData(const ArrayBufferViewOrArrayBuffer& aBufferOrView,
                                     nsTArray<uint8_t>& aOutData)
{
  if (aBufferOrView.IsArrayBuffer()) {
    const ArrayBuffer& buffer = aBufferOrView.GetAsArrayBuffer();
    buffer.ComputeLengthAndData();
    aOutData.AppendElements(buffer.Data(), buffer.Length());
  } else if (aBufferOrView.IsArrayBufferView()) {
    const ArrayBufferView& bufferview = aBufferOrView.GetAsArrayBufferView();
    bufferview.ComputeLengthAndData();
    aOutData.AppendElements(bufferview.Data(), bufferview.Length());
  } else {
    return false;
  }
  return true;
}

nsIDocument*
MediaKeys::GetOwnerDoc() const
{
  return mElement ? mElement->OwnerDoc() : nullptr;
}

} // namespace dom
} // namespace mozilla
