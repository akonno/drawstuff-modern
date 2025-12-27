#pragma once
// hud_fps.hpp - instanced 7-seg FPS HUD for OpenGL 3.3 core
// No external dependencies (optionally uses <cstdio> for snprintf).
// Designed for integration with drawstuff-modern demo.
// Copyright (c) 2025 Akihisa Konno
// Released under the BSD 3-Clause License.

#include <vector>
#include <chrono>
#include <cstdint>
#include <cstdio>   // snprintf

#include <glad/glad.h>

namespace fps_hud
{
class FpsHud
{
public:
    // Place HUD in top-left by default (pixel coords from top-left)
    struct Style
    {
        int   margin_px = 10;
        int   digit_w_px = 14;
        int   digit_h_px = 24;
        int   seg_thick_px = 3;
        int   digit_gap_px = 6;
        float color_rgba[4] = {1.0f, 1.0f, 1.0f, 0.95f};
        float bg_rgba[4]    = {0.0f, 0.0f, 0.0f, 0.35f}; // optional background plate
        bool  draw_bg = true;
    };

    void init()
    {
        if (inited_) return;

        // --- Minimal instanced quad pipeline:
        // No VBO for base quad: use gl_VertexID to generate 4 corners (triangle strip)
        // Instance VBO: rect(x,y,w,h) in NDC + color RGBA
        const char* vs = R"GLSL(
            #version 330 core
            layout(location=0) in vec4 iRect;   // x,y,w,h in NDC
            layout(location=1) in vec4 iColor;

            out vec4 vColor;

            void main()
            {
                // Triangle strip with 4 vertices: (0,0)(1,0)(0,1)(1,1)
                vec2 uv;
                if      (gl_VertexID == 0) uv = vec2(0.0, 0.0);
                else if (gl_VertexID == 1) uv = vec2(1.0, 0.0);
                else if (gl_VertexID == 2) uv = vec2(0.0, 1.0);
                else                       uv = vec2(1.0, 1.0);

                vec2 pos = iRect.xy + uv * iRect.zw;
                gl_Position = vec4(pos, 0.0, 1.0);
                vColor = iColor;
            }
        )GLSL";

        const char* fs = R"GLSL(
            #version 330 core
            in vec4 vColor;
            out vec4 FragColor;
            void main()
            {
                FragColor = vColor;
            }
        )GLSL";

        prog_ = linkProgram_(compileShader_(GL_VERTEX_SHADER, vs),
                             compileShader_(GL_FRAGMENT_SHADER, fs));

        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);

        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        // locations: 0=iRect, 1=iColor
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Instance_), (void*)offsetof(Instance_, rect));
        glVertexAttribDivisor(0, 1);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Instance_), (void*)offsetof(Instance_, color));
        glVertexAttribDivisor(1, 1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // fps timer init
        last_fps_update_ = clock::now();
        last_tick_ = clock::now();

        inited_ = true;
    }

    void shutdown()
    {
        if (!inited_) return;
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (prog_) glDeleteProgram(prog_);
        vbo_ = vao_ = prog_ = 0;
        inited_ = false;
    }

    // Call once per rendered frame
    void tick()
    {
        const auto now = clock::now();
        frame_count_++;

        const auto dt = std::chrono::duration<double>(now - last_fps_update_).count();
        if (dt >= 0.5) // update FPS twice a second
        {
            fps_ = frame_count_ / dt;
            frame_count_ = 0;
            last_fps_update_ = now;
        }
        last_tick_ = now;
    }

    // Call during draw (after 3D), passing current framebuffer size
    void render(int fb_width, int fb_height, const Style& style)
    {
        if (!inited_ || fb_width <= 0 || fb_height <= 0) return;

        // Build instances
        instances_.clear();
        instances_.reserve(256);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "FPS %5.1f", fps_);

        // Optional background plate (compute rough width)
        const int len = (int)std::strlen(buf);
        const int plate_w = style.margin_px * 0 + len * (style.digit_w_px + style.digit_gap_px); // rough
        const int plate_h = style.digit_h_px + style.seg_thick_px * 2;

        int x = style.margin_px;
        int y = style.margin_px;

        if (style.draw_bg)
        {
            addRectPx_(fb_width, fb_height,
                       x - 6, y - 6, plate_w + 12, plate_h + 12,
                       style.bg_rgba);
        }

        // Draw each character using 7-seg-ish glyphs
        for (int i = 0; buf[i] != '\0'; ++i)
        {
            char c = buf[i];
            if (c == ' ')
            {
                x += style.digit_w_px + style.digit_gap_px;
                continue;
            }

            if (c >= '0' && c <= '9')
            {
                drawDigit7seg_(fb_width, fb_height, x, y,
                               (c - '0'), style);
                x += style.digit_w_px + style.digit_gap_px;
            }
            else if (c == '.')
            {
                // dot
                const int d = style.seg_thick_px;
                addRectPx_(fb_width, fb_height,
                           x + style.digit_w_px - d, y + style.digit_h_px - d,
                           d, d, style.color_rgba);
                x += style.digit_w_px/2 + style.digit_gap_px;
            }
            else
            {
                // Minimal letters: F,P,S (7-seg approximation)
                drawLetter7seg_(fb_width, fb_height, x, y, c, style);
                x += style.digit_w_px + style.digit_gap_px;
            }
        }

        // Upload & draw
        glUseProgram(prog_);
        glBindVertexArray(vao_);

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(instances_.size() * sizeof(Instance_)),
                     instances_.data(),
                     GL_STREAM_DRAW);

        // Overlay state
        GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull_face  = glIsEnabled(GL_CULL_FACE);
        GLboolean blend      = glIsEnabled(GL_BLEND);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // 4 verts triangle strip, instanced
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)instances_.size());

        // Restore (lightweight)
        if (depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cull_face)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
        if (blend)      glEnable(GL_BLEND);      else glDisable(GL_BLEND);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void render(int fb_width, int fb_height)
    {
        Style s;
        render(fb_width, fb_height, s);
    }

