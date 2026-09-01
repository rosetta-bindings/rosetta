// Copyright (c) fmaerten@gmail.com
// License: MIT

// A 3D view of objects it knows nothing about.
//
// This widget includes <rosetta/dynamic.h> and Qt. It does NOT include scene.h,
// and it names no bound type. Each frame it walks the interpreter's variable
// table and, for every object whose class satisfies the geometry protocol
// (dynui::has_geometry — `positions()` and `triangles()`, verified against the
// metadata before anything is called), it:
//
//   * invokes positions() / triangles() dynamically to get the mesh,
//   * reads whatever OPTIONAL appearance fields the class happens to have —
//     visible, colour, opacity, size, spin, origin, shading, preset — skipping
//     any that are absent,
//   * builds a triangle soup and draws it.
//
// Bind a different library with the same two method names and it renders,
// with no change here. That is what a dynamic object model buys a viewer: the
// renderer is written once, against the protocol, not once per type.
//
// It also shows where the dynamic path needs help. Pulling a mesh across the
// boundary boxes every coordinate into an Any, so the Stanford bunny (1889
// vertices / 3851 faces) costs ~7.5 ms/frame if fetched naively — all of it
// marshalling, none of it GL. The geometry cache below cuts that to ~0.07 ms by
// resolving the methods once per class and re-fetching only when a cheap stamp
// (vertexCount / triangleCount) says the mesh actually changed. Appearance is
// still read live every frame, because it is a handful of scalars and must
// respond to a slider immediately. Control dynamic, bulk data cached.

#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVector3D>
#include <QWheelEvent>

#include <cmath>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "../interp.h"

class SceneView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit SceneView(dynui::Interp *interp, QWidget *parent = nullptr)
        : QOpenGLWidget(parent), interp_(interp) {
        setMinimumSize(480, 360);
    }

    ~SceneView() override {
        makeCurrent();
        vbo_.destroy();
        vao_.destroy();
        doneCurrent();
    }

