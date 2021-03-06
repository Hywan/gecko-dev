/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef imgStatusTracker_h__
#define imgStatusTracker_h__

class imgDecoderObserver;
class imgIContainer;
class imgStatusNotifyRunnable;
class imgRequestNotifyRunnable;
class imgStatusTrackerObserver;
class nsIRunnable;

#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "nsCOMPtr.h"
#include "nsTObserverArray.h"
#include "nsThreadUtils.h"
#include "nsRect.h"
#include "imgRequestProxy.h"

namespace mozilla {
namespace image {

class Image;

// Image state bitflags.
enum {
  FLAG_REQUEST_STARTED    = 1u << 0,
  FLAG_HAS_SIZE           = 1u << 1,  // STATUS_SIZE_AVAILABLE
  FLAG_DECODE_STARTED     = 1u << 2,  // STATUS_DECODE_STARTED
  FLAG_DECODE_STOPPED     = 1u << 3,  // STATUS_DECODE_COMPLETE
  FLAG_FRAME_STOPPED      = 1u << 4,  // STATUS_FRAME_COMPLETE
  FLAG_REQUEST_STOPPED    = 1u << 5,  // STATUS_LOAD_COMPLETE
  FLAG_ONLOAD_BLOCKED     = 1u << 6,
  FLAG_ONLOAD_UNBLOCKED   = 1u << 7,
  FLAG_IS_ANIMATED        = 1u << 8,
  FLAG_IS_MULTIPART       = 1u << 9,
  FLAG_MULTIPART_STOPPED  = 1u << 10,
  FLAG_HAS_ERROR          = 1u << 11  // STATUS_ERROR
};

struct ImageStatusDiff
{
  ImageStatusDiff()
    : diffState(0)
  { }

  static ImageStatusDiff NoChange() { return ImageStatusDiff(); }
  bool IsNoChange() const { return *this == NoChange(); }

  bool operator!=(const ImageStatusDiff& aOther) const { return !(*this == aOther); }
  bool operator==(const ImageStatusDiff& aOther) const {
    return aOther.diffState == diffState;
  }

  void Combine(const ImageStatusDiff& aOther) {
    diffState |= aOther.diffState;
  }

  uint32_t diffState;
};

} // namespace image
} // namespace mozilla

/*
 * The image status tracker is a class that encapsulates all the loading and
 * decoding status about an Image, and makes it possible to send notifications
 * to imgRequestProxys, both synchronously (i.e., the status now) and
 * asynchronously (the status later).
 *
 * When a new proxy needs to be notified of the current state of an image, call
 * the Notify() method on this class with the relevant proxy as its argument,
 * and the notifications will be replayed to the proxy asynchronously.
 */


class imgStatusTracker : public mozilla::SupportsWeakPtr<imgStatusTracker>
{
  virtual ~imgStatusTracker();

public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(imgStatusTracker)
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(imgStatusTracker)

  // aImage is the image that this status tracker will pass to the
  // imgRequestProxys in SyncNotify() and EmulateRequestFinished(), and must be
  // alive as long as this instance is, because we hold a weak reference to it.
  explicit imgStatusTracker(mozilla::image::Image* aImage);

  // Image-setter, for imgStatusTrackers created by imgRequest::Init, which
  // are created before their Image is created.  This method should only
  // be called once, and only on an imgStatusTracker that was initialized
  // without an image.
  void SetImage(mozilla::image::Image* aImage);

  // Image resetter, for when mImage is about to go out of scope. mImage is a
  // weak reference, and thus must be set to null when it's object is deleted.
  void ResetImage();

  // Inform this status tracker that it is associated with a multipart image.
  void SetIsMultipart();

  // Schedule an asynchronous "replaying" of all the notifications that would
  // have to happen to put us in the current state.
  // We will also take note of any notifications that happen between the time
  // Notify() is called and when we call SyncNotify on |proxy|, and replay them
  // as well.
  // Should be called on the main thread only, since imgRequestProxy and GetURI
  // are not threadsafe.
  void Notify(imgRequestProxy* proxy);

