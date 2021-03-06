/*
 * Copyright (C) 2014-2015 Jolla Ltd.
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
 *
 * Authored by: Mohammed Hassan <mohammed.hassan@jolla.com>
 */

#include "droidmediacamera.h"
#include "allocator.h"
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
#include <android/log.h>
#include <utils/String8.h>
#include <utils/Condition.h>
#include <media/stagefright/CameraSource.h>
#include "droidmediabuffer.h"
#include "private.h"

// This is really a hack
namespace android {
  class CameraSourceListener {
  public:
    static int32_t getColorFormat(android::CameraSource *src, android::CameraParameters p) {
      return src->isCameraColorFormatSupported (p) == android::NO_ERROR ? src->mColorFormat : -1;
    }
  };
};

extern "C" {

class DroidMediaCameraRecordingData
{
public:
    android::sp<android::IMemory> mem;
    nsecs_t ts;
};

class DroidMediaCamera
{
public:
    DroidMediaCamera() :
        m_cb_data(0) {
        memset(&m_cb, 0x0, sizeof(m_cb));
    }

    android::sp<android::Camera> m_camera;
    android::sp<DroidMediaBufferQueue> m_queue;
    DroidMediaCameraCallbacks m_cb;
    void *m_cb_data;
};

class CameraListener : public android::CameraListener {
public:
    CameraListener(DroidMediaCamera *cam) :
        m_cam(cam) {

    }

    void notify(int32_t msgType, int32_t ext1, int32_t ext2)
    {
        switch (msgType) {
            case CAMERA_MSG_SHUTTER:
                if (m_cam->m_cb.shutter_cb) {
                    m_cam->m_cb.shutter_cb(m_cam->m_cb_data);
                }
                break;

            case CAMERA_MSG_FOCUS:
                if (m_cam->m_cb.focus_cb) {
                    m_cam->m_cb.focus_cb(m_cam->m_cb_data, ext1);
                }
                break;

            case CAMERA_MSG_FOCUS_MOVE:
                if (m_cam->m_cb.focus_move_cb) {
                    m_cam->m_cb.focus_move_cb(m_cam->m_cb_data, ext1);
                }
                break;

            case CAMERA_MSG_ERROR:
                if (m_cam->m_cb.error_cb) {
                    m_cam->m_cb.error_cb(m_cam->m_cb_data, ext1);
                }
                break;

            case CAMERA_MSG_ZOOM:
                if (m_cam->m_cb.zoom_cb) {
                    m_cam->m_cb.zoom_cb(m_cam->m_cb_data, ext1, ext2);
                }
                break;
            default:
                ALOGW("DroidMediaCamera: unknown notify message 0x%x", msgType);
                break;
        }
    }

    void postData(int32_t msgType, const android::sp<android::IMemory>& dataPtr,
                  camera_frame_metadata_t *metadata)
    {
        DroidMediaData mem;
        mem.size = dataPtr->size();
        mem.data = dataPtr->pointer();

        switch (msgType) {
            case CAMERA_MSG_RAW_IMAGE:
                if (m_cam->m_cb.raw_image_cb) {
                    m_cam->m_cb.raw_image_cb(m_cam->m_cb_data, &mem);
                }
                break;

            case CAMERA_MSG_COMPRESSED_IMAGE:
                if (m_cam->m_cb.compressed_image_cb) {
                    m_cam->m_cb.compressed_image_cb(m_cam->m_cb_data, &mem);
                }
                break;

            case CAMERA_MSG_POSTVIEW_FRAME:
                if (m_cam->m_cb.postview_frame_cb) {
                    m_cam->m_cb.postview_frame_cb(m_cam->m_cb_data, &mem);
                }
                break;

            case CAMERA_MSG_PREVIEW_METADATA:
                if (m_cam->m_cb.preview_metadata_cb) {
                    sendPreviewMetadata(metadata);
                }
                break;

            case CAMERA_MSG_RAW_IMAGE_NOTIFY:
                if (m_cam->m_cb.raw_image_notify_cb) {
                    m_cam->m_cb.raw_image_notify_cb(m_cam->m_cb_data);
                }
                break;

            case CAMERA_MSG_PREVIEW_FRAME:
                if (m_cam->m_cb.preview_frame_cb) {
                    m_cam->m_cb.preview_frame_cb(m_cam->m_cb_data, &mem);
                }
                break;

            default:
                ALOGW("DroidMediaCamera: unknown postData message 0x%x", msgType);
                break;
        }
    }

    void postDataTimestamp(nsecs_t timestamp, int32_t msgType, const android::sp<android::IMemory>& dataPtr)
    {
        switch (msgType) {
            case CAMERA_MSG_VIDEO_FRAME:
                if (m_cam->m_cb.video_frame_cb) {
                    // TODO: revisit this data structure
                    DroidMediaCameraRecordingData *data = new DroidMediaCameraRecordingData;
                    data->mem = dataPtr;
                    data->ts = timestamp;
                    m_cam->m_cb.video_frame_cb(m_cam->m_cb_data, data);
                } else {
                    m_cam->m_camera->releaseRecordingFrame(dataPtr);
                }

                break;

            default:
                ALOGW("DroidMediaCamera: unknown postDataTimestamp message 0x%x", msgType);
                break;
        }
    }

    void sendPreviewMetadata(camera_frame_metadata_t *metadata)
    {
        android::Vector<DroidMediaCameraFace> faces;

        for (int x = 0; x < metadata->number_of_faces; x++) {
            DroidMediaCameraFace face;
            face.left = metadata->faces[x].rect[0];
            face.top = metadata->faces[x].rect[1];
            face.right = metadata->faces[x].rect[2];
            face.bottom = metadata->faces[x].rect[3];
            face.score = metadata->faces[x].score;
            face.id = metadata->faces[x].id;
            face.left_eye[0] = metadata->faces[x].left_eye[0];
            face.left_eye[1] = metadata->faces[x].left_eye[1];
            face.right_eye[0] = metadata->faces[x].right_eye[0];
            face.right_eye[1] = metadata->faces[x].right_eye[1];
            face.mouth[0] = metadata->faces[x].mouth[0];
            face.mouth[1] = metadata->faces[x].mouth[1];

            faces.push_back(face);
        }

        m_cam->m_cb.preview_metadata_cb(m_cam->m_cb_data, faces.array(), faces.size());
    }

private:
    DroidMediaCamera *m_cam;
};

DroidMediaBufferQueue *droid_media_camera_get_buffer_queue (DroidMediaCamera *camera)
{
  return camera->m_queue.get();
}

int droid_media_camera_get_number_of_cameras()
{
    return android::Camera::getNumberOfCameras();
}

bool droid_media_camera_get_info(DroidMediaCameraInfo *info, int camera_number)
{
    android::CameraInfo inf;

    if (android::Camera::getCameraInfo(camera_number, &inf) != 0) {
        return false;
    }

    info->orientation = inf.orientation;
    if (inf.facing == CAMERA_FACING_FRONT) {
      info->facing = DROID_MEDIA_CAMERA_FACING_FRONT;
    } else {
      info->facing = DROID_MEDIA_CAMERA_FACING_BACK;
    }

    return true;
}

DroidMediaCamera *droid_media_camera_connect(int camera_number)
{
  android::sp<DroidMediaBufferQueueListener> listener(new DroidMediaBufferQueueListener);

    android::sp<DroidMediaBufferQueue>
      queue(new DroidMediaBufferQueue("DroidMediaCameraBufferQueue"));
    if (!queue->connectListener()) {
        ALOGE("Failed to connect buffer queue listener");
	queue.clear();
	listener.clear();
        return NULL;
    }

    DroidMediaCamera *cam = new DroidMediaCamera;
    if (!cam) {
        ALOGE("Failed to allocate DroidMediaCamera");
        return NULL;
    }

#if ANDROID_MAJOR == 4 && ANDROID_MINOR == 4
    cam->m_camera = android::Camera::connect(camera_number, android::String16("droidmedia"),
					     android::Camera::USE_CALLING_UID);
#else
    cam->m_camera = android::Camera::connect(camera_number);
#endif
    if (cam->m_camera.get() == NULL) {
        delete cam;
        ALOGE("Failed to connect to camera service");
        return NULL;
    }

    cam->m_queue = queue;

#if ANDROID_MAJOR == 4 && ANDROID_MINOR == 4
    cam->m_camera->setPreviewTarget(cam->m_queue);
#else
    cam->m_camera->setPreviewTexture(cam->m_queue);
#endif

    cam->m_camera->setListener(new CameraListener(cam));

    return cam;
}

bool droid_media_camera_reconnect(DroidMediaCamera *camera) {
    return camera->m_camera->reconnect() == android::NO_ERROR;
}

void droid_media_camera_disconnect(DroidMediaCamera *camera)
{
    camera->m_camera->disconnect();

    camera->m_queue->setCallbacks(0, 0);

    delete camera;
}

bool droid_media_camera_lock(DroidMediaCamera *camera) {
    return camera->m_camera->lock() == android::NO_ERROR;
}

bool droid_media_camera_unlock(DroidMediaCamera *camera) {
    return camera->m_camera->unlock() == android::NO_ERROR;
}

bool droid_media_camera_start_preview(DroidMediaCamera *camera)
{
    return camera->m_camera->startPreview() == android::NO_ERROR;
}

void droid_media_camera_stop_preview(DroidMediaCamera *camera)
{
    camera->m_camera->stopPreview();
}

bool droid_media_camera_is_preview_enabled(DroidMediaCamera *camera)
{
    return camera->m_camera->previewEnabled();
}

bool droid_media_camera_start_recording(DroidMediaCamera *camera)
{
    return camera->m_camera->startRecording() == android::NO_ERROR;
}

void droid_media_camera_stop_recording(DroidMediaCamera *camera)
{
    camera->m_camera->stopRecording();
}

bool droid_media_camera_is_recording_enabled(DroidMediaCamera *camera)
{
    return camera->m_camera->recordingEnabled();
}

bool droid_media_camera_start_auto_focus(DroidMediaCamera *camera)
{
    return camera->m_camera->autoFocus() == android::NO_ERROR;
}

bool droid_media_camera_cancel_auto_focus(DroidMediaCamera *camera)
{
    return camera->m_camera->cancelAutoFocus() == android::NO_ERROR;
}

void droid_media_camera_set_callbacks(DroidMediaCamera *camera, DroidMediaCameraCallbacks *cb, void *data)
{
    memcpy(&camera->m_cb, cb, sizeof(camera->m_cb));
    camera->m_cb_data = data;
}

bool droid_media_camera_send_command(DroidMediaCamera *camera, int32_t cmd, int32_t arg1, int32_t arg2)
{
    return camera->m_camera->sendCommand(cmd, arg1, arg2) == android::NO_ERROR;
}

bool droid_media_camera_store_meta_data_in_buffers(DroidMediaCamera *camera, bool enabled)
{
    return camera->m_camera->storeMetaDataInBuffers(enabled) == android::NO_ERROR;
}

void droid_media_camera_set_preview_callback_flags(DroidMediaCamera *camera, int preview_callback_flag)
{
    camera->m_camera->setPreviewCallbackFlags(preview_callback_flag);
}

bool droid_media_camera_set_parameters(DroidMediaCamera *camera, const char *params)
{
    return camera->m_camera->setParameters(android::String8(params)) == android::NO_ERROR;
}

char *droid_media_camera_get_parameters(DroidMediaCamera *camera)
{
    android::String8 p = camera->m_camera->getParameters();
    if (p.isEmpty()) {
        ALOGE("Failed to get camera parameters");
        return NULL;
    }

    size_t len = p.length();

    char *params = (char *)malloc(len + 1);
    if (!params) {
        ALOGE("Failed to allocate enough memory for camera parameters");
        return NULL;
    }

    memcpy(params, p.string(), len);
    params[len] = '\0';

    return params;
}

bool droid_media_camera_take_picture(DroidMediaCamera *camera, int msgType)
{
    return camera->m_camera->takePicture(msgType) == android::NO_ERROR;
}

void droid_media_camera_release_recording_frame(DroidMediaCamera *camera, DroidMediaCameraRecordingData *data)
{
    camera->m_camera->releaseRecordingFrame(data->mem);
    delete data;
}

nsecs_t droid_media_camera_recording_frame_get_timestamp(DroidMediaCameraRecordingData *data)
{
    return data->ts;
}

size_t droid_media_camera_recording_frame_get_size(DroidMediaCameraRecordingData *data)
{
    return data->mem->size();
}

void *droid_media_camera_recording_frame_get_data(DroidMediaCameraRecordingData *data)
{
    return data->mem->pointer();
}

bool droid_media_camera_enable_face_detection(DroidMediaCamera *camera,
					      DroidMediaCameraFaceDetectionType type, bool enable)
{
  int detection_type = type == DROID_MEDIA_CAMERA_FACE_DETECTION_HW ? CAMERA_FACE_DETECTION_HW :
    CAMERA_FACE_DETECTION_SW;

  int cmd = enable ? CAMERA_CMD_START_FACE_DETECTION : CAMERA_CMD_STOP_FACE_DETECTION;

  return droid_media_camera_send_command (camera, cmd, detection_type, 0);
}

int32_t droid_media_camera_get_video_color_format (DroidMediaCamera *camera)
{
#if ANDROID_MAJOR == 4 && ANDROID_MINOR <= 2
  android::CameraSource *s = android::CameraSource::Create();
#else
  android::CameraSource *s = android::CameraSource::Create(android::String16(""));
#endif

  android::CameraParameters p(camera->m_camera->getParameters());

  int32_t fmt = android::CameraSourceListener::getColorFormat(s, p);

  delete s;

  return fmt;
}

};