protected:
    // -----------------------------------------------------------------------
    // Reading whatever the metadata happens to offer
    // -----------------------------------------------------------------------

    static double num_field(const dynui::rd::Object &o, const char *n, double dflt) {
        if (!o.meta() || !o.meta()->field(n)) {
            return dflt;
        }
        const auto r = o.get(n);
        return r.ok() && r.value.kind() == dynui::rd::Kind::number ? r.value.as_number() : dflt;
    }

    static bool bool_field(const dynui::rd::Object &o, const char *n, bool dflt) {
        if (!o.meta() || !o.meta()->field(n)) {
            return dflt;
        }
        const auto r = o.get(n);
        return r.ok() && r.value.kind() == dynui::rd::Kind::boolean ? r.value.as_bool() : dflt;
    }

    static QString str_field(const dynui::rd::Object &o, const char *n, const QString &dflt) {
        if (!o.meta() || !o.meta()->field(n)) {
            return dflt;
        }
        const auto r = o.get(n);
        return r.ok() && r.value.kind() == dynui::rd::Kind::string
                   ? QString::fromStdString(r.value.as_string())
                   : dflt;
    }

    /** @brief An enum field, by enumerator NAME — so the view never hard-codes
     *  the integer values of an enumeration it has not seen. */
    static QString enum_field(const dynui::rd::Object &o, const char *n) {
        const dynui::rd::MetaField *f = o.meta() ? o.meta()->field(n) : nullptr;
        if (!f || f->type->kind != dynui::rd::Kind::enum_) {
            return {};
        }
        const auto r = o.get(n);
        return r.ok() ? QString::fromStdString(dynui::enumerator_name(f->type, r.value.as_int()))
                      : QString{};
    }

    /** @brief Shading parameters for a named material preset.
     *
     *  The names come from the `combobox` annotation on the field, not from
     *  here — the view is only deciding what each one should LOOK like, which
     *  is a renderer's business and not the library's. An unrecognised name
     *  falls back to the default material rather than failing, so a library
     *  offering different presets still renders.
     */
    struct Material {
        float shine   = 48.0f; // specular exponent
        float spec    = 0.14f; // specular strength
        float fresnel = 0.0f;  // rim brightening; non-zero only for glass
        float alpha   = 1.0f;  // multiplies the object's own opacity
    };

    static Material material_for(const QString &preset) {
        // The four differ enough to be told apart at a glance, which is the
        // whole point of a preset control: a highlight that only a rendering
        // engineer can see is the same as no highlight.
        if (preset == "matte") {
            return {4.0f, 0.0f, 0.0f, 1.0f}; // chalk: no highlight at all
        }
        if (preset == "glossy") {
            // A LOW exponent with a strong weight gives a broad, obvious
            // sheen; a high one puts a pinpoint somewhere off the silhouette
            // and reads as no change.
            return {24.0f, 0.65f, 0.0f, 1.0f};
        }
        if (preset == "glass") {
            return {64.0f, 0.5f, 0.30f, 0.30f}; // translucent, bright rim
        }
        return {}; // "default", and anything this view does not know
    }

    /** @brief A nested bound object read through the SAME dynamic API — the
     *  handle it returns pins its parent, so borrowing x/y/z off it is safe. */
    static QVector3D vec_field(const dynui::rd::Object &o, const char *n) {
        const dynui::rd::MetaField *f = o.meta() ? o.meta()->field(n) : nullptr;
        if (!f || f->type->kind != dynui::rd::Kind::object) {
            return {};
        }
        const auto r = o.get(n);
        if (!r.ok() || !r.value.as_object().cls) {
            return {};
        }
        const auto      &ref = r.value.as_object();
        dynui::rd::Object sub = dynui::rd::Object::borrow(*ref.cls, ref.ptr);
        return QVector3D(float(num_field(sub, "x", 0)), float(num_field(sub, "y", 0)),
                         float(num_field(sub, "z", 0)));
    }

    /** @brief Call an ALREADY-RESOLVED nullary method returning a sequence of
     *  numbers. Skipping Object::call() skips resolve(), which is a linear scan
     *  plus overload scoring — worth avoiding on the geometry path. */
    static std::vector<double> num_list_via(const dynui::rd::MetaMethod *m,
                                            const dynui::rd::Object     &o) {
        std::vector<double> out;
        if (!m || !m->invoke) {
            return out;
        }
        const dynui::rd::Any v = m->invoke(o.ref(), {});
        if (v.kind() != dynui::rd::Kind::vector) {
            return out;
        }
        out.reserve(v.as_list().size());
        for (const auto &e : v.as_list()) {
            out.push_back(e.as_number());
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // GL
    // -----------------------------------------------------------------------

    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        static const char *VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNrm;
out vec3 vN;
out vec3 vP;
void main() {
    vN = uNrm * aNrm;
    vP = vec3(uModel * vec4(aPos, 1.0));
    gl_Position = uMVP * vec4(aPos, 1.0);
})";

        static const char *FS = R"(#version 330 core
in vec3 vN;
in vec3 vP;
uniform vec3  uColour;
uniform float uAlpha;
uniform vec3  uEye;
uniform int   uFlat;
uniform float uShine;      // specular exponent  (preset)
uniform float uSpec;       // specular strength  (preset)
uniform float uFresnel;    // rim brightening    (preset: glass)
out vec4 FragColour;
void main() {
    if (uFlat == 1) {          // wireframe / grid: unlit
        FragColour = vec4(uColour, uAlpha);
        return;
    }
    vec3 N = normalize(vN);
    vec3 V = normalize(uEye - vP);
    vec3 L = normalize(vec3(0.45, 0.85, 0.55));   // fixed key light
    vec3 H = normalize(L + V);
    float key  = max(dot(N, L), 0.0);
    float fill = max(dot(N, -L), 0.0);            // soft bounce from below
    float spec = pow(max(dot(N, H), 0.0), uShine);
    vec3  c    = uColour * (0.20 + 0.70 * key + 0.15 * fill) + vec3(uSpec * spec);
    // Grazing angles go brighter and more opaque, which is what reads as
    // glass; uFresnel is 0 for the solid presets, so they are unaffected.
    // The rim is tinted with the object's own colour rather than white, or a
    // saturated object turns into a pale blob at the silhouette.
    float rim  = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    c += uColour * (uFresnel * rim);
    FragColour = vec4(c, clamp(uAlpha + uFresnel * rim, 0.0, 1.0));
})";

        prog_.addShaderFromSourceCode(QOpenGLShader::Vertex, VS);
        prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, FS);
        prog_.link();

        vao_.create();
        vbo_.create();
        vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        build_grid();
    }

    void resizeGL(int w, int h) override { glViewport(0, 0, w, h); }

    void paintGL() override {
        glClearColor(0.13f, 0.14f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const float aspect = height() ? float(width()) / float(height()) : 1.0f;
        QMatrix4x4  proj;
        proj.perspective(40.0f, aspect, 0.05f, 200.0f);

        const float cy = std::cos(qDegreesToRadians(yaw_)), sy = std::sin(qDegreesToRadians(yaw_));
        const float cp = std::cos(qDegreesToRadians(pitch_)),
                    sp = std::sin(qDegreesToRadians(pitch_));
        const QVector3D eye(dist_ * cp * sy, dist_ * sp, dist_ * cp * cy);

        QMatrix4x4 view;
        view.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 1, 0));

        prog_.bind();
        vao_.bind();
        prog_.setUniformValue("uEye", eye);

        draw_grid(proj * view);

        QElapsedTimer clock;
        clock.start();
        for (auto &[ptr, c] : cache_) {
            c.live = false;
        }
        int drawn = 0, tris = 0;
        for (const auto &[name, obj] : interp_->vars) {
            if (!obj.valid() || !obj.meta() || !dynui::has_geometry(*obj.meta())) {
                continue;
            }
            if (!bool_field(obj, "visible", true)) {
                continue;
            }
            const int t = draw_object(obj, proj * view);
            drawn += t > 0 ? 1 : 0;
            tris += t;
        }
        // Sweep: an object that vanished from the table takes its cache with
        // it, so a freed address cannot be mistaken for a live entry later.
        for (auto it = cache_.begin(); it != cache_.end();) {
            it = it->second.live ? std::next(it) : cache_.erase(it);
        }
        const double ms = double(clock.nsecsElapsed()) / 1.0e6;

        vao_.release();
        prog_.release();
        emit stats(drawn, tris, ms);
    }

