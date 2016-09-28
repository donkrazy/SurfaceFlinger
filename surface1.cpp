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
 */
#include <stdlib.h>
#include <ui/GraphicBuffer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

using namespace android;

#define WIDTH					1024
#define HEIGHT					1024
#define RED_COLOR				0xff0000ff        /* ABGR */
#define GREEN_COLOR				0xff00ff00        /* ABGR */
#define BLUE_COLOR				0xffff0000        /* ABGR */
#define NUMBER_OF_PIXEL			WIDTH * HEIGHT
#define BPP						4                 /* Bite Per Pixel is 4 Bytes */
#define NUMBER_OF_BUFFER		3

int main(int argc, char** argv) {
    // set up the thread-pool
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();

    // Connect SurfaceFlinger (SurfaceComposerClient)
    sp<SurfaceComposerClient> client = new SurfaceComposerClient();

    // create a client to surfaceflinger (SurfaceControl)
    sp<SurfaceControl> surfaceControl = client->createSurface(
            String8("My Surface"), 
            WIDTH, HEIGHT, PIXEL_FORMAT_RGBA_8888, 0);

    // Modify Layer state for Z vaule to 1000000
    SurfaceComposerClient::openGlobalTransaction(); 
    surfaceControl->setLayer(10000000);
    SurfaceComposerClient::closeGlobalTransaction();

    // Get ANativeWindow(ANW) from SurfaceControl
    sp<Surface> surface=surfaceControl->getSurface();
    sp<ANativeWindow> window(surface);

    // Set ANW buffer count to 3+1
    int err = native_window_set_buffer_count(window.get(), NUMBER_OF_BUFFER+1);

    // Set ANW usage to READ and WRITE flags
    err = native_window_set_usage(window.get(),
            GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);

    unsigned int * pBufferAddr[NUMBER_OF_BUFFER];
    ANativeWindowBuffer* ANBuffer;
    sp<GraphicBuffer> buffer[NUMBER_OF_BUFFER];
    while(1){
        for(int i =0; i < NUMBER_OF_BUFFER; i++){
            // Get a ANativeWindowBuffer
            window->dequeueBuffer_DEPRECATED(window.get(), &ANBuffer);

            // Create GraphicBuffer using ANB
            buffer[i] = new GraphicBuffer(ANBuffer, false);			
        }
        for(int i =0; i < NUMBER_OF_BUFFER; i++){	
            {
                // Get a ANB's pointer and Fill Red, Green and Blue color
                buffer[i]->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&pBufferAddr[i]));
                for(int j = 0; j < NUMBER_OF_PIXEL ; j++)
                    pBufferAddr[i][j] = (i==0) ? RED_COLOR:
                        (i==1) ? GREEN_COLOR:
                        BLUE_COLOR;
                buffer[i]->unlock();
            }
            // Send the Buffer to SurfaceFlinger service
            window->queueBuffer_DEPRECATED(window.get(),  buffer[i]->getNativeBuffer());

            usleep(1000000);
        }
    }
    return 0;
}

