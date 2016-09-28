/*
 * Copyright (C) 2010 The Android Open Source Project
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
 * Modified By LWJ in S.LSI
 */ 

#include <stdlib.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

#include <ui/GraphicBuffer.h>

#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

using namespace android;

#define SURFACE_WIDTH 512
#define SURFACE_HEIGHT 512

#define BUFFER_WIDTH 256
#define BUFFER_HEIGHT 256

EGLDisplay dpy;
EGLContext eglContext;
EGLSurface eglSurface;

pthread_mutex_t mut1;
pthread_cond_t cond1;

static struct SLOT{
	sp<GraphicBuffer> graphicBuffer;
	unsigned int * vpAddr;
	GLuint texId;
	EGLImageKHR eglImg;
	EGLSyncKHR fence;
} slot[2];

typedef enum _USEFENCE_ 
{
	USEFENCE_NO = 0,
	USEFENCE_YES = 1,
}USEFENCE;

//use it both threads
volatile USEFENCE gFlagUseFence;
volatile int currentBuffer;

void setupGraphics(int w, int h)
{
	glEnable(GL_TEXTURE_EXTERNAL_OES);
	glViewport(0, 0, w, h);
	glLoadIdentity();
}

bool setupTexSurface(EGLDisplay dpy, int index)
{
	const int texUsage = GraphicBuffer::USAGE_HW_RENDER;

	slot[index].graphicBuffer = new GraphicBuffer(BUFFER_WIDTH, BUFFER_HEIGHT, HAL_PIXEL_FORMAT_RGBA_8888, texUsage);

	EGLClientBuffer clientBuffer = (EGLClientBuffer)slot[index].graphicBuffer->getNativeBuffer();
	slot[index].eglImg = eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
									EGL_NATIVE_BUFFER_ANDROID,
									clientBuffer,
									0);
	if (slot[index].eglImg == EGL_NO_IMAGE_KHR){
		return false;
	}

	glGenTextures(1, &slot[index].texId);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, slot[index].texId);

	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	return true;
}

void renderFrame(int index)
{
	static GLfloat fVertices[] =  {
		-0.5f,-0.5f,
		-0.5f,0.5f,
		0.5f, 0.5f,
		0.5f,-0.5f};

	static GLfloat fTexcoord[] = {
		0.0, 0.0,
		0.0, 1.0,
		1.0, 1.0,
		1.0, 0.0};

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, slot[index].texId);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)slot[index].eglImg);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClear( GL_COLOR_BUFFER_BIT);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);	
	glVertexPointer(2, GL_FLOAT, 0, fVertices);
	glTexCoordPointer(2, GL_FLOAT, 0, fTexcoord);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

EGLBoolean init_egl(ANativeWindow *window)
{
	EGLBoolean returnValue;
	EGLConfig myConfig = {0};

	EGLint s_configAttribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE };

	EGLint majorVersion;
	EGLint minorVersion;
	EGLint w, h;

	// Init EGL
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(dpy, &majorVersion, &minorVersion);
	EGLint config_count;
	eglChooseConfig(dpy, s_configAttribs, &myConfig, 1, &config_count);
	eglSurface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
	eglContext = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, NULL);
	returnValue = eglMakeCurrent(dpy, eglSurface, eglSurface, eglContext);

	return returnValue;
}

int fill_buffer(int index)
{
	static int shift; // RGB shift
	status_t err;

	err = slot[index].graphicBuffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN|GRALLOC_USAGE_SW_READ_OFTEN,
			(void**)(&slot[index].vpAddr));
	if (err != 0) {
		fprintf(stderr,"lock failed\n");
		return -1;
	}

	for(int i=0; i< BUFFER_WIDTH*BUFFER_HEIGHT; i++){
		slot[index].vpAddr[i] = 0xff000000|(0xff<<(shift*8)); //ABGR
	}
	if(shift++ == 2) shift = 0;

	err  = slot[index].graphicBuffer->unlock();
	if (err != 0) {
		fprintf(stderr, "graphicBuffer->unlock() failed: %d\n", err);
		return -1;
	}
	return 0;
}