signals:
    /** @brief Objects drawn, triangles drawn, and milliseconds spent doing it
     *  — including every dynamic call. A demo about invocation cost should say
     *  what that cost is. */
    void stats(int objects, int triangles, double ms);

protected:

    /** @brief One object: geometry by dynamic call, appearance by dynamic read.
     *  Returns the triangle count drawn (0 if nothing was). */
    int draw_object(const dynui::rd::Object &obj, const QMatrix4x4 &viewProj) {
        const QString shading = enum_field(obj, "shading");
        const bool    wire    = shading == "Wireframe";

        const std::vector<float> &soup = geometry(obj, shading);
        if (soup.empty()) {
            return 0;
        }

        // Appearance is re-read every frame: these are half a dozen scalar
        // calls, and they must stay live so a slider drag shows immediately.
        QMatrix4x4      model;
        const QVector3D o = vec_field(obj, "origin");
        model.translate(o);
        model.rotate(float(num_field(obj, "spin", 0)), 0, 1, 0);
        const float s = float(num_field(obj, "size", 1));
        model.scale(s <= 0 ? 1.0f : s);

        QColor col(str_field(obj, "colour", "#4488ee"));
        if (!col.isValid()) {
            col = QColor("#4488ee");
        }

        prog_.setUniformValue("uMVP", viewProj * model);
        prog_.setUniformValue("uModel", model);
        prog_.setUniformValue("uNrm", model.normalMatrix());
        prog_.setUniformValue("uColour",
                              QVector3D(float(col.redF()), float(col.greenF()),
                                        float(col.blueF())));
        // `preset` is one more OPTIONAL appearance field, read the same way as
        // colour and opacity: a class that does not declare it simply renders
        // with the default material, because str_field falls back.
        const Material mat = material_for(str_field(obj, "preset", "default"));
        prog_.setUniformValue("uAlpha", float(num_field(obj, "opacity", 1.0)) * mat.alpha);
        prog_.setUniformValue("uShine", mat.shine);
        prog_.setUniformValue("uSpec", mat.spec);
        prog_.setUniformValue("uFresnel", mat.fresnel);
        prog_.setUniformValue("uFlat", wire ? 1 : 0);

        upload(soup);
        glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(soup.size() / 6));
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        return int(soup.size() / 18);
    }

    // -----------------------------------------------------------------------
    // Geometry cache
    // -----------------------------------------------------------------------
    //
    // Pulling a mesh across the dynamic boundary means boxing every coordinate
    // into an Any: the Stanford bunny is 1889 vertices and 3851 faces, so a
    // naive redraw allocates ~17k Anys twice per frame. Measured, that is ~7.5
    // ms/frame for this scene — the whole of it marshalling, none of it GL.
    //
    // So do what <rosetta/dynamic.h> recommends for a hot path: resolve the
    // methods ONCE per class (resolve() is a linear scan plus overload scoring)
    // and cache the built vertex buffer, re-fetching only when a cheap stamp
    // says the geometry actually changed. vertexCount() / triangleCount() are
    // two scalar calls, so the steady state costs two dynamic invocations per
    // object per frame instead of two whole meshes.
    //
    // This is the honest shape of the trade-off: the dynamic path is perfectly
    // good for control (properties, commands, menus) and wants a cache in front
    // of it for bulk data.

    /** @brief The geometry protocol's methods, resolved once per class. */
    struct Protocol {
        const dynui::rd::MetaMethod *positions     = nullptr;
        const dynui::rd::MetaMethod *triangles     = nullptr;
        const dynui::rd::MetaMethod *vertexCount   = nullptr;
        const dynui::rd::MetaMethod *triangleCount = nullptr;
    };

    struct Cached {
        std::vector<float> soup;
        long long          vstamp  = -1;
        long long          tstamp  = -1;
        QString            shading;
        bool               live = false; // seen this frame (mark & sweep)
    };

    const Protocol &protocol_of(const dynui::rd::MetaClass &k) {
        auto it = proto_.find(&k);
        if (it != proto_.end()) {
            return it->second;
        }
        Protocol p;
        p.positions     = k.method("positions");
        p.triangles     = k.method("triangles");
        p.vertexCount   = k.method("vertexCount");
        p.triangleCount = k.method("triangleCount");
        return proto_.emplace(&k, p).first->second;
    }

    /** @brief A cheap scalar call through an already-resolved method, or -1. */
    static long long stamp(const dynui::rd::MetaMethod *m, const dynui::rd::Object &o) {
        if (!m || !m->invoke || m->n_params != 0) {
            return -1;
        }
        const dynui::rd::Any v = m->invoke(o.ref(), {});
        return v.kind() == dynui::rd::Kind::number ? v.as_int() : -1;
    }

    const std::vector<float> &geometry(const dynui::rd::Object &obj, const QString &shading) {
        const Protocol &p = protocol_of(*obj.meta());
        Cached         &c = cache_[obj.ptr()];
        c.live            = true;

        const long long vs = stamp(p.vertexCount, obj);
        const long long ts = stamp(p.triangleCount, obj);
        // No stamp available (a class without the counters) means rebuild every
        // frame — correct, just slow, and the status bar will show it.
        const bool fresh = !c.soup.empty() && vs >= 0 && ts >= 0 && vs == c.vstamp &&
                           ts == c.tstamp && shading == c.shading;
        if (fresh) {
            return c.soup;
        }
        c.soup    = build_soup(obj, p, shading);
        c.vstamp  = vs;
        c.tstamp  = ts;
        c.shading = shading;
        return c.soup;
    }

    /** @brief The expensive path: pull the whole mesh across the boundary. */
    std::vector<float> build_soup(const dynui::rd::Object &obj, const Protocol &p,
                                  const QString &shading) {
        std::vector<float> soup;
        if (!p.positions || !p.triangles) {
            return soup;
        }
        const std::vector<double> pos = num_list_via(p.positions, obj);
        const std::vector<double> tri = num_list_via(p.triangles, obj);
        if (pos.size() < 9 || tri.size() < 3) {
            return soup;
        }

        const bool smooth = shading == "Smooth";

        // Expand to a triangle soup with normals. Flat shading gets face
        // normals; smooth shading averages them per vertex first.
        std::vector<QVector3D> vnrm;
        if (smooth) {
            vnrm.assign(pos.size() / 3, QVector3D());
        }
        soup.reserve(tri.size() * 6);

        const auto vertex = [&](std::size_t i) {
            return QVector3D(float(pos[3 * i]), float(pos[3 * i + 1]), float(pos[3 * i + 2]));
        };
        const std::size_t nv = pos.size() / 3;

        for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
            const auto a = std::size_t(tri[t]), b = std::size_t(tri[t + 1]),
                       c = std::size_t(tri[t + 2]);
            if (a >= nv || b >= nv || c >= nv) {
                continue;
            }
            const QVector3D n =
                QVector3D::crossProduct(vertex(b) - vertex(a), vertex(c) - vertex(a));
            if (smooth) {
                vnrm[a] += n;
                vnrm[b] += n;
                vnrm[c] += n;
            }
        }

        for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
            const auto idx = {std::size_t(tri[t]), std::size_t(tri[t + 1]),
                              std::size_t(tri[t + 2])};
            const auto a = std::size_t(tri[t]), b = std::size_t(tri[t + 1]),
                       c = std::size_t(tri[t + 2]);
            if (a >= nv || b >= nv || c >= nv) {
                continue;
            }
            const QVector3D fn =
                QVector3D::crossProduct(vertex(b) - vertex(a), vertex(c) - vertex(a)).normalized();
            for (std::size_t i : idx) {
                const QVector3D v = vertex(i);
                const QVector3D n = smooth ? vnrm[i].normalized() : fn;
                soup.insert(soup.end(), {v.x(), v.y(), v.z(), n.x(), n.y(), n.z()});
            }
        }
        return soup;
    }

    // -----------------------------------------------------------------------
    // Camera
    // -----------------------------------------------------------------------

    void mousePressEvent(QMouseEvent *e) override { last_ = e->position(); }

    void mouseMoveEvent(QMouseEvent *e) override {
        const QPointF d = e->position() - last_;
        last_           = e->position();
        yaw_ -= float(d.x()) * 0.4f;
        pitch_ = qBound(-89.0f, pitch_ + float(d.y()) * 0.4f, 89.0f);
        update();
    }

    void wheelEvent(QWheelEvent *e) override {
        dist_ = qBound(0.6f, dist_ * (e->angleDelta().y() > 0 ? 0.9f : 1.1f), 80.0f);
        update();
    }

