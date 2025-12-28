// ============================================================================
// drawstuff - platform-specific code for X11 with GLX
// src/platform_x11_glx.cpp
// This file is part of drawstuff-modern, a modern reimplementation inspired by
// the drawstuff library distributed with the Open Dynamics Engine (ODE).
// The original drawstuff was developed by Russell L. Smith.
// This implementation has been substantially rewritten and redesigned
// ============================================================================
// Copyright (c) 2025 Akihisa Konno
// Released under the BSD 3-Clause License.
// See the LICENSE file for details.

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <sys/time.h> // gettimeofday
#include <X11/Xatom.h> // XA_STRING, XA_WM_NAME など
#include "drawstuff_core.hpp"
#include <GL/glx.h> // GLXContext など

namespace ds_internal {
    // ==============================================================
    // X11 window management
    // ==============================================================
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
        XSync(display, false);
    }

    void DrawstuffApp::destroyMainWindow()
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
            my = event.xbutton.y;
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
                current_state = SIM_STATE_FINISHED;
                return;
            }
            return;

        case ConfigureNotify:
            width = event.xconfigure.width;
            height = event.xconfigure.height;
            return;
        }
    }

    // For capturing frames to PPM files
    static int maskShift(unsigned long mask) {
        int s = 0;
        if (!mask) return 0;
        while ((mask & 1UL) == 0UL) { mask >>= 1; ++s; }
        return s;
    }

    static int maskBits(unsigned long mask) {
        // LSB側へ寄せた後の連続1の長さを数える
        if (!mask) return 0;
        while ((mask & 1UL) == 0UL) mask >>= 1;
        int b = 0;
        while (mask & 1UL) { mask >>= 1; ++b; }
        return b;
    }

    static std::uint8_t extract8(unsigned long pixel, unsigned long mask) {
        if (!mask) return 0;
        const int s = maskShift(mask);
        const int b = maskBits(mask);
        const unsigned long v = (pixel & mask) >> s;           // 0 .. (2^b-1)
        const unsigned long denom = (b >= 32) ? 0xffffffffUL : ((1UL << b) - 1UL);
        // 0..255へスケール（丸め）
        const unsigned long v8 = (v * 255UL + denom / 2UL) / denom;
        return static_cast<std::uint8_t>(v8 & 0xffUL);
    }

    void DrawstuffApp::captureFrame(const int num)
    {
        // ログ出力（stderr 相当）
        std::cerr << "capturing frame "
                  << std::setw(4) << std::setfill('0') << num << '\n';

        // ファイル名生成
        std::ostringstream oss;
        oss << "frame/frame"
            << std::setw(4) << std::setfill('0') << num
            << ".ppm";
        const std::string filename = oss.str();

        // バイナリ出力ストリーム
        std::ofstream out(filename, std::ios::binary);
        if (!out)
        {
            fatalError("can't open \"%s\" for writing", filename.c_str());
        }

        // PPM ヘッダ
        out << "P6\n"
            << width << ' ' << height << "\n255\n";

        // X11 からフレームバッファ取得
        XImage *image = XGetImage(display, win,
                                  0, 0,
                                  static_cast<unsigned int>(width),
                                  static_cast<unsigned int>(height),
                                  ~0UL, ZPixmap);
        if (!image)
        {
            fatalError("XGetImage failed");
        }

        // 1 行分のバッファ
        std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 3);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const unsigned long pixel = XGetPixel(image, x, y);
                const std::size_t idx = static_cast<std::size_t>(x) * 3;
                row[idx + 0] = extract8(pixel, image->red_mask);
                row[idx + 1] = extract8(pixel, image->green_mask);
                row[idx + 2] = extract8(pixel, image->blue_mask);
            }

            out.write(reinterpret_cast<const char *>(row.data()),
                      static_cast<std::streamsize>(row.size()));
        }

        XDestroyImage(image);
    }

    void DrawstuffApp::microsleep(const unsigned long usecs)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(usecs));
    }

    void DrawstuffApp::processRenderFrame(int *frame, const dsFunctions *fn)
    {
        renderFrame(width, height, fn, pausemode && !singlestep);
        singlestep = 0;

        if (fn->postStep)
            fn->postStep(pausemode);
        
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

    void DrawstuffApp::platformSimulationLoop(const int window_width, const int window_height, const dsFunctions *fn,
                                              const int initial_pause)
    {
        pausemode = initial_pause;
        createMainWindow(window_width, window_height);
        glXMakeCurrent(display, win, glx_context);

        startGraphics(window_width, window_height, fn);
        
        if (fn->start)
            fn->start();

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

        timeval tv;
        gettimeofday(&tv, 0);
        double prev = tv.tv_sec + (double)tv.tv_usec / 1000000.0;

        int frame = 1;
        while (current_state == SIM_STATE_RUNNING)
        {
            // read in and process all pending events for the main window
            XEvent event;
            while (current_state == SIM_STATE_RUNNING && XPending(display))
            {
                XNextEvent(display, &event);
                handleEvent(event, fn);
            }

            gettimeofday(&tv, 0);
            double curr = tv.tv_sec + (double)tv.tv_usec / 1000000.0;
            if (curr - prev >= 1.0 / 60.0)
            {
                prev = curr;
                processRenderFrame(&frame, fn);
            }
            else
                microsleep(1000);
        };

        if (fn->stop)
            fn->stop();
        stopGraphics();

        destroyMainWindow();
    }

} // namespace ds_internal
