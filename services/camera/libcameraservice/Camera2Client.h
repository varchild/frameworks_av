/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_SERVERS_CAMERA_CAMERA2CLIENT_H
#define ANDROID_SERVERS_CAMERA_CAMERA2CLIENT_H

#include "Camera2Device.h"
#include "CameraService.h"
#include "camera2/Parameters.h"
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <gui/CpuConsumer.h>
#include <gui/BufferItemConsumer.h>

namespace android {

/**
 * Implements the android.hardware.camera API on top of
 * camera device HAL version 2.
 */
class Camera2Client :
        public CameraService::Client,
        public Camera2Device::NotificationListener
{
public:
    // ICamera interface (see ICamera for details)

    virtual void            disconnect();
    virtual status_t        connect(const sp<ICameraClient>& client);
    virtual status_t        lock();
    virtual status_t        unlock();
    virtual status_t        setPreviewDisplay(const sp<Surface>& surface);
    virtual status_t        setPreviewTexture(
        const sp<ISurfaceTexture>& surfaceTexture);
    virtual void            setPreviewCallbackFlag(int flag);
    virtual status_t        startPreview();
    virtual void            stopPreview();
    virtual bool            previewEnabled();
    virtual status_t        storeMetaDataInBuffers(bool enabled);
    virtual status_t        startRecording();
    virtual void            stopRecording();
    virtual bool            recordingEnabled();
    virtual void            releaseRecordingFrame(const sp<IMemory>& mem);
    virtual status_t        autoFocus();
    virtual status_t        cancelAutoFocus();
    virtual status_t        takePicture(int msgType);
    virtual status_t        setParameters(const String8& params);
    virtual String8         getParameters() const;
    virtual status_t        sendCommand(int32_t cmd, int32_t arg1, int32_t arg2);

    // Interface used by CameraService

    Camera2Client(const sp<CameraService>& cameraService,
            const sp<ICameraClient>& cameraClient,
            int cameraId,
            int cameraFacing,
            int clientPid);
    virtual ~Camera2Client();

    status_t initialize(camera_module_t *module);

    virtual status_t dump(int fd, const Vector<String16>& args);

    // Interface used by CameraDevice

    virtual void notifyError(int errorCode, int arg1, int arg2);
    virtual void notifyShutter(int frameNumber, nsecs_t timestamp);
    virtual void notifyAutoFocus(uint8_t newState, int triggerId);
    virtual void notifyAutoExposure(uint8_t newState, int triggerId);
    virtual void notifyAutoWhitebalance(uint8_t newState, int triggerId);

private:
    /** ICamera interface-related private members */

    // Mutex that must be locked by methods implementing the ICamera interface.
    // Ensures serialization between incoming ICamera calls. All methods below
    // that append 'L' to the name assume that mICameraLock is locked when
    // they're called
    mutable Mutex mICameraLock;

    // Mutex that must be locked by methods accessing the base Client's
    // mCameraClient ICameraClient interface member, for sending notifications
    // up to the camera user
    mutable Mutex mICameraClientLock;

    typedef camera2::Parameters Parameters;
    typedef camera2::CameraMetadata CameraMetadata;

    status_t setPreviewWindowL(const sp<IBinder>& binder,
            sp<ANativeWindow> window);
    status_t startPreviewL(Parameters &params, bool restart);
    void     stopPreviewL();
    status_t startRecordingL(Parameters &params, bool restart);
    bool     recordingEnabledL();

    // Individual commands for sendCommand()
    status_t commandStartSmoothZoomL();
    status_t commandStopSmoothZoomL();
    status_t commandSetDisplayOrientationL(int degrees);
    status_t commandEnableShutterSoundL(bool enable);
    status_t commandPlayRecordingSoundL();
    status_t commandStartFaceDetectionL(int type);
    status_t commandStopFaceDetectionL(Parameters &params);
    status_t commandEnableFocusMoveMsgL(bool enable);
    status_t commandPingL();
    status_t commandSetVideoBufferCountL(size_t count);

    // Current camera device configuration
    camera2::SharedParameters mParameters;

    /** Camera device-related private members */

    class Camera2Heap;

    void     setPreviewCallbackFlagL(Parameters &params, int flag);
    status_t updateRequests(const Parameters &params);

    // Used with stream IDs
    static const int NO_STREAM = -1;

    /* Output frame metadata processing thread.  This thread waits for new
     * frames from the device, and analyzes them as necessary.
     */
    class FrameProcessor: public Thread {
      public:
        FrameProcessor(wp<Camera2Client> client);
        ~FrameProcessor();

        void dump(int fd, const Vector<String16>& args);
      private:
        static const nsecs_t kWaitDuration = 10000000; // 10 ms
        wp<Camera2Client> mClient;

        virtual bool threadLoop();

        void processNewFrames(sp<Camera2Client> &client);
        status_t processFaceDetect(const CameraMetadata &frame,
                sp<Camera2Client> &client);

        CameraMetadata mLastFrame;
    };

    sp<FrameProcessor> mFrameProcessor;

    /* Preview related members */

    int mPreviewStreamId;
    CameraMetadata mPreviewRequest;
    sp<IBinder> mPreviewSurface;
    sp<ANativeWindow> mPreviewWindow;

    status_t updatePreviewRequest(const Parameters &params);
    status_t updatePreviewStream(const Parameters &params);

    /** Preview callback related members */

    int mCallbackStreamId;
    static const size_t kCallbackHeapCount = 6;
    sp<CpuConsumer>    mCallbackConsumer;
    sp<ANativeWindow>  mCallbackWindow;
    // Simple listener that forwards frame available notifications from
    // a CPU consumer to the callback notification
    class CallbackWaiter: public CpuConsumer::FrameAvailableListener {
      public:
        CallbackWaiter(Camera2Client *parent) : mParent(parent) {}
        void onFrameAvailable() { mParent->onCallbackAvailable(); }
      private:
        Camera2Client *mParent;
    };
    sp<CallbackWaiter>  mCallbackWaiter;
    sp<Camera2Heap>     mCallbackHeap;
    int mCallbackHeapId;
    size_t mCallbackHeapHead, mCallbackHeapFree;
    // Handle callback image buffers
    void onCallbackAvailable();

    status_t updateCallbackStream(const Parameters &params);

    /* Still image capture related members */

    int mCaptureStreamId;
    sp<CpuConsumer>    mCaptureConsumer;
    sp<ANativeWindow>  mCaptureWindow;
    // Simple listener that forwards frame available notifications from
    // a CPU consumer to the capture notification
    class CaptureWaiter: public CpuConsumer::FrameAvailableListener {
      public:
        CaptureWaiter(Camera2Client *parent) : mParent(parent) {}
        void onFrameAvailable() { mParent->onCaptureAvailable(); }
      private:
        Camera2Client *mParent;
    };
    sp<CaptureWaiter>  mCaptureWaiter;
    CameraMetadata mCaptureRequest;
    sp<Camera2Heap>    mCaptureHeap;
    // Handle captured image buffers
    void onCaptureAvailable();

    status_t updateCaptureRequest(const Parameters &params);
    status_t updateCaptureStream(const Parameters &params);

    /* Recording related members */

    int mRecordingStreamId;
    int mRecordingFrameCount;
    sp<BufferItemConsumer>    mRecordingConsumer;
    sp<ANativeWindow>  mRecordingWindow;
    // Simple listener that forwards frame available notifications from
    // a CPU consumer to the recording notification
    class RecordingWaiter: public BufferItemConsumer::FrameAvailableListener {
      public:
        RecordingWaiter(Camera2Client *parent) : mParent(parent) {}
        void onFrameAvailable() { mParent->onRecordingFrameAvailable(); }
      private:
        Camera2Client *mParent;
    };
    sp<RecordingWaiter>  mRecordingWaiter;
    CameraMetadata mRecordingRequest;
    sp<Camera2Heap> mRecordingHeap;

    static const size_t kDefaultRecordingHeapCount = 8;
    size_t mRecordingHeapCount;
    Vector<BufferItemConsumer::BufferItem> mRecordingBuffers;
    size_t mRecordingHeapHead, mRecordingHeapFree;
    // Handle new recording image buffers
    void onRecordingFrameAvailable();

    status_t updateRecordingRequest(const Parameters &params);
    status_t updateRecordingStream(const Parameters &params);

    /** Notification-related members */

    bool mAfInMotion;

    /** Camera2Device instance wrapping HAL2 entry */

    sp<Camera2Device> mDevice;

    /** Utility members */

    // Verify that caller is the owner of the camera
    status_t checkPid(const char *checkLocation) const;

    // Utility class for managing a set of IMemory blocks
    class Camera2Heap : public RefBase {
    public:
        Camera2Heap(size_t buf_size, uint_t num_buffers = 1,
                const char *name = NULL) :
                         mBufSize(buf_size),
                         mNumBufs(num_buffers) {
            mHeap = new MemoryHeapBase(buf_size * num_buffers, 0, name);
            mBuffers = new sp<MemoryBase>[mNumBufs];
            for (uint_t i = 0; i < mNumBufs; i++)
                mBuffers[i] = new MemoryBase(mHeap,
                                             i * mBufSize,
                                             mBufSize);
        }

        virtual ~Camera2Heap()
        {
            delete [] mBuffers;
        }

        size_t mBufSize;
        uint_t mNumBufs;
        sp<MemoryHeapBase> mHeap;
        sp<MemoryBase> *mBuffers;
    };

    // Update parameters all requests use, based on mParameters
    status_t updateRequestCommon(CameraMetadata *request, const Parameters &params) const;

    // Map from sensor active array pixel coordinates to normalized camera
    // parameter coordinates. The former are (0,0)-(array width - 1, array height
    // - 1), the latter from (-1000,-1000)-(1000,1000)
    int normalizedXToArray(int x) const;
    int normalizedYToArray(int y) const;
    int arrayXToNormalized(int width) const;
    int arrayYToNormalized(int height) const;


    static size_t calculateBufferSize(int width, int height,
            int format, int stride);
};

}; // namespace android

#endif