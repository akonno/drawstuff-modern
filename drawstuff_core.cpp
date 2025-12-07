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

namespace ds_internal
{
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
    void skipWhiteSpace(char *filename, FILE *f)
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
    int readNumber(char *filename, FILE *f)
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
        Image(char *filename)
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
        Texture(char *filename)
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

    std::array<Texture *, 4 + 1> texture; // +1 since index 0 is not used
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
        if (use_textures)
        {
            glEnable(GL_TEXTURE_2D);
            sky_texture->bind(0);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
            glColor3f(0, 0.5, 1.0);
        }

        // make sure sky depth is as far back as possible
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthRange(1, 1);

        const float ssize = 1000.0f;
        static float offset = 0.0f;

        float x = ssize * sky_scale;
        float z = view_xyz[2] + sky_height;

        glBegin(GL_QUADS);
        glNormal3f(0, 0, -1);
        glTexCoord2f(-x + offset, -x + offset);
        glVertex3f(-ssize + view_xyz[0], -ssize + view_xyz[1], z);
        glTexCoord2f(-x + offset, x + offset);
        glVertex3f(-ssize + view_xyz[0], ssize + view_xyz[1], z);
        glTexCoord2f(x + offset, x + offset);
        glVertex3f(ssize + view_xyz[0], ssize + view_xyz[1], z);
        glTexCoord2f(x + offset, -x + offset);
        glVertex3f(ssize + view_xyz[0], -ssize + view_xyz[1], z);
        glEnd();

        offset = offset + 0.002f;
        if (offset > 1)
            offset -= 1;

        glDepthFunc(GL_LESS);
        glDepthRange(0, 1);
    }

    void DrawstuffApp::drawGround()
    {
        glDisable(GL_LIGHTING);
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        // glDepthRange (1,1);

        if (use_textures)
        {
            glEnable(GL_TEXTURE_2D);
            ground_texture->bind(0);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
            glColor3f(GROUND_R, GROUND_G, GROUND_B);
        }

        // ground fog seems to cause problems with TNT2 under windows
        /*
        GLfloat fogColor[4] = {0.5, 0.5, 0.5, 1};
        glEnable (GL_FOG);
        glFogi (GL_FOG_MODE, GL_EXP2);
        glFogfv (GL_FOG_COLOR, fogColor);
        glFogf (GL_FOG_DENSITY, 0.05f);
        glHint (GL_FOG_HINT, GL_NICEST); // GL_DONT_CARE);
        glFogf (GL_FOG_START, 1.0);
        glFogf (GL_FOG_END, 5.0);
        */

        const float gsize = 100.0f;
        const float offset = 0; // -0.001f; ... polygon offsetting doesn't work well

        glBegin(GL_QUADS);
        glNormal3f(0, 0, 1);
        glTexCoord2f(-gsize * ground_scale + ground_ofsx,
                     -gsize * ground_scale + ground_ofsy);
        glVertex3f(-gsize, -gsize, offset);
        glTexCoord2f(gsize * ground_scale + ground_ofsx,
                     -gsize * ground_scale + ground_ofsy);
        glVertex3f(gsize, -gsize, offset);
        glTexCoord2f(gsize * ground_scale + ground_ofsx,
                     gsize * ground_scale + ground_ofsy);
        glVertex3f(gsize, gsize, offset);
        glTexCoord2f(-gsize * ground_scale + ground_ofsx,
                     gsize * ground_scale + ground_ofsy);
        glVertex3f(-gsize, gsize, offset);
        glEnd();

        glDisable(GL_FOG);
    }

    void DrawstuffApp::drawPyramidGrid()
    {
        // setup stuff
        glEnable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // draw the pyramid grid
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

        strcpy(s, prefix);
        strcat(s, "/sky.ppm");
        texture[DS_SKY] = sky_texture = new Texture(s);

        strcpy(s, prefix);
        strcat(s, "/ground.ppm");
        texture[DS_GROUND] = ground_texture = new Texture(s);

        strcpy(s, prefix);
        strcat(s, "/wood.ppm");
        texture[DS_WOOD] = wood_texture = new Texture(s);

        strcpy(s, prefix);
        strcat(s, "/checkered.ppm");
        texture[DS_CHECKERED] = checkered_texture = new Texture(s);
    }

    void DrawstuffApp::stopGraphics()
    {
        delete sky_texture;
        delete ground_texture;
        delete wood_texture;
        sky_texture = 0;
        ground_texture = 0;
        wood_texture = 0;
    }

    void DrawstuffApp::drawFrame(const int width, const int height, const dsFunctions *fn, const int pause)
    {
        if (current_state == SIM_STATE_NOT_STARTED)
            internalError("internal error");
        current_state = SIM_STATE_DRAWING;

        // setup stuff
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

        // setup viewport
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const float vnear = 0.1f;
        const float vfar = 100.0f;
        const float k = 0.8f; // view scale, 1 = +/- 45 degrees
        if (width >= height)
        {
            float k2 = float(height) / float(width);
            glFrustum(-vnear * k, vnear * k, -vnear * k * k2, vnear * k * k2, vnear, vfar);
        }
        else
        {
            float k2 = float(width) / float(height);
            glFrustum(-vnear * k * k2, vnear * k * k2, -vnear * k, vnear * k, vnear, vfar);
        }

        // setup lights. it makes a difference whether this is done in the
        // GL_PROJECTION matrix mode (lights are scene relative) or the
        // GL_MODELVIEW matrix mode (lights are camera relative, bad!).
        static GLfloat light_ambient[] = {0.5, 0.5, 0.5, 1.0};
        static GLfloat light_diffuse[] = {1.0, 1.0, 1.0, 1.0};
        static GLfloat light_specular[] = {1.0, 1.0, 1.0, 1.0};
        glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
        glColor3f(1.0, 1.0, 1.0);

        // clear the window
        glClearColor(0.5, 0.5, 0.5, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // snapshot camera position (in MS Windows it is changed by the GUI thread)
        float view2_xyz[3];
        float view2_hpr[3];

        memcpy(view2_xyz, view_xyz.data(), sizeof(float) * 3);
        memcpy(view2_hpr, view_hpr.data(), sizeof(float) * 3);

        // go to GL_MODELVIEW matrix mode and set the camera
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        setCamera(view2_xyz[0], view2_xyz[1], view2_xyz[2],
                  view2_hpr[0], view2_hpr[1], view2_hpr[2]);

        // set the light position (for some reason we have to do this in model view.
        static GLfloat light_position[] = {LIGHTX, LIGHTY, 1.0, 0.0};
        glLightfv(GL_LIGHT0, GL_POSITION, light_position);

        // draw the background (ground, sky etc)
        drawSky(view2_xyz);
        drawGround();

        // draw the little markers on the ground
        drawPyramidGrid();

        // leave openGL in a known state - flat shaded white, no textures
        glEnable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glShadeModel(GL_FLAT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glColor3f(1, 1, 1);
        setColor(1, 1, 1, 1);

        // draw the rest of the objects. set drawing state first.
        current_color[0] = 1;
        current_color[1] = 1;
        current_color[2] = 1;
        current_color[3] = 1;
        texture_id = 0;
        if (fn->step)
            fn->step(pause);
    }

} // namespace ds_internal