int getFreeBuffer()
{
	return currentBuffer&0x01?0:1;
}

void *cpu_thread(void * arg)
{
	int index;
	while(1){
		index = getFreeBuffer(); //like DQ;

		if(gFlagUseFence == USEFENCE_NO){
			fill_buffer(index);
		}
		else if(gFlagUseFence == USEFENCE_YES){
			volatile EGLSyncKHR fence = slot[index].fence;
			if(fence != NULL){
				EGLint result = eglClientWaitSyncKHR(dpy, fence, 0, 1000000000);
				fprintf(stderr,"eglClient Wait Sync \n");
				eglDestroySyncKHR(dpy, fence);
				if (result == EGL_FALSE) {
					fprintf(stderr, "error waiting for previous fence: %#x", eglGetError());
					return 0;
				} else if (result == EGL_TIMEOUT_EXPIRED_KHR) {
					fprintf(stderr, "timeout waiting for previous fence");
					return 0;
				}
				slot[index].fence = NULL;
				fill_buffer(index);
			}
		}
		pthread_cond_signal(&cond1); //Q Buffer
	}
	return 0;
}

int main(int argc, char** argv)
{
	int i;
	pthread_t thread;
	if(argc == 1)	
	{
		printf("please input test number(1~2) :\n");
		printf("1 : Don't use  fence sync\n");
		printf("2 : Use fence sync\n");
		return 0;
	}

	switch(atoi(argv[1]))
	{
		case 1:
			gFlagUseFence = USEFENCE_NO;
			printf("Don't use Fence Sync\n");
			break;
		case 2:
			gFlagUseFence = USEFENCE_YES;
			printf("Use Fence Sync\n");
			break;
		case 3:
			printf("EXIT\n");
			return 0;
		default:
			break;
	}

	// set up the thread-pool
	sp<ProcessState> proc(ProcessState::self());
	ProcessState::self()->startThreadPool();

	// create a client to surfaceflinger
	sp<SurfaceComposerClient> client = new SurfaceComposerClient();

	sp<SurfaceControl> surfaceControl = client->createSurface(
			String8("Test Surface"), SURFACE_WIDTH , SURFACE_HEIGHT, PIXEL_FORMAT_RGBA_8888, 0);

	SurfaceComposerClient::openGlobalTransaction();
	surfaceControl->setLayer(100000);
	SurfaceComposerClient::closeGlobalTransaction();

	// pretend it went cross-process
	Parcel parcel;
	SurfaceControl::writeSurfaceToParcel(surfaceControl, &parcel);
	parcel.setDataPosition(0);
	sp<Surface> surface = Surface::readFromParcel(parcel);

	ANativeWindow* window = surface.get();

	if(init_egl(window) != EGL_TRUE){
		return 1;
	}

	if(!setupTexSurface(dpy,0) || !setupTexSurface(dpy,1)) {
		fprintf(stderr, "Could not set up texture surface.\n");
		return 1;
	}

	setupGraphics(SURFACE_WIDTH, SURFACE_HEIGHT); 

	pthread_cond_init(&cond1, NULL);
	pthread_mutex_init(&mut1, NULL);
	pthread_create(&thread, NULL, &cpu_thread, NULL);
	usleep(10000);

	for(;;){
		pthread_mutex_lock(&mut1);
		pthread_cond_wait(&cond1, &mut1);
		pthread_mutex_unlock(&mut1);
		
		currentBuffer = currentBuffer&0x01?0:1;

		renderFrame(currentBuffer);

		// CREATE FENCE
		if(gFlagUseFence == USEFENCE_YES){
			slot[currentBuffer].fence = eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL);
			if (slot[currentBuffer].fence == EGL_NO_SYNC_KHR) {
				fprintf(stderr,"Fail to create Sync KHR\n");
				return 0;
			}
		}
		//RENDERING
		eglSwapBuffers(dpy, eglSurface);
	}
	return 0;
}
