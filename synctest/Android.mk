LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	sync_test.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils \
    libEGL \
    libGLESv1_CM \
    libui \
    libbinder \
    libutils \
	libgui

LOCAL_C_INCLUDES += $(call include-path-for, opengl-tests-includes)

LOCAL_SYSTEM_LIBRARIES := lpthread

LOCAL_MODULE:= test-fence

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

include $(BUILD_EXECUTABLE)