private:
    void upload(const std::vector<float> &soup) {
        vbo_.bind();
        vbo_.allocate(soup.data(), int(soup.size() * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              reinterpret_cast<void *>(3 * sizeof(float)));
    }

    void build_grid() {
        const int   n = 10;
        const float e = float(n) * 0.5f;
        for (int i = -n / 2; i <= n / 2; ++i) {
            const float p = float(i) * 0.5f;
            for (const QVector3D &v : {QVector3D(p, 0, -e * 0.5f), QVector3D(p, 0, e * 0.5f),
                                       QVector3D(-e * 0.5f, 0, p), QVector3D(e * 0.5f, 0, p)}) {
                grid_.insert(grid_.end(), {v.x(), v.y(), v.z(), 0.0f, 1.0f, 0.0f});
            }
        }
    }

    void draw_grid(const QMatrix4x4 &viewProj) {
        if (grid_.empty()) {
            return;
        }
        QMatrix4x4 identity;
        prog_.setUniformValue("uMVP", viewProj);
        prog_.setUniformValue("uModel", identity);
        prog_.setUniformValue("uNrm", identity.normalMatrix());
        prog_.setUniformValue("uColour", QVector3D(0.30f, 0.32f, 0.38f));
        prog_.setUniformValue("uAlpha", 1.0f);
        prog_.setUniformValue("uFlat", 1);
        upload(grid_);
        glDrawArrays(GL_LINES, 0, GLsizei(grid_.size() / 6));
    }

    dynui::Interp                                       *interp_ = nullptr;
    std::unordered_map<const dynui::rd::MetaClass *, Protocol> proto_;
    std::unordered_map<const void *, Cached>                   cache_;
    QOpenGLShaderProgram     prog_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer            vbo_{QOpenGLBuffer::VertexBuffer};
    std::vector<float>       grid_;
    QPointF                  last_;
    float                    yaw_ = 35.0f, pitch_ = 22.0f, dist_ = 4.0f;
    int                      drawn_ = 0;
};