  // Schedule an asynchronous "replaying" of all the notifications that would
  // have to happen to put us in the state we are in right now.
  // Unlike Notify(), does *not* take into account future notifications.
  // This is only useful if you do not have an imgRequest, e.g., if you are a
  // static request returned from imgIRequest::GetStaticRequest().
  // Should be called on the main thread only, since imgRequestProxy and GetURI
  // are not threadsafe.
  void NotifyCurrentState(imgRequestProxy* proxy);

  // "Replay" all of the notifications that would have to happen to put us in
  // the state we're currently in.
  // Only use this if you're already servicing an asynchronous call (e.g.
  // OnStartRequest).
  // Should be called on the main thread only, since imgRequestProxy and GetURI
  // are not threadsafe.
  void SyncNotify(imgRequestProxy* proxy);

  // Send some notifications that would be necessary to make |proxy| believe
  // the request is finished downloading and decoding.  We only send
  // OnStopRequest and UnblockOnload, and only if necessary.
  void EmulateRequestFinished(imgRequestProxy* proxy, nsresult aStatus);

  // We manage a set of consumers that are using an image and thus concerned
  // with its status. Weak pointers.
  void AddConsumer(imgRequestProxy* aConsumer);
  bool RemoveConsumer(imgRequestProxy* aConsumer, nsresult aStatus);
  size_t ConsumerCount() const {
    MOZ_ASSERT(NS_IsMainThread(), "Use mConsumers on main thread only");
    return mConsumers.Length();
  }

  // This is intentionally non-general because its sole purpose is to support an
  // some obscure network priority logic in imgRequest. That stuff could probably
  // be improved, but it's too scary to mess with at the moment.
  bool FirstConsumerIs(imgRequestProxy* aConsumer);

  void AdoptConsumers(imgStatusTracker* aTracker) {
    MOZ_ASSERT(NS_IsMainThread(), "Use mConsumers on main thread only");
    MOZ_ASSERT(aTracker);
    mConsumers = aTracker->mConsumers;
  }

  // Returns whether we are in the process of loading; that is, whether we have
  // not received OnStopRequest.
  bool IsLoading() const;

  // Get the current image status (as in imgIRequest).
  uint32_t GetImageStatus() const;

  // Following are all the notification methods. You must call the Record
  // variant on this status tracker, then call the Send variant for each proxy
  // you want to notify.

  // Call when the request is being cancelled.
  void RecordCancel();

  // Shorthand for recording all the load notifications: StartRequest,
  // StartContainer, StopRequest.
  void RecordLoaded();

  // Shorthand for recording all the decode notifications: StartDecode,
  // DataAvailable, StopFrame, StopDecode.
  void RecordDecoded();

  /* non-virtual imgDecoderObserver methods */
  // Functions with prefix Send- are main thread only, since they contain calls
  // to imgRequestProxy functions, which are expected on the main thread.
  void RecordStartDecode();
  void SendStartDecode(imgRequestProxy* aProxy);
  void RecordStartContainer(imgIContainer* aContainer);
  void SendStartContainer(imgRequestProxy* aProxy);
  void RecordStopFrame();
  void SendStopFrame(imgRequestProxy* aProxy);
  void RecordStopDecode(nsresult statusg);
  void SendStopDecode(imgRequestProxy* aProxy, nsresult aStatus);
  void SendDiscard(imgRequestProxy* aProxy);
  void RecordUnlockedDraw();
  void SendUnlockedDraw(imgRequestProxy* aProxy);
  void RecordImageIsAnimated();
  void SendImageIsAnimated(imgRequestProxy *aProxy);

  /* non-virtual sort-of-nsIRequestObserver methods */
  // Functions with prefix Send- are main thread only, since they contain calls
  // to imgRequestProxy functions, which are expected on the main thread.
  void RecordStartRequest();
  void SendStartRequest(imgRequestProxy* aProxy);
  void RecordStopRequest(bool aLastPart, nsresult aStatus);
  void SendStopRequest(imgRequestProxy* aProxy, bool aLastPart, nsresult aStatus);

