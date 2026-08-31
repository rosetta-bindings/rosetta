// Copyright (c) fmaerten@gmail.com
// License: MIT

// A small "existing library" for the dynamic object-model example.
//
// Stock C++ throughout: no rosetta include, no `[[= rosetta::... ]]`. Every
// doc / range / combobox / label / widget hint lives out of line in
// Mesh.ann.json and Vec3.ann.json, wired in by the manifest's "annotations"
// field. So this header is exactly what an unmodified third-party library
// looks like — which is the point of the exercise.
//
// Mesh carries real geometry (positions + triangles) so the Qt viewer in qt/
// has something to draw. Note that the storage is PRIVATE: reflection only
// reaches the public surface, so the viewer gets the geometry by *calling*
// positions() / triangles() dynamically, and the property panel never shows a
// 600-element array.

#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "bunny.h" // constexpr vertex/index tables for Mesh::bunny()

namespace scene {

    enum class Shading { Flat = 0, Smooth = 1, Wireframe = 2 };

    struct Vec3 {
        double x = 0;
        double y = 0;
        double z = 0;

        Vec3() = default;
        Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

        double norm() const { return std::sqrt(x * x + y * y + z * z); }
        void   scale(double k) {
            x *= k;
            y *= k;
            z *= k;
        }
    };

    class Mesh {
    public:
        Mesh() = default;
        explicit Mesh(std::string n) : name(std::move(n)) {}

        // ---- state a UI edits -------------------------------------------
        std::string name    = "untitled";
        bool        visible = true;
        std::string colour  = "#4488ee"; // colour picker, out of line
        double      opacity = 1.0;       // slider [0,1]
        double      size    = 1.0;       // slider [0.1,5]
        double      spin    = 0.0;       // slider [0,360], degrees about Y
        Shading     shading = Shading::Smooth;
        std::string preset  = "default"; // combobox, out of line
        std::string id      = "m0";      // readonly, out of line
        Vec3        origin;
        std::vector<double> weights{1.0, 2.0, 4.0};

        // ---- geometry, read by the viewer through dynamic invocation ----
        std::vector<double> positions() const { return v_; }
        std::vector<int>    triangles() const { return t_; }
        int vertexCount() const { return static_cast<int>(v_.size() / 3); }
        int triangleCount() const { return static_cast<int>(t_.size() / 3); }

        // ---- an overload set --------------------------------------------
        // Every name-keyed backend (node, wasm, C#, Java, REST, lua) binds only
        // the first of these and drops the rest; the dynamic model keeps both
        // and picks at call time.
        double at(int i) const { return weights.empty() ? 0.0 : weights[i % weights.size()]; }
        double at(int i, int j) const { return at(i) * at(j); }

        // A reference return: crosses without a copy, and the handle must pin
        // this Mesh so the sub-object cannot outlive its parent.
        Vec3 &originRef() { return origin; }

        // Takes another bound class, by const reference.
        void translate(const Vec3 &d) {
            origin.x += d.x;
            origin.y += d.y;
            origin.z += d.z;
        }

        std::string describe() const {
            return name + " (" + std::to_string(vertexCount()) + " verts, " +
                   std::to_string(triangleCount()) + " tris)";
        }

        void reset() {
            origin = Vec3{};
            size   = 1.0;
            spin   = 0.0;
        }

        // Splits every triangle into four. Nothing about the silhouette
        // changes — switch `shading` to Wireframe and you see it happen.
        void subdivide() {
            std::vector<double> nv = v_;
            std::vector<int>    nt;
            nt.reserve(t_.size() * 4);
            auto midpoint = [&](int a, int b) {
                const double mx = 0.5 * (v_[3 * a] + v_[3 * b]);
                const double my = 0.5 * (v_[3 * a + 1] + v_[3 * b + 1]);
                const double mz = 0.5 * (v_[3 * a + 2] + v_[3 * b + 2]);
                nv.push_back(mx);
                nv.push_back(my);
                nv.push_back(mz);
                return static_cast<int>(nv.size() / 3) - 1;
            };
            for (std::size_t i = 0; i + 2 < t_.size(); i += 3) {
                const int a = t_[i], b = t_[i + 1], c = t_[i + 2];
                const int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                for (int idx : {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca}) {
                    nt.push_back(idx);
                }
            }
            v_ = std::move(nv);
            t_ = std::move(nt);
        }

