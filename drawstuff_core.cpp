// drawstuff_core.cpp
// This code is rewritten from drawstuff (part of ODE) to provide
// backward compatibility for programs that used drawstuff for rendering.

// Original drawstuff license:
/*************************************************************************
 *                                                                       *
 * Open Dynamics Engine, Copyright (C) 2001-2003 Russell L. Smith.       *
 * All rights reserved.  Email: russ@q12.org   Web: www.q12.org          *
 *                                                                       *
 * This library is free software; you can redistribute it and/or         *
 * modify it under the terms of EITHER:                                  *
 *   (1) The GNU Lesser General Public License as published by the Free  *
 *       Software Foundation; either version 2.1 of the License, or (at  *
 *       your option) any later version. The text of the GNU Lesser      *
 *       General Public License is included with this library in the     *
 *       file LICENSE.TXT.                                               *
 *   (2) The BSD-style license that is included with this library in     *
 *       the file LICENSE-BSD.TXT.                                       *
 *                                                                       *
 * This library is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the files    *
 * LICENSE.TXT and LICENSE-BSD.TXT for more details.                     *
 *                                                                       *
 *************************************************************************/
#include "drawstuff_core.hpp"

#ifdef WIN32
#include <windows.h>
#endif

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ode/ode.h>
// #include "config.h"

#ifdef HAVE_APPLE_OPENGL_FRAMEWORK
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include "drawstuff_core.hpp"
#include <X11/Xlib.h>  // Display, Window, XChangeProperty など
#include <X11/Xatom.h> // XA_STRING, XA_WM_NAME など
#include <GL/glx.h>
#include <sys/time.h> // gettimeofday
// 必要に応じて <GL/gl.h> など既存の include も

const char *DEFAULT_PATH_TO_TEXTURES = "../textures/";
// drawstuff_core.cpp

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

namespace
{

    GLuint compileShader(GLenum type, const char *src)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
            std::string log(logLen, '\0');
            glGetShaderInfoLog(shader, logLen, nullptr, log.data());
            fprintf(stderr, "Shader compile error:\n%s\n", log.c_str());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint linkProgram(GLuint vs, GLuint fs)
    {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);

        GLint ok = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLen = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
            std::string log(logLen, '\0');
            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
            fprintf(stderr, "Program link error:\n%s\n", log.c_str());
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    }

} // namespace

namespace ds_internal
{
    // 頂点構造体の例（好きな場所に定義）
    struct VertexPNC
    {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec4 color;
    };

    // Report error and exit
    void fatalError(const char *msg, ...)
    {
        va_list ap;
        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        fprintf(stderr, "\n");
        va_end(ap);
        exit(EXIT_FAILURE);
    }

    // Report error and abort with core dump (for debugging)
    void internalError(const char *msg, ...)
    {
        va_list ap;
        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        fprintf(stderr, "\n");
        va_end(ap);
        abort();
    }

    // skip over whitespace and comments in a stream.
    void skipWhiteSpace(const char *filename, FILE *f)
    {
        int c, d;
        for (;;)
        {
            c = fgetc(f);
            if (c == EOF)
                fatalError("unexpected end of file in \"%s\"", filename);

            // skip comments
            if (c == '#')
            {
                do
                {
                    d = fgetc(f);
                    if (d == EOF)
                        fatalError("unexpected end of file in \"%s\"", filename);
                } while (d != '\n');
                continue;
            }

            if (c > ' ')
            {
                ungetc(c, f);
                return;
            }
        }
    }

    // read a number from a stream, this return 0 if there is none (that's okay
    // because 0 is a bad value for all PPM numbers anyway).
    int readNumber(const char *filename, FILE *f)
    {
        int c, n = 0;
        for (;;)
        {
            c = fgetc(f);
            if (c == EOF)
                fatalError("unexpected end of file in \"%s\"", filename);
            if (c >= '0' && c <= '9')
                n = n * 10 + (c - '0');
            else
            {
                ungetc(c, f);
                return n;
            }
        }
    }
    //***************************************************************************
    typedef unsigned char byte;
    class Image
    {
        int image_width, image_height;
        byte *image_data;

    public:
        Image(const char *filename)
        {
            FILE *f = fopen(filename, "rb");
            if (!f)
                fatalError("Can't open image file `%s'", filename);

            // read in header
            if (fgetc(f) != 'P' || fgetc(f) != '6')
                fatalError("image file \"%s\" is not a binary PPM (no P6 header)", filename);
            skipWhiteSpace(filename, f);

            // read in image parameters
            image_width = readNumber(filename, f);
            skipWhiteSpace(filename, f);
            image_height = readNumber(filename, f);
            skipWhiteSpace(filename, f);
            int max_value = readNumber(filename, f);

            // check values
            if (image_width < 1 || image_height < 1)
                fatalError("bad image file \"%s\"", filename);
            if (max_value != 255)
                fatalError("image file \"%s\" must have color range of 255", filename);

            // read either nothing, LF (10), or CR,LF (13,10)
            int c = fgetc(f);
            if (c == 10)
            {
                // LF
            }
            else if (c == 13)
            {
                // CR
                c = fgetc(f);
                if (c != 10)
                    ungetc(c, f);
            }
            else
                ungetc(c, f);

            // read in rest of data
            image_data = new byte[image_width * image_height * 3];
            if (fread(image_data, image_width * image_height * 3, 1, f) != 1)
                fatalError("Can not read data from image file `%s'", filename);
            fclose(f);
        }
        // load from PPM file
        ~Image()
        {
            delete[] image_data;
        }
        int width() { return image_width; }
        int height() { return image_height; }
        byte *data() { return image_data; }
    };

    //***************************************************************************
    // Texture object.
    class Texture
    {
        Image *image;
        GLuint name;

    public:
        Texture(const char *filename)
        {
            image = new Image(filename);
            glGenTextures(1, &name);
            glBindTexture(GL_TEXTURE_2D, name);

            // set pixel unpacking mode
            glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

            // glTexImage2D (GL_TEXTURE_2D, 0, 3, image->width(), image->height(), 0,
            //		   GL_RGB, GL_UNSIGNED_BYTE, image->data());
            gluBuild2DMipmaps(GL_TEXTURE_2D, 3, image->width(), image->height(),
                              GL_RGB, GL_UNSIGNED_BYTE, image->data());

            // set texture parameters - will these also be bound to the texture???
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_LINEAR);

            glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
        }
        ~Texture()
        {
            delete image;
            glDeleteTextures(1, &name);
        }
        void bind(int modulate)
        {
            glBindTexture(GL_TEXTURE_2D, name);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                      modulate ? GL_MODULATE : GL_DECAL);
        }
    };

    constexpr int DS_NUMTEXTURES = 4; // number of standard textures
    std::array<std::unique_ptr<Texture>, DS_NUMTEXTURES + 1> texture;
    Texture *sky_texture = nullptr;
    Texture *ground_texture = nullptr;
    Texture *wood_texture = nullptr;
    Texture *checkered_texture = nullptr;

    // ================ DrawstuffApp implementation =================
    DrawstuffApp &DrawstuffApp::instance()
    {
        static DrawstuffApp inst;
        return inst;
    }

    DrawstuffApp::DrawstuffApp()
        : current_state(SIM_STATE_NOT_STARTED),
        use_textures(true),
        use_shadows(true),
        writeframes(false),
        pausemode(false),
        view_xyz{{2.0f, 0.0f, 1.0f}},
        view_hpr{{180.0f, 0.0f, 0.0f}},
        current_color{{1.0f, 1.0f, 1.0f, 1.0f}},
        texture_id(0),
        callbacks_(nullptr)
    {
        // ここで旧 initMotionModel() の中身をそのまま書いてもよいし、
        // 別メンバ関数に切り出してもよい。
    }

    DrawstuffApp::~DrawstuffApp() = default;

    void DrawstuffApp::initMotionModel()
    {
        view_xyz = {{2.0f, 0.0f, 1.0f}};
        view_hpr = {{180.0f, 0.0f, 0.0f}};
    }

    void DrawstuffApp::initBasicProgram()
    {
        // すでに作ってあれば何もしない
        if (programBasic_ != 0)
            return;

        // ライティング付きシェーダ
        static const char *vsSrc = R"GLSL(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNormal;

        uniform mat4 uMVP;
        uniform mat4 uModel;

        out vec3 vNormal;

        void main()
        {
            gl_Position = uMVP * vec4(aPos, 1.0);
            vNormal = mat3(uModel) * aNormal;
        }
    )GLSL";

        static const char *fsSrc = R"GLSL(
        #version 330 core
        in vec3 vNormal;

        uniform vec4 uColor;

        out vec4 FragColor;

        void main()
        {
            vec3 N = normalize(vNormal);
            // 適当な平行光源
            float diff = max(dot(N, normalize(vec3(0.2, 0.5, 1.0))), 0.0);
            vec3 rgb = uColor.rgb * (0.3 + 0.7 * diff);
            FragColor = vec4(rgb, uColor.a);
        }
    )GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs)
        {
            internalError("Failed to compile basic shaders");
        }

        programBasic_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programBasic_)
        {
            internalError("Failed to link basic shader program");
        }

        

        // uniform ロケーションを取得
        uMVP_ = glGetUniformLocation(programBasic_, "uMVP");
        uModel_ = glGetUniformLocation(programBasic_, "uModel");
        uColor_ = glGetUniformLocation(programBasic_, "uColor");
    }

    void DrawstuffApp::initGroundProgram()
    {
        if (programGround_ != 0)
            return;
        static const char *ground_vs_src = R"GLSL(
// ground_vs.glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;

// ground_scale, ground_ofsx, ground_ofsy を uniform で渡す想定
uniform float uGroundScale;
uniform vec2  uGroundOffset;

out vec3 vNormal;
out vec2 vTex;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;

    // 元の fixed-function の対応：
    // u = x * ground_scale + ground_ofsx
    // v = y * ground_scale + ground_ofsy
    vTex = aPos.xy * uGroundScale + uGroundOffset;
}
)GLSL";

        static const char *ground_fs_src = R"GLSL(
        #version 330 core
        in vec3 vNormal;
        in vec2 vTex;

        uniform sampler2D uTex;
        uniform vec4 uColor;
        uniform int  uUseTex; // 1 = テクスチャ使用, 0 = 単色

        out vec4 FragColor;

        void main()
        {
            vec3 rgb;

            if (uUseTex == 1)
            {
                vec4 texColor = texture(uTex, vTex);
                rgb = uColor.rgb * texColor.rgb;
            }
            else
            {
                // テクスチャなし
                rgb = uColor.rgb;
            }
            FragColor = vec4(rgb, uColor.a);
        }
    )GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, ground_vs_src); // 上記 GLSL
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, ground_fs_src);
        if (!vs || !fs)
            internalError("Failed to compile ground shaders");

        programGround_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programGround_)
            internalError("Failed to link ground shader program");

        uGroundMVP_ = glGetUniformLocation(programGround_, "uMVP");
        uGroundModel_ = glGetUniformLocation(programGround_, "uModel");
        uGroundColor_ = glGetUniformLocation(programGround_, "uColor");
        uGroundTex_ = glGetUniformLocation(programGround_, "uTex");
        uGroundScale_ = glGetUniformLocation(programGround_, "uGroundScale");
        uGroundOffset_ = glGetUniformLocation(programGround_, "uGroundOffset");
        uGroundUseTex_ = glGetUniformLocation(programGround_, "uUseTex");
    }

    void DrawstuffApp::initGroundMesh()
    {
        if (vaoGround_ != 0)
            return; // すでに初期化済みなら何もしない

        const float gsize = 100.0f;
        const float offset = 0.0f; // 元コードの offset

        // 平面 z = offset 上の四角形を 2 つの三角形に分割（6頂点）
        glm::vec3 p0(-gsize, -gsize, offset);
        glm::vec3 p1(gsize, -gsize, offset);
        glm::vec3 p2(gsize, gsize, offset);
        glm::vec3 p3(-gsize, gsize, offset);

        glm::vec3 n(0.0f, 0.0f, 1.0f);
        glm::vec4 color(GROUND_R, GROUND_G, GROUND_B, 1.0f);

        std::array<VertexPNC, 6> verts = {
            VertexPNC{p0, n, color},
            VertexPNC{p1, n, color},
            VertexPNC{p2, n, color},

            VertexPNC{p0, n, color},
            VertexPNC{p2, n, color},
            VertexPNC{p3, n, color},
        };

        glGenVertexArrays(1, &vaoGround_);
        glGenBuffers(1, &vboGround_);

        glBindVertexArray(vaoGround_);
        glBindBuffer(GL_ARRAY_BUFFER, vboGround_);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(verts),
                     verts.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPNC), (void *)offsetof(VertexPNC, pos));

        // layout(location = 1) normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPNC), (void *)offsetof(VertexPNC, normal));

        // layout(location = 2) color
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE,
            sizeof(VertexPNC), (void *)offsetof(VertexPNC, color));

        glBindVertexArray(0);
    }

    void DrawstuffApp::initSkyProgram()
    {
        if (programSky_ != 0)
            return;
        static const char *sky_vs_src = R"GLSL(
#version 330 core

layout(location = 0) in vec3 aPos;

uniform mat4 uMVP;
uniform float uSkyScale;   // sky_scale
uniform float uSkyOffset;  // offset

out vec2 vTex;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);

    // aPos.xy は [-ssize, ssize] の範囲
    //   vTex = aPos.xy * sky_scale + offset
    vTex = aPos.xy * uSkyScale + vec2(uSkyOffset);
}
)GLSL";

        static const char *sky_fs_src = R"GLSL(
