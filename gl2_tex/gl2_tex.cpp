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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/Timers.h>

#include <WindowSurface.h>
#include <ui/GraphicBuffer.h>
#include <EGLUtils.h>

#include "cutils/trace.h"

using namespace android;

static const char gVertexShader[] = "attribute vec4 vPosition;\n"
    "varying vec2 texCoords;\n"
    "void main() {\n"
    "  texCoords = vPosition.xy + vec2(0.5, 0.5);\n"
    "  gl_Position = vPosition;\n"
    "}\n";

static const char gFragmentShader[] = "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "uniform samplerExternalOES texSampler;\n"
    "varying vec2 texCoords;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(texSampler, texCoords);\n"
    "}\n";

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
            } else {
                fprintf(stderr, "Guessing at GL_INFO_LOG_LENGTH size\n");
                char* buf = (char*) malloc(0x1000);
                if (buf) {
                    glGetShaderInfoLog(shader, 0x1000, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        glAttachShader(program, pixelShader);
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

GLuint gProgram;
GLint gvPositionHandle;
GLint gTexSamplerHandle;

bool setupGraphics(int w, int h) {
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        return false;
    }
    gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
    fprintf(stderr, "glGetAttribLocation(\"vPosition\") = %d\n",
            gvPositionHandle);
    gTexSamplerHandle = glGetUniformLocation(gProgram, "texSampler");
    fprintf(stderr, "glGetUniformLocation(\"texSampler\") = %d\n",
            gTexSamplerHandle);

    glViewport(0, 0, w, h);
    return true;
}

const int texWidth = 608;
const int texHeight = 480;
const int texUsage = GraphicBuffer::USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;
const int texFormat = HAL_PIXEL_FORMAT_RGBA_8888;
static sp<GraphicBuffer> texBuffer;
static GLuint tex;

bool setupTexSurface(EGLDisplay dpy, EGLContext context) {
    texBuffer = new GraphicBuffer(texWidth, texHeight, texFormat, texUsage);

    char* buf = NULL;

    status_t err = texBuffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&buf));
    for (int x = 0; x < texWidth*texHeight*4; x++) {
            buf[x]=0xaa;
    }
    err = texBuffer->unlock();

    EGLClientBuffer clientBuffer = (EGLClientBuffer)texBuffer->getNativeBuffer();
    EGLImageKHR img = eglCreateImageKHR(dpy,
            EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
            clientBuffer, 0);
    if (img == EGL_NO_IMAGE_KHR) {
        return false;
    }

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);

    return true;
}

const GLfloat gTriangleVertices[] = {
    -0.5f, 0.5f,
    -0.5f, -0.5f,
    0.5f, -0.5f,
    0.5f, 0.5f,
};

void renderFrame() {
    static float fRColor = 0.0f;
    glClearColor(fRColor, 0.0f, 1.0f, 1.0f);
    fRColor += 0.01f;
    if (fRColor >= 1.0f){
        fRColor = 0.0f;
    }

    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    glUseProgram(gProgram);

    glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, gTriangleVertices);
    glEnableVertexAttribArray(gvPositionHandle);

    glUniform1i(gTexSamplerHandle, 0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

int main(int argc, char** argv) {
    EGLBoolean returnValue;
    EGLConfig myConfig = {0};

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint s_configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE };
    EGLint majorVersion;
    EGLint minorVersion;
    EGLContext context;
    EGLSurface surface;
    EGLint w, h;

    EGLDisplay dpy;

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
        return 0;
    }

    returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
    if (returnValue != EGL_TRUE) {
        printf("eglInitialize failed\n");
        return 0;
    }

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();
    returnValue = EGLUtils::selectConfigForNativeWindow(dpy, s_configAttribs, window, &myConfig);
    if (returnValue) {
        printf("EGLUtils::selectConfigForNativeWindow() returned %d", returnValue);
        return 1;
    }

    surface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
    if (surface == EGL_NO_SURFACE) {
        printf("gelCreateWindowSurface failed.\n");
        return 1;
    }

    context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return 1;
    }
    returnValue = eglMakeCurrent(dpy, surface, surface, context);
    if (returnValue != EGL_TRUE) {
        return 1;
    }
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);

    if(!setupTexSurface(dpy, context)) {
        fprintf(stderr, "Could not set up texture surface.\n");
        return 1;
    }

    if(!setupGraphics(w, h)) {
        fprintf(stderr, "Could not set up graphics.\n");
        return 1;
    }

    for (;;) {
        atrace_set_debuggable(true);
        renderFrame();
        eglSwapBuffers(dpy, surface);
    }

    return 0;
}