        // ---- factories ---------------------------------------------------
        static Mesh cube() {
            Mesh m("cube");
            m.v_ = {-.5, -.5, -.5, .5, -.5, -.5, .5, .5, -.5, -.5, .5, -.5,
                    -.5, -.5, .5,  .5, -.5, .5,  .5, .5, .5,  -.5, .5, .5};
            m.t_ = {0, 2, 1, 0, 3, 2,  // back
                    4, 5, 6, 4, 6, 7,  // front
                    0, 1, 5, 0, 5, 4,  // bottom
                    3, 7, 6, 3, 6, 2,  // top
                    0, 4, 7, 0, 7, 3,  // left
                    1, 2, 6, 1, 6, 5}; // right
            return m;
        }

        static Mesh plane() {
            Mesh m("plane");
            m.v_ = {-.5, 0, -.5, .5, 0, -.5, .5, 0, .5, -.5, 0, .5};
            m.t_ = {0, 2, 1, 0, 3, 2};
            return m;
        }

        static Mesh sphere(int rings, int segments) {
            Mesh m("sphere");
            rings    = rings < 2 ? 2 : rings;
            segments = segments < 3 ? 3 : segments;
            const double pi = 3.14159265358979323846;
            for (int r = 0; r <= rings; ++r) {
                const double phi = pi * r / rings;
                for (int s = 0; s <= segments; ++s) {
                    const double th = 2 * pi * s / segments;
                    m.v_.push_back(0.5 * std::sin(phi) * std::cos(th));
                    m.v_.push_back(0.5 * std::cos(phi));
                    m.v_.push_back(0.5 * std::sin(phi) * std::sin(th));
                }
            }
            for (int r = 0; r < rings; ++r) {
                for (int s = 0; s < segments; ++s) {
                    const int a = r * (segments + 1) + s;
                    const int b = a + segments + 1;
                    for (int idx : {a, b, a + 1, a + 1, b, b + 1}) {
                        m.t_.push_back(idx);
                    }
                }
            }
            return m;
        }

        /**
         * @brief The Stanford bunny, from the constexpr tables in bunny.h.
         *
         * Nothing about this factory is special: it is a static method
         * returning a Mesh, so the dynamic model picks it up like cube() or
         * sphere(), and every host gains it for free — the console can
         * `static b scene::Mesh bunny`, and the Qt viewer's Add menu grows an
         * entry, because that menu is built by scanning the registry for
         * static factories returning a drawable class. No UI code changed.
         *
         * The source data is a scan in metres (~0.15 tall, sitting off-origin),
         * so it is recentred and scaled into a unit box here — otherwise it
         * would be an invisible speck next to the unit cube.
         */
        static Mesh bunny() {
            Mesh m("bunny");
            m.colour = "#c9c0b0";

            constexpr std::size_t nv = bunny::vertexCount;
            double lo[3] = {std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max()};
            double hi[3] = {std::numeric_limits<double>::lowest(),
                            std::numeric_limits<double>::lowest(),
                            std::numeric_limits<double>::lowest()};
            for (std::size_t i = 0; i < nv; ++i) {
                for (int c = 0; c < 3; ++c) {
                    const double v = bunny::positions[3 * i + c];
                    lo[c]          = std::min(lo[c], v);
                    hi[c]          = std::max(hi[c], v);
                }
            }
            const double span =
                std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1e-12});

            m.v_.reserve(nv * 3);
            for (std::size_t i = 0; i < nv; ++i) {
                for (int c = 0; c < 3; ++c) {
                    m.v_.push_back((bunny::positions[3 * i + c] - 0.5 * (lo[c] + hi[c])) / span);
                }
            }
            m.t_.reserve(bunny::indexCount);
            for (std::size_t i = 0; i < bunny::indexCount; ++i) {
                m.t_.push_back(static_cast<int>(bunny::indices[i]));
            }
            return m;
        }

        // NOT marshalable by the dynamic model: a std::function parameter has no
        // canonical Any representation yet. It still appears in the metadata,
        // with the reason — it does not silently disappear.
        void onProgress(const std::function<void(double)> &cb) { cb(1.0); }

    private:
        std::vector<double> v_; // flat xyz, object space
        std::vector<int>    t_; // index triples
    };

} // namespace scene