#version 330 core

in vec2 vTex;

uniform sampler2D uTex;
uniform vec4      uColor;   // テクスチャを使わないときの空色
uniform int       uUseTex;  // 1 = テクスチャ, 0 = 単色

out vec4 FragColor;

void main()
{
    vec4 color = uColor;

    if (uUseTex != 0)
    {
        FragColor = texture(uTex, vTex);  // テクスチャそのまま
    }
    else
    {
        FragColor = uColor;
    }
}
)GLSL";

        GLuint vs = compileShader(GL_VERTEX_SHADER, sky_vs_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, sky_fs_src);
        if (!vs || !fs)
            internalError("Failed to compile sky shaders");

        programSky_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!programSky_)
            internalError("Failed to link sky shader program");

        uSkyMVP_ = glGetUniformLocation(programSky_, "uMVP");
        uSkyColor_ = glGetUniformLocation(programSky_, "uColor");
        uSkyTex_ = glGetUniformLocation(programSky_, "uTex");
        uSkyScale_ = glGetUniformLocation(programSky_, "uSkyScale");
        uSkyOffset_ = glGetUniformLocation(programSky_, "uSkyOffset");
        uSkyUseTex_ = glGetUniformLocation(programSky_, "uUseTex");
    }

    void DrawstuffApp::initSkyMesh()
    {
        if (vaoSky_ != 0)
            return;

        const float ssize = 1000.0f; // 元コードと同じ

        glm::vec3 p0(-ssize, -ssize, 0.0f);
        glm::vec3 p1(-ssize, ssize, 0.0f);
        glm::vec3 p2(ssize, ssize, 0.0f);
        glm::vec3 p3(ssize, -ssize, 0.0f);

        glm::vec3 n(0.0f, 0.0f, -1.0f);          // 一応
        glm::vec4 color(1.0f, 1.0f, 1.0f, 1.0f); // テクスチャ時は uColor=1 で塗る

        std::array<VertexPNC, 6> verts = {
            VertexPNC{p0, n, color},
            VertexPNC{p1, n, color},
            VertexPNC{p2, n, color},

            VertexPNC{p0, n, color},
            VertexPNC{p2, n, color},
            VertexPNC{p3, n, color},
        };

        glGenVertexArrays(1, &vaoSky_);
        glGenBuffers(1, &vboSky_);

        glBindVertexArray(vaoSky_);
        glBindBuffer(GL_ARRAY_BUFFER, vboSky_);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(verts),
                     verts.data(),
                     GL_STATIC_DRAW);

        // layout(location = 0) position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPNC), (void *)offsetof(VertexPNC, pos));

        // layout(location = 1) normal（今回は使わないが統一しておく）
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(VertexPNC), (void *)offsetof(VertexPNC, normal));

        // layout(location = 2) color（今回は使わない）
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2, 4, GL_FLOAT, GL_FALSE,
            sizeof(VertexPNC), (void *)offsetof(VertexPNC, color));

        glBindVertexArray(0);
    }

    Display *display = nullptr;
    int screen = 0;
    XVisualInfo *visual = nullptr; // best visual for openGL
    Colormap colormap = 0;         // window's colormap
    Atom wm_protocols_atom = 0;
    Atom wm_delete_window_atom = 0;

    // window and openGL
    Window win = 0;             // X11 window, 0 if not initialized
    int width = 0, height = 0;  // window size
    GLXContext glx_context = 0; // openGL rendering context
    int last_key_pressed = 0;   // last key pressed in the window
    int run = 1;                // 1 if simulation running
    int pausemode = 0;          // 1 if in `pause' mode
    int singlestep = 0;         // 1 if single step key pressed
    int writeframes = 0;        // 1 if frame files to be written

    void DrawstuffApp::createMainWindow(const int _width, const int _height)
    {
        // create X11 display connection
        display = XOpenDisplay(NULL);
        if (!display)
            fatalError("can not open X11 display");
        screen = DefaultScreen(display);

        // get GL visual
        static int attribListDblBuf[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16,
                                         GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE, 4, None};
        static int attribList[] = {GLX_RGBA, GLX_DEPTH_SIZE, 16,
                                   GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE, 4, None};
        visual = glXChooseVisual(display, screen, attribListDblBuf);
        if (!visual)
            visual = glXChooseVisual(display, screen, attribList);
        if (!visual)
            fatalError("no good X11 visual found for OpenGL");

        // create colormap
        colormap = XCreateColormap(display, RootWindow(display, screen),
                                   visual->visual, AllocNone);

        // initialize variables
        win = 0;
        width = _width;
        height = _height;
        glx_context = 0;
        last_key_pressed = 0;

        if (width < 1 || height < 1)
            internalError(0, "bad window width or height");

        // create the window
        XSetWindowAttributes attributes;
        attributes.background_pixel = BlackPixel(display, screen);
        attributes.colormap = colormap;
        attributes.event_mask = ButtonPressMask | ButtonReleaseMask |
                                KeyPressMask | KeyReleaseMask | ButtonMotionMask | PointerMotionHintMask |
                                StructureNotifyMask;
        win = XCreateWindow(display, RootWindow(display, screen), 50, 50, width, height,
                            0, visual->depth, InputOutput, visual->visual,
                            CWBackPixel | CWColormap | CWEventMask, &attributes);

        // associate a GLX context with the window
        glx_context = glXCreateContext(display, visual, 0, GL_TRUE);
        if (!glx_context)
            fatalError("can't make an OpenGL context");

        // set the window title
        XTextProperty window_name;
        window_name.value = (unsigned char *)"Simulation";
        window_name.encoding = XA_STRING;
        window_name.format = 8;
        window_name.nitems = strlen((char *)window_name.value);
        XSetWMName(display, win, &window_name);

        // participate in the window manager 'delete yourself' protocol
        wm_protocols_atom = XInternAtom(display, "WM_PROTOCOLS", False);
        wm_delete_window_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
        if (XSetWMProtocols(display, win, &wm_delete_window_atom, 1) == 0)
            fatalError("XSetWMProtocols() call failed");

        // pop up the window
        XMapWindow(display, win);
        XSync(display, win);
    }

    void destroyMainWindow()
    {
        glXDestroyContext(display, glx_context);
        XDestroyWindow(display, win);
        XSync(display, 0);
        XCloseDisplay(display);
        display = 0;
        win = 0;
        glx_context = 0;
    }

    void DrawstuffApp::handleEvent(XEvent &event, const dsFunctions *fn)
    {
        static int mx = 0, my = 0; // mouse position
        static int mode = 0;       // mouse button bits

        switch (event.type)
        {

        case ButtonPress:
        {
            if (event.xbutton.button == Button1)
                mode |= 1;
            if (event.xbutton.button == Button2)
                mode |= 2;
            if (event.xbutton.button == Button3)
                mode |= 4;
            mx = event.xbutton.x;
            my = event.xbutton.y;
        }
            return;

        case ButtonRelease:
        {
            if (event.xbutton.button == Button1)
                mode &= (~1);
            if (event.xbutton.button == Button2)
                mode &= (~2);
            if (event.xbutton.button == Button3)
                mode &= (~4);
            mx = event.xbutton.x;
            my = event.xbutton.x;
        }
            return;

        case MotionNotify:
        {
            if (event.xmotion.is_hint)
            {
                Window root, child;
                unsigned int mask;
                XQueryPointer(display, win, &root, &child, &event.xbutton.x_root,
                              &event.xbutton.y_root, &event.xbutton.x, &event.xbutton.y,
                              &mask);
            }
            motion(mode, event.xmotion.x - mx, event.xmotion.y - my);
            mx = event.xmotion.x;
            my = event.xmotion.y;
        }
            return;

        case KeyPress:
        {
            KeySym key;
            XLookupString(&event.xkey, NULL, 0, &key, 0);
            if ((event.xkey.state & ControlMask) == 0)
            {
                if (key >= ' ' && key <= 126 && fn->command)
                    fn->command(key);
            }
            else if (event.xkey.state & ControlMask)
            {
                switch (key)
                {
                case 't':
                case 'T':
                    use_textures = !use_textures;
                    break;
                case 's':
                case 'S':
                    use_shadows = !use_shadows;
                    break;
                case 'x':
                case 'X':
                    stopSimulation();
                    break;
                case 'p':
                case 'P':
                    pausemode ^= 1;
                    singlestep = 0;
                    break;
                case 'o':
                case 'O':
                    if (pausemode)
                        singlestep = 1;
                    break;
                case 'v':
                case 'V':
                {
                    float xyz[3], hpr[3];
                    getViewpoint(xyz, hpr);
                    printf("Viewpoint = (%.4f,%.4f,%.4f,%.4f,%.4f,%.4f)\n",
                           xyz[0], xyz[1], xyz[2], hpr[0], hpr[1], hpr[2]);
                    break;
                }
                case 'w':
                case 'W':
                    writeframes ^= 1;
                    if (writeframes)
                        printf("Now writing frames to PPM files\n");
                    break;
                }
            }
            last_key_pressed = key; // a kludgy place to put this...
        }
            return;

        case KeyRelease:
        {
            // hmmmm...
        }
            return;

        case ClientMessage:
            if (event.xclient.message_type == wm_protocols_atom &&
                event.xclient.format == 32 &&
                Atom(event.xclient.data.l[0]) == wm_delete_window_atom)
            {
                run = 0;
                return;
            }
            return;

        case ConfigureNotify:
            width = event.xconfigure.width;
            height = event.xconfigure.height;
            return;
        }
    }

    // return the index of the highest bit
    static int getHighBitIndex(unsigned int x)
    {
        int i = 0;
        while (x)
        {
            i++;
            x >>= 1;
        }
        return i - 1;
    }