private:
    using clock = std::chrono::steady_clock;

    struct Instance_
    {
        float rect[4];  // x,y,w,h in NDC
        float color[4];
    };

    // Segment bits: 0..6 = A,B,C,D,E,F,G
    //   A: top, B: upper-right, C: lower-right, D: bottom,
    //   E: lower-left, F: upper-left, G: middle
    static constexpr uint8_t DIGIT_MASKS_[10] = {
        0b0111111, // 0: A B C D E F
        0b0000110, // 1: B C
        0b1011011, // 2: A B D E G
        0b1001111, // 3: A B C D G
        0b1100110, // 4: B C F G
        0b1101101, // 5: A C D F G
        0b1111101, // 6: A C D E F G
        0b0000111, // 7: A B C
        0b1111111, // 8: A B C D E F G
        0b1101111  // 9: A B C D F G
    };

    void addRectPx_(int fbw, int fbh, int x_px, int y_px, int w_px, int h_px, const float rgba[4])
    {
        // Convert from top-left pixel coords to NDC rect (x right, y down)
        // NDC: x [-1,1], y [-1,1] (y up). So y needs flip.
        const float x0 =  (2.0f * float(x_px) / float(fbw)) - 1.0f;
        const float x1 =  (2.0f * float(x_px + w_px) / float(fbw)) - 1.0f;

        const float y0 =  1.0f - (2.0f * float(y_px) / float(fbh));
        const float y1 =  1.0f - (2.0f * float(y_px + h_px) / float(fbh));

        Instance_ inst{};
        inst.rect[0] = x0;
        inst.rect[1] = y1;          // bottom-left in NDC
        inst.rect[2] = (x1 - x0);    // w
        inst.rect[3] = (y0 - y1);    // h (positive)
        inst.color[0] = rgba[0];
        inst.color[1] = rgba[1];
        inst.color[2] = rgba[2];
        inst.color[3] = rgba[3];
        instances_.push_back(inst);
    }

    void drawDigit7seg_(int fbw, int fbh, int x, int y, int digit, const Style& s)
    {
        if (digit < 0 || digit > 9) return;
        const uint8_t m = DIGIT_MASKS_[digit];

        const int W = s.digit_w_px;
        const int H = s.digit_h_px;
        const int t = s.seg_thick_px;

        // Segment rects in pixel coords (top-left origin)
        // A
        if (m & (1<<0)) addRectPx_(fbw, fbh, x + t,     y + 0,      W - 2*t, t, s.color_rgba);
        // B
        if (m & (1<<1)) addRectPx_(fbw, fbh, x + W - t, y + t,      t, (H/2) - t, s.color_rgba);
        // C
        if (m & (1<<2)) addRectPx_(fbw, fbh, x + W - t, y + H/2,    t, (H/2) - t, s.color_rgba);
        // D
        if (m & (1<<3)) addRectPx_(fbw, fbh, x + t,     y + H - t,  W - 2*t, t, s.color_rgba);
        // E
        if (m & (1<<4)) addRectPx_(fbw, fbh, x + 0,     y + H/2,    t, (H/2) - t, s.color_rgba);
        // F
        if (m & (1<<5)) addRectPx_(fbw, fbh, x + 0,     y + t,      t, (H/2) - t, s.color_rgba);
        // G
        if (m & (1<<6)) addRectPx_(fbw, fbh, x + t,     y + (H/2) - (t/2), W - 2*t, t, s.color_rgba);
    }

    void drawLetter7seg_(int fbw, int fbh, int x, int y, char c, const Style& s)
    {
        // crude 7-seg approximations for FPS
        // F: A + F + G + E
        // P: A + B + F + G + E (looks like P)
        // S: same as 5
        if (c == 'F' || c == 'f')
        {
            // segments: A,F,G,E
            const int W = s.digit_w_px, H = s.digit_h_px, t = s.seg_thick_px;
            addRectPx_(fbw, fbh, x + t, y + 0,     W - 2*t, t, s.color_rgba);                  // A
            addRectPx_(fbw, fbh, x + 0, y + t,     t, (H/2) - t, s.color_rgba);                // F
            addRectPx_(fbw, fbh, x + t, y + (H/2) - (t/2), W - 2*t, t, s.color_rgba);         // G
            addRectPx_(fbw, fbh, x + 0, y + H/2,   t, (H/2) - t, s.color_rgba);                // E
        }
        else if (c == 'P' || c == 'p')
        {
            const int W = s.digit_w_px, H = s.digit_h_px, t = s.seg_thick_px;
            addRectPx_(fbw, fbh, x + t,     y + 0,     W - 2*t, t, s.color_rgba);              // A
            addRectPx_(fbw, fbh, x + W - t, y + t,     t, (H/2) - t, s.color_rgba);            // B
            addRectPx_(fbw, fbh, x + 0,     y + t,     t, (H/2) - t, s.color_rgba);            // F
            addRectPx_(fbw, fbh, x + t,     y + (H/2) - (t/2), W - 2*t, t, s.color_rgba);     // G
            addRectPx_(fbw, fbh, x + 0,     y + H/2,   t, (H/2) - t, s.color_rgba);            // E
        }
        else if (c == 'S' || c == 's')
        {
            drawDigit7seg_(fbw, fbh, x, y, 5, s);
        }
        else
        {
            // unknown: draw a small block
            addRectPx_(fbw, fbh, x + 2, y + 2, s.digit_w_px - 4, s.digit_h_px - 4, s.color_rgba);
        }
    }

    static GLuint compileShader_(GLenum type, const char* src)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);

        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[2048];
            GLsizei n = 0;
            glGetShaderInfoLog(s, (GLsizei)sizeof(log), &n, log);
            // Don't throw; keep minimal. You can integrate with ds_internal::fatalError if desired.
            // fprintf(stderr, "HUD shader compile failed: %s\n", log);
        }
        return s;
    }

    static GLuint linkProgram_(GLuint vs, GLuint fs)
    {
        GLuint p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[2048];
            GLsizei n = 0;
            glGetProgramInfoLog(p, (GLsizei)sizeof(log), &n, log);
            // fprintf(stderr, "HUD program link failed: %s\n", log);
        }
        return p;
    }

private:
    bool inited_ = false;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint prog_ = 0;

    std::vector<Instance_> instances_;

    double fps_ = 0.0;
    int frame_count_ = 0;
    clock::time_point last_fps_update_{};
    clock::time_point last_tick_{};
};
} // namespace fps_hud

#ifdef DS_HUD_FPS_IMPLEMENTATION
// Nothing else needed: header-only implementation already included.
#endif