  // All main thread only because they call functions (like SendStartRequest)
  // which are expected to be called on the main thread.
  void OnStartRequest();
  // OnDataAvailable will dispatch a call to itself onto the main thread if not
  // called there.
  void OnDataAvailable();
  void OnStopRequest(bool aLastPart, nsresult aStatus);
  void OnDiscard();
  void OnUnlockedDraw();
  // This is called only by VectorImage, and only to ensure tests work
  // properly. Do not use it.
  void OnStopFrame();

  /* non-virtual imgIOnloadBlocker methods */
  // NB: If UnblockOnload is sent, and then we are asked to replay the
  // notifications, we will not send a BlockOnload/UnblockOnload pair.  This
  // is different from all the other notifications.
  void RecordBlockOnload();
  void SendBlockOnload(imgRequestProxy* aProxy);
  void RecordUnblockOnload();
  void SendUnblockOnload(imgRequestProxy* aProxy);

  // Main thread only because mConsumers is not threadsafe.
  void MaybeUnblockOnload();

  void RecordError();

  bool IsMultipart() const { return mState & mozilla::image::FLAG_IS_MULTIPART; }

  // Weak pointer getters - no AddRefs.
  inline already_AddRefed<mozilla::image::Image> GetImage() const {
    nsRefPtr<mozilla::image::Image> image = mImage;
    return image.forget();
  }
  inline bool HasImage() { return mImage; }

  inline imgDecoderObserver* GetDecoderObserver() { return mTrackerObserver.get(); }

  already_AddRefed<imgStatusTracker> CloneForRecording();

  // Compute the difference between this status tracker and aOther.
  mozilla::image::ImageStatusDiff Difference(imgStatusTracker* aOther) const;

  // Captures all of the decode notifications (i.e., not OnStartRequest /
  // OnStopRequest) so far as an ImageStatusDiff.
  mozilla::image::ImageStatusDiff DecodeStateAsDifference() const;

  // Update our state to incorporate the changes in aDiff.
  void ApplyDifference(const mozilla::image::ImageStatusDiff& aDiff);

  // Notify for the changes captured in an ImageStatusDiff. Because this may
  // result in recursive notifications, no decoding locks may be held.
  // Called on the main thread only.
  void SyncNotifyDifference(const mozilla::image::ImageStatusDiff& aDiff,
                            const nsIntRect& aInvalidRect = nsIntRect());

private:
  typedef nsTObserverArray<mozilla::WeakPtr<imgRequestProxy>> ProxyArray;
  friend class imgStatusNotifyRunnable;
  friend class imgRequestNotifyRunnable;
  friend class imgStatusTrackerObserver;
  friend class imgStatusTrackerInit;
  imgStatusTracker(const imgStatusTracker& aOther);

  // Main thread only because it deals with the observer service.
  void FireFailureNotification();

  // Main thread only, since imgRequestProxy calls are expected on the main
  // thread, and mConsumers is not threadsafe.
  static void SyncNotifyState(ProxyArray& aProxies,
                              bool aHasImage, uint32_t aState,
                              const nsIntRect& aInvalidRect);

  nsCOMPtr<nsIRunnable> mRequestRunnable;

  // This weak ref should be set null when the image goes out of scope.
  mozilla::image::Image* mImage;

  // List of proxies attached to the image. Each proxy represents a consumer
  // using the image. Array and/or individual elements should only be accessed
  // on the main thread.
  ProxyArray mConsumers;

  mozilla::RefPtr<imgDecoderObserver> mTrackerObserver;

  uint32_t mState;
};

class imgStatusTrackerInit
{
public:
  imgStatusTrackerInit(mozilla::image::Image* aImage,
                       imgStatusTracker* aTracker);
  ~imgStatusTrackerInit();
private:
  imgStatusTracker* mTracker;
};

#endif