// shift x left by i, where i can be positive or negative
#define SHIFTL(x, i) (((i) >= 0) ? ((x) << (i)) : ((x) >> (-i)))

    void DrawstuffApp::captureFrame(const int num)
    {
        fprintf(stderr, "capturing frame %04d\n", num);

        char s[100];
        sprintf(s, "frame/frame%04d.ppm", num);
        FILE *f = fopen(s, "wb");
        if (!f)
            fatalError("can't open \"%s\" for writing", s);
        fprintf(f, "P6\n%d %d\n255\n", width, height);
        XImage *image = XGetImage(display, win, 0, 0, width, height, ~0, ZPixmap);

        int rshift = 7 - getHighBitIndex(image->red_mask);
        int gshift = 7 - getHighBitIndex(image->green_mask);
        int bshift = 7 - getHighBitIndex(image->blue_mask);

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                unsigned long pixel = XGetPixel(image, x, y);
                unsigned char b[3];
                b[0] = SHIFTL(pixel & image->red_mask, rshift);
                b[1] = SHIFTL(pixel & image->green_mask, gshift);
                b[2] = SHIFTL(pixel & image->blue_mask, bshift);
                fwrite(b, 3, 1, f);
            }
        }
        fclose(f);
        XDestroyImage(image);
    }

    void DrawstuffApp::processDrawFrame(int *frame, const dsFunctions *fn)
    {
        drawFrame(width, height, fn, pausemode && !singlestep);
        singlestep = 0;

        glFlush();
        glXSwapBuffers(display, win);
        XSync(display, 0);

        // capture frames if necessary
        if (pausemode == 0 && writeframes)
        {
            captureFrame(*frame);
            (*frame)++;
        }
    }

    void microsleep(int usecs)
    {
        if (usecs <= 0)
            return;
        std::this_thread::sleep_for(std::chrono::microseconds(usecs));
    }

    void DrawstuffApp::platformSimulationLoop(const int window_width, const int window_height, const dsFunctions *fn,
                                              const int initial_pause)
    {
        pausemode = initial_pause;
        createMainWindow(window_width, window_height);
        glXMakeCurrent(display, win, glx_context);

        startGraphics(window_width, window_height, fn);

        static bool firsttime = true;
        if (firsttime)
        {
            fprintf(
                stderr,
                "\n"
                "Simulation test environment (drawstuffCompat)\n"
                "   Ctrl-P : pause / unpause (or say `-pause' on command line).\n"
                "   Ctrl-O : single step when paused.\n"
                "   Ctrl-T : toggle textures (or say `-notex' on command line).\n"
                "   Ctrl-S : toggle shadows (or say `-noshadow' on command line).\n"
                "   Ctrl-V : print current viewpoint coordinates (x,y,z,h,p,r).\n"
                "   Ctrl-W : write frames to ppm files: frame/frameNNN.ppm\n"
                "   Ctrl-X : exit.\n"
                "\n"
                "Change the camera position by clicking + dragging in the window.\n"
                "   Left button - pan and tilt.\n"
                "   Right button - forward and sideways.\n"
                "   Left + Right button (or middle button) - sideways and up.\n"
                "\n");
            firsttime = false;
        }

        if (fn->start)
            fn->start();

        timeval tv;
        gettimeofday(&tv, 0);
        double prev = tv.tv_sec + (double)tv.tv_usec / 1000000.0;

        int frame = 1;
        run = 1;
        while (run)
        {
            // read in and process all pending events for the main window
            XEvent event;
            while (run && XPending(display))
            {
                XNextEvent(display, &event);
                handleEvent(event, fn);
            }

            gettimeofday(&tv, 0);
            double curr = tv.tv_sec + (double)tv.tv_usec / 1000000.0;
            if (curr - prev >= 1.0 / 60.0)
            {
                prev = curr;
                processDrawFrame(&frame, fn);
            }
            else
                microsleep(1000);
        };

        if (fn->stop)
            fn->stop();
        stopGraphics();

        destroyMainWindow();
    }

    int DrawstuffApp::runSimulation(const int argc, const char *const argv[],
                                    const int window_width, const int window_height,
                                    const dsFunctions *fn)
    {
        if (current_state != SIM_STATE_NOT_STARTED)
        {
            fatalError("DrawstuffApp::runSimulation() called more than once");
            return -1;
        }
        current_state = SIM_STATE_RUNNING;
        callbacks_storage_ = *fn;
        callbacks_ = &callbacks_storage_;

        int initial_pause = 0;
        for (int i = 1; i < argc; ++i)
        {
            if (strcmp(argv[i], "-notex") == 0)
                use_textures = false;
            if (strcmp(argv[i], "-noshadow") == 0)
                use_shadows = false;
            if (strcmp(argv[i], "-noshadows") == 0)
                use_shadows = false;
            if (strcmp(argv[i], "-pause") == 0)
                initial_pause = 1;
            if (strcmp(argv[i], "-texturepath") == 0 && ++i < argc)
            {
                if (++i < argc)
                    callbacks_storage_.path_to_textures = argv[i];
            }
        }

        // ウィンドウ生成・GL 初期化・メインループなど、
        // 既存の dsSimulationLoop の残りをここに移していく
        // （当面は OpenGL 1.x のままで OK）
        initMotionModel();
        platformSimulationLoop(window_width, window_height, callbacks_, initial_pause);

        current_state = SIM_STATE_NOT_STARTED;

        return 0;
    }

    void DrawstuffApp::stopSimulation()
    {
        if (current_state != SIM_STATE_RUNNING)
        {
            fatalError("DrawstuffApp::stopSimulation() called without a running simulation");
            return;
        }
        current_state = SIM_STATE_DRAWING; // stopping
    }

    void DrawstuffApp::wrapCameraAngles()
    {
        for (int i = 0; i < 3; i++)
        {
            while (view_hpr[i] > 180)
                view_hpr[i] -= 360;
            while (view_hpr[i] < -180)
                view_hpr[i] += 360;
        }
    }

    void DrawstuffApp::motion(const int mode, const int deltax, const int deltay)
    {
        float side = 0.01f * float(deltax);
        float fwd = (mode == 4) ? (0.01f * float(deltay)) : 0.0f;
        float s = (float)sin(view_hpr[0] * DEG_TO_RAD);
        float c = (float)cos(view_hpr[0] * DEG_TO_RAD);

        if (mode == 1)
        {
            view_hpr[0] += float(deltax) * 0.5f;
            view_hpr[1] += float(deltay) * 0.5f;
        }
        else
        {
            view_xyz[0] += -s * side + c * fwd;
            view_xyz[1] += c * side + s * fwd;
            if (mode == 2 || mode == 5)
                view_xyz[2] += 0.01f * float(deltay);
        }
        wrapCameraAngles();
    }

    void DrawstuffApp::getViewpoint(float xyz[3], float hpr[3])
    {
        if (current_state == SIM_STATE_NOT_STARTED)
            fatalError("DrawstuffApp::getViewpoint() called before simulation started");
        if (xyz)
        {
            xyz[0] = view_xyz[0];
            xyz[1] = view_xyz[1];
            xyz[2] = view_xyz[2];
        }
        if (hpr)
        {
            hpr[0] = view_hpr[0];
            hpr[1] = view_hpr[1];
            hpr[2] = view_hpr[2];
        }
    }

    void DrawstuffApp::setViewpoint(const float xyz[3], const float hpr[3])
    {
        if (current_state == SIM_STATE_NOT_STARTED)
            fatalError("DrawstuffApp::setViewpoint() called before simulation started");
        if (xyz)
        {
            view_xyz[0] = xyz[0];
            view_xyz[1] = xyz[1];
            view_xyz[2] = xyz[2];
        }
        if (hpr)
        {
            view_hpr[0] = hpr[0];
            view_hpr[1] = hpr[1];
            view_hpr[2] = hpr[2];
            wrapCameraAngles();
        }
    }

    void DrawstuffApp::storeColor(const float r, const float g, const float b, const float alpha)
    {
        current_color[0] = r;
        current_color[1] = g;
        current_color[2] = b;
        current_color[3] = alpha;
    }

    void DrawstuffApp::setColor(const float r, const float g, const float b, const float alpha)
    {
        GLfloat light_ambient[4], light_diffuse[4], light_specular[4];
        light_ambient[0] = r * 0.3f;
        light_ambient[1] = g * 0.3f;
        light_ambient[2] = b * 0.3f;
        light_ambient[3] = alpha;
        light_diffuse[0] = r * 0.7f;
        light_diffuse[1] = g * 0.7f;
        light_diffuse[2] = b * 0.7f;
        light_diffuse[3] = alpha;
        light_specular[0] = r * 0.2f;
        light_specular[1] = g * 0.2f;
        light_specular[2] = b * 0.2f;
        light_specular[3] = alpha;
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, light_ambient);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, light_diffuse);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, light_specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 5.0f);
    }

    void DrawstuffApp::setTexture(int texnum)
    {
        texture_id = texnum;
    }

    void DrawstuffApp::setupDrawingMode()
    {
        glEnable(GL_LIGHTING);
        if (texture_id)
        {
            if (use_textures)
            {
                glEnable(GL_TEXTURE_2D);
                texture[texture_id]->bind(1);
                glEnable(GL_TEXTURE_GEN_S);
                glEnable(GL_TEXTURE_GEN_T);
                glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
                glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
                static GLfloat s_params[4] = {1.0f, 1.0f, 0.0f, 1};
                static GLfloat t_params[4] = {0.817f, -0.817f, 0.817f, 1};
                glTexGenfv(GL_S, GL_OBJECT_PLANE, s_params);
                glTexGenfv(GL_T, GL_OBJECT_PLANE, t_params);
            }
            else
            {
                glDisable(GL_TEXTURE_2D);
            }
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
        }
        setColor(current_color[0], current_color[1], current_color[2], current_color[3]);

        if (current_color[3] < 1)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    void DrawstuffApp::setShadowDrawingMode()
    {
        // ここに旧 setShadowDrawingMode() の中身をそのまま書く
        glDisable(GL_LIGHTING);
        if (use_textures)
        {
            glEnable(GL_TEXTURE_2D);
            ground_texture->bind(1);
            glColor3f(SHADOW_INTENSITY, SHADOW_INTENSITY, SHADOW_INTENSITY);
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_TEXTURE_GEN_S);
            glEnable(GL_TEXTURE_GEN_T);
            glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
            glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
            static GLfloat s_params[4] = {ground_scale, 0, 0, ground_ofsx};
            static GLfloat t_params[4] = {0, ground_scale, 0, ground_ofsy};
            glTexGenfv(GL_S, GL_EYE_PLANE, s_params);
            glTexGenfv(GL_T, GL_EYE_PLANE, t_params);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
            glColor3f(GROUND_R * SHADOW_INTENSITY, GROUND_G * SHADOW_INTENSITY,
                      GROUND_B * SHADOW_INTENSITY);
        }
        glDepthRange(0, 0.9999);
    }

    void DrawstuffApp::setShadowTransform()
    {
        // ここに影描画用変換行列設定コードを移植
        GLfloat matrix[16];
        for (int i = 0; i < 16; i++)
            matrix[i] = 0;
        matrix[0] = 1;
        matrix[5] = 1;
        matrix[8] = -LIGHTX;
        matrix[9] = -LIGHTY;
        matrix[15] = 1;
        glPushMatrix();
        glMultMatrixf(matrix);
    }

    void DrawstuffApp::drawSky(const float view_xyz[3])
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D); // core では意味無いが一応
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        initSkyProgram();
        initSkyMesh();

        // 元コードと同じ offset ロジック
        static float offset = 0.0f;
        offset += 0.002f;
        if (offset > 1.0f)
            offset -= 1.0f;

        // モデル行列：
        //   カメラ位置に合わせて xy をシフト、
        //   z は view_xyz[2] + sky_height の高さに。
        glm::mat4 model(1.0f);
        model = glm::translate(
            model,
            glm::vec3(view_xyz[0],
                      view_xyz[1],
                      view_xyz[2] + sky_height));

        glm::mat4 mvp = proj_ * view_ * model;

        // 「最奥にだけ書く」ための depth range
        glDepthRange(1, 1);

        glUseProgram(programSky_);
        glBindVertexArray(vaoSky_);

        glUniformMatrix4fv(uSkyMVP_, 1, GL_FALSE, glm::value_ptr(mvp));

        // 空色（テクスチャなしのとき用・あるいはテクスチャの色乗算）
        glm::vec4 skyColor(0.0f, 0.5f, 1.0f, 1.0f);
        glUniform4fv(uSkyColor_, 1, glm::value_ptr(skyColor));

        glUniform1f(uSkyScale_, sky_scale);
        glUniform1f(uSkyOffset_, offset);

        if (use_textures && sky_texture)
        {
            glUniform1i(uSkyUseTex_, 1);    // テクスチャを使う
            glActiveTexture(GL_TEXTURE0);
            sky_texture->bind(0);     // 内部で glBindTexture(GL_TEXTURE_2D, ...) している前提
            glUniform1i(uSkyTex_, 0); // sampler2D uTex にユニット 0 を対応付け
        }
        else
        {
            glUniform1i(uSkyUseTex_, 0); // テクスチャは使わない
            glUniform1i(uSkyTex_, 0);    // sampler2D uTex にユニット 0 を対応付け
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);
        glUseProgram(0);

        // depth 状態を元に戻す
        glDepthFunc(GL_LESS);
        glDepthRange(0, 1);
    }

    void DrawstuffApp::drawGround()
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D); // core では意味ないが一応
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_FOG);

        initGroundMesh();

        glm::mat4 model(1.0f);
        glm::mat4 mvp = proj_ * view_ * model;

        glUseProgram(programGround_);
        glBindVertexArray(vaoGround_);

        glUniformMatrix4fv(uGroundModel_, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(uGroundMVP_, 1, GL_FALSE, glm::value_ptr(mvp));

        glm::vec4 groundColor;
        if (use_textures) {
            groundColor = glm::vec4(1.0f);
            glUniform1i(uGroundUseTex_, 1); // テクスチャ有効
        }
        else
        {
            groundColor = glm::vec4(GROUND_R, GROUND_G, GROUND_B, 1.0f);
            glUniform1i(uGroundUseTex_, 0); // テクスチャ無効
        }
        glUniform4fv(uGroundColor_, 1, glm::value_ptr(groundColor));

        // 元のパラメータを uniform で渡す
        glUniform1f(uGroundScale_, ground_scale);
        glUniform2f(uGroundOffset_, ground_ofsx, ground_ofsy);

        // テクスチャをユニット0に bind
        glActiveTexture(GL_TEXTURE0);
        ground_texture->bind(0); // 内部で glBindTexture(GL_TEXTURE_2D, ...) している想定
        glUniform1i(uGroundTex_, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);
        glUseProgram(0);
    }

    void DrawstuffApp::drawPyramidGrid()
    {
        // まずは従来通りのステートセット（必要に応じて整理）
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        if (/* backend_ == GLBackend::Legacy */ false)
        {
            // ---- 旧実装：そのまま残しておく ----
            glEnable(GL_LIGHTING);
            glDisable(GL_TEXTURE_2D);
            glShadeModel(GL_FLAT);

            for (int i = -1; i <= 1; i++)
            {
                for (int j = -1; j <= 1; j++)
                {
                    glPushMatrix();
                    glTranslatef((float)i, (float)j, (float)0);
                    if (i == 1 && j == 0)
                        setColor(1, 0, 0, 1);
                    else if (i == 0 && j == 1)
                        setColor(0, 0, 1, 1);
                    else
                        setColor(1, 1, 0, 1);
                    const float k = 0.03f;
                    glBegin(GL_TRIANGLE_FAN);
                    glNormal3f(0, -1, 1);
                    glVertex3f(0, 0, k);
                    glVertex3f(-k, -k, 0);
                    glVertex3f(k, -k, 0);
                    glNormal3f(1, 0, 1);
                    glVertex3f(k, k, 0);
                    glNormal3f(0, 1, 1);
                    glVertex3f(-k, k, 0);
                    glNormal3f(-1, 0, 1);
                    glVertex3f(-k, -k, 0);
                    glEnd();
                    glPopMatrix();
                }
            }
            return;
        }

        // ---- ここから Core33 用実装 ----
        // 固定機能のライトは core では使えないので、シェーダ側でライティングする想定
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glShadeModel(GL_FLAT); // 互換プロファイル用。core では意味なし。

        static GLuint vao = 0;
        static GLuint vbo = 0;

        if (vao == 0)
        {
            // 1. まず「単位ピラミッド」のメッシュを一度だけ作る（k = 1 とする）
            constexpr float k = 1.0f;
            const glm::vec3 top(0.0f, 0.0f, k);
            const glm::vec3 p1(-k, -k, 0.0f);
            const glm::vec3 p2(k, -k, 0.0f);
            const glm::vec3 p3(k, k, 0.0f);
            const glm::vec3 p4(-k, k, 0.0f);

            // 法線は元コードに合わせて「面ごとに一定」。正規化しておく。
            auto n01 = glm::normalize(glm::vec3(0.0f, -1.0f, 1.0f));
            auto n12 = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
            auto n23 = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));
            auto n30 = glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f));

            // TRIANGLE_FAN ではなく、4枚の三角形として展開する（頂点12個）
            std::array<VertexPNC, 12> verts;

            // face 0: top, p1, p2
            verts[0] = {top, n01, glm::vec4(1.0f)};
            verts[1] = {p1, n01, glm::vec4(1.0f)};
            verts[2] = {p2, n01, glm::vec4(1.0f)};
            // face 1: top, p2, p3
            verts[3] = {top, n12, glm::vec4(1.0f)};
            verts[4] = {p2, n12, glm::vec4(1.0f)};
            verts[5] = {p3, n12, glm::vec4(1.0f)};
            // face 2: top, p3, p4
            verts[6] = {top, n23, glm::vec4(1.0f)};
            verts[7] = {p3, n23, glm::vec4(1.0f)};
            verts[8] = {p4, n23, glm::vec4(1.0f)};
            // face 3: top, p4, p1
            verts[9] = {top, n30, glm::vec4(1.0f)};
            verts[10] = {p4, n30, glm::vec4(1.0f)};
            verts[11] = {p1, n30, glm::vec4(1.0f)};

            // 2. VAO/VBO 初期化
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);

            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         sizeof(verts),
                         verts.data(),
                         GL_STATIC_DRAW);

            // layout(location = 0) position
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                0, 3, GL_FLOAT, GL_FALSE,
                sizeof(VertexPNC), (void *)offsetof(VertexPNC, pos));

            // layout(location = 1) normal
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1, 3, GL_FLOAT, GL_FALSE,
                sizeof(VertexPNC), (void *)offsetof(VertexPNC, normal));

            // layout(location = 2) color
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2, 4, GL_FLOAT, GL_FALSE,
                sizeof(VertexPNC), (void *)offsetof(VertexPNC, color));

            glBindVertexArray(0);
        }

        // 3. 描画ループ：各グリッド位置にインスタンス配置
        glUseProgram(programBasic_);
        glBindVertexArray(vao);

        const float kScale = 0.03f; // 元コードの「k」に相当

        for (int i = -1; i <= 1; ++i)
        {
            for (int j = -1; j <= 1; ++j)
            {

                // 色を決める（元コードと同じルール）
                glm::vec4 color;
                if (i == 1 && j == 0)
                    color = glm::vec4(1, 0, 0, 1);
                else if (i == 0 && j == 1)
                    color = glm::vec4(0, 0, 1, 1);
                else
                    color = glm::vec4(1, 1, 0, 1);

                // モデル行列：平行移動 + スケーリング
                glm::mat4 model(1.0f);
                model = glm::translate(model, glm::vec3((float)i, (float)j, 0.0f));
                model = glm::scale(model, glm::vec3(kScale));

                glm::mat4 mvp = proj_ * view_ * model;

                // uniform をシェーダに送る
                glUniformMatrix4fv(uModel_, 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix4fv(uMVP_, 1, GL_FALSE, glm::value_ptr(mvp));
                glUniform4fv(uColor_, 1, glm::value_ptr(color));

                // 描画（4面×3頂点 = 12頂点）
                glDrawArrays(GL_TRIANGLES, 0, 12);
            }
        }

        glBindVertexArray(0);
        glUseProgram(0);
    }

    void DrawstuffApp::drawUnitSphere()
    {
        // 原点中心，半径1の球（単位球）を描画
        // icosahedron data for an icosahedron of radius 1.0
#define ICX 0.525731112119133606f
#define ICZ 0.850650808352039932f
        static GLfloat idata[12][3] = {
            {-ICX, 0, ICZ},
            {ICX, 0, ICZ},
            {-ICX, 0, -ICZ},
            {ICX, 0, -ICZ},
            {0, ICZ, ICX},
            {0, ICZ, -ICX},
            {0, -ICZ, ICX},
            {0, -ICZ, -ICX},
            {ICZ, ICX, 0},
            {-ICZ, ICX, 0},
            {ICZ, -ICX, 0},
            {-ICZ, -ICX, 0}};

        static int index[20][3] = {
            {0, 4, 1},
            {0, 9, 4},
            {9, 5, 4},
            {4, 5, 8},
            {4, 8, 1},
            {8, 10, 1},
            {8, 3, 10},
            {5, 3, 8},
            {5, 2, 3},
            {2, 7, 3},
            {7, 10, 3},
            {7, 6, 10},
            {7, 11, 6},
            {11, 0, 6},
            {0, 1, 6},
            {6, 1, 10},
            {9, 0, 11},
            {9, 11, 2},
            {9, 2, 5},
            {7, 2, 11},
        };

        static GLuint listnum = 0;
        if (listnum == 0)
        {
            listnum = glGenLists(1);
            glNewList(listnum, GL_COMPILE);
            glBegin(GL_TRIANGLES);
            for (int i = 0; i < 20; i++)
            {
                drawPatch(&idata[index[i][2]][0], &idata[index[i][1]][0],
                          &idata[index[i][0]][0], sphere_quality);
            }
            glEnd();
            glEndList();
        }
        glCallList(listnum);
    }

    void DrawstuffApp::startGraphics(const int width, const int height, const dsFunctions *fn)
    {
        const char *prefix = DEFAULT_PATH_TO_TEXTURES;
        if (fn->version >= 2 && fn->path_to_textures)
            prefix = fn->path_to_textures;
        char *s = (char *)alloca(strlen(prefix) + 20);

        // GL 初期化
        gladLoadGL();

        initBasicProgram();

        // ground メッシュがまだなら初期化
        initGroundProgram();
        initGroundMesh();

        std::string skypath = std::string(prefix) + "/sky.ppm";
        texture[DS_SKY] = std::make_unique<Texture>(skypath.c_str());
        sky_texture = texture[DS_SKY].get();
        
        std::string groundpath = std::string(prefix) + "/ground.ppm";
        texture[DS_GROUND] = std::make_unique<Texture>(groundpath.c_str());
        ground_texture = texture[DS_GROUND].get();

        std::string woodpath = std::string(prefix) + "/wood.ppm";
        texture[DS_WOOD] = std::make_unique<Texture>(woodpath.c_str());
        wood_texture = texture[DS_WOOD].get();

        std::string checkeredpath = std::string(prefix) + "/checkered.ppm";
        texture[DS_CHECKERED] = std::make_unique<Texture>(checkeredpath.c_str());
        checkered_texture = texture[DS_CHECKERED].get();
    }

    void DrawstuffApp::stopGraphics()
    {
        for (int i = 0; i < DS_NUMTEXTURES; i++)
        {
            texture[i].reset();
        }
    }

    void DrawstuffApp::drawFrame(const int width, const int height,
                                 const dsFunctions *fn, const int pause)
    {
        if (current_state == SIM_STATE_NOT_STARTED)
            internalError("internal error");
        current_state = SIM_STATE_DRAWING;

        // ---- GL の基本状態設定（ここはとりあえずそのまま） ----
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_GEN_S);
        glDisable(GL_TEXTURE_GEN_T);
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        // ---- ビューポート ----
        glViewport(0, 0, width, height);

        // ---- 投影行列: glFrustum → glm::frustum ----
        const float vnear = 0.1f;
        const float vfar = 100.0f;
        const float k = 0.8f; // 1=±45°

        float left, right, bottom, top;
        if (width >= height)
        {
            const float k2 = (height > 0) ? float(height) / float(width) : 1.0f;
            left = -vnear * k;
            right = vnear * k;
            bottom = -vnear * k * k2;
            top = vnear * k * k2;
        }
        else
        {
            const float k2 = (height > 0) ? float(width) / float(height) : 1.0f;
            left = -vnear * k * k2;
            right = vnear * k * k2;
            bottom = -vnear * k;
            top = vnear * k;
        }

        // GLM で投影行列を生成し、メンバに保持
        proj_ = glm::frustum(left, right, bottom, top, vnear, vfar);

        // まだ固定機能を使っているあいだは、互換のために glLoadMatrixf する
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(glm::value_ptr(proj_));

        // ---- ライト設定（従来どおり PROJECTION モードで） ----
        static GLfloat light_ambient[] = {0.5f, 0.5f, 0.5f, 1.0f};
        static GLfloat light_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
        static GLfloat light_specular[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
        glColor3f(1.0f, 1.0f, 1.0f);

        // ---- 画面クリア ----
        glClearColor(0.5f, 0.5f, 0.5f, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- カメラパラメータのスナップショット ----
        float view2_xyz[3];
        float view2_hpr[3];
        memcpy(view2_xyz, view_xyz.data(), sizeof(float) * 3);
        memcpy(view2_hpr, view_hpr.data(), sizeof(float) * 3);

        // ---- MODELVIEW & カメラ設定 ----
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        setCamera(view2_xyz[0], view2_xyz[1], view2_xyz[2],
                  view2_hpr[0], view2_hpr[1], view2_hpr[2]);

        // ここで setCamera 側も GLM で view_ を更新しておくと、
        // Core Profile 移行が楽になります（前メッセージの案）。

        // ---- ライト位置（従来どおり MODELVIEW で）----
        static GLfloat light_position[] = {LIGHTX, LIGHTY, 1.0f, 0.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, light_position);

        // ---- 背景（空・地面など）----
        drawSky(view2_xyz);
        drawGround();

        // ---- 地面のマーカー ----
        drawPyramidGrid();

        // ---- OpenGL 状態を整えてからユーザ描画へ ----
        glEnable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glColor3f(1, 1, 1);
        setColor(1, 1, 1, 1);

        current_color[0] = 1;
        current_color[1] = 1;
        current_color[2] = 1;
        current_color[3] = 1;
        texture_id = 0;

        if (fn->step)
            fn->step(pause);
    }

} // namespace ds_internal
