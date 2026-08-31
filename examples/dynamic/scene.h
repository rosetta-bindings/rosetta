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

        // Write geometry back. Relaxer drives the mesh entirely through this
        // and positions()/triangles(), i.e. through the same public surface a
        // binding sees — so nothing in the solver depends on being inside the
        // class.
        void setPositions(const std::vector<double> &p) {
            if (p.size() == v_.size()) {
                v_ = p;
            }
        }

    private:
        std::vector<double> v_; // flat xyz, object space
        std::vector<int>    t_; // index triples
    };

    /**
     * @brief Relaxes a triangle mesh toward equilateral triangles.
     *
     * The third kind of thing a library exposes, after data (Vec3) and a
     * document (Mesh): a SOLVER — an object that is mostly tuning parameters,
     * plus one method that runs for a while and changes something else. Its
     * parameters are exactly the kind of state a property sheet is for, which
     * is why it is the useful case for the dynamic projection: bind nothing,
     * and every host gets a solver control panel because the parameters are
     * public fields with ranges declared beside them.
     *
     * The scheme is tangential Laplacian relaxation. Each pass moves every
     * vertex a fraction `step` toward the centroid of its one-ring neighbours,
     * which equalises edge lengths and so drives triangles toward equilateral.
     * Moving to the raw centroid also shrinks the surface — the classic failure
     * of naive Laplacian smoothing — so with `preserveShape` the displacement
     * is projected onto the vertex tangent plane, removing the component along
     * the normal. The silhouette then stays put while the triangulation
     * improves underneath it. Measured on the bunny, 40 passes at step 0.8:
     * mean quality 0.834 -> 0.915 either way, but the bounding-box diagonal
     * loses 1.05% with the projection off and 0.01% with it on.
     *
     * Quality per triangle is the standard normalised ratio
     * `q = 4*sqrt(3)*A / (a^2 + b^2 + c^2)`, which is 1 for an equilateral
     * triangle and falls to 0 as one degenerates.
     */
    class Relaxer {
    public:
        Relaxer() = default;

        // ---- tuning: what a property sheet edits ------------------------
        int    iterations    = 10;   // slider [1, 200]
        double step          = 0.5;  // slider [0, 1], relaxation factor
        bool   pinBoundary   = true; // hold the boundary loop still
        bool   preserveShape = true; // project onto the tangent plane

        // ---- outcome: filled by run(), read-only to a UI ----------------
        double meanQuality  = 0.0;
        double worstQuality = 0.0;
        int    lastPasses   = 0;

        /** @brief Mean triangle quality of a mesh, in [0,1]. */
        static double quality(const Mesh &m) { return measure(m).first; }

        /** @brief Quality of the WORST triangle — what actually breaks solvers. */
        static double minQuality(const Mesh &m) { return measure(m).second; }

        /**
         * @brief Run `iterations` relaxation passes over `m`, in place.
         *
         * Takes the mesh by mutable reference, which the dynamic model passes
         * without copying — so a console command, a slider release and a
         * Python call all mutate the same object the 3D view is drawing.
         */
        void run(Mesh &m) {
            std::vector<double> p = m.positions();
            const std::vector<int> t = m.triangles();
            const std::size_t      n = p.size() / 3;
            if (n == 0 || t.empty()) {
                lastPasses = 0;
                return;
            }

            // One-ring adjacency, and which vertices sit on a boundary edge.
            // An edge belongs to the boundary when exactly one triangle uses
            // it; those vertices are the ones that must not move, or the mesh
            // erodes from its rim inward.
            std::vector<std::vector<int>> ring(n);
            std::vector<int>              edgeUse;
            std::vector<std::pair<int, int>> edges;
            const auto edgeKey = [&](int a, int b) {
                return std::pair<int, int>{std::min(a, b), std::max(a, b)};
            };
            std::vector<std::pair<std::pair<int, int>, int>> tally;
            for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
                const int v[3] = {t[i], t[i + 1], t[i + 2]};
                for (int e = 0; e < 3; ++e) {
                    const int a = v[e], b = v[(e + 1) % 3];
                    if (a < 0 || b < 0 || a >= static_cast<int>(n) ||
                        b >= static_cast<int>(n)) {
                        continue;
                    }
                    ring[a].push_back(b);
                    ring[b].push_back(a);
                    tally.emplace_back(edgeKey(a, b), 1);
                }
            }
            std::sort(tally.begin(), tally.end());
            std::vector<bool> onBoundary(n, false);
            for (std::size_t i = 0; i < tally.size();) {
                std::size_t j = i;
                while (j < tally.size() && tally[j].first == tally[i].first) {
                    ++j;
                }
                if (j - i == 1) { // used once => boundary
                    onBoundary[tally[i].first.first]  = true;
                    onBoundary[tally[i].first.second] = true;
                }
                i = j;
            }
            for (auto &r : ring) { // dedupe the one-rings
                std::sort(r.begin(), r.end());
                r.erase(std::unique(r.begin(), r.end()), r.end());
            }

            const int passes = iterations < 1 ? 1 : (iterations > 500 ? 500 : iterations);
            const double k    = step < 0.0 ? 0.0 : (step > 1.0 ? 1.0 : step);

            std::vector<double> next(p.size());
            for (int pass = 0; pass < passes; ++pass) {
                const std::vector<double> nrm = preserveShape
                                                    ? vertexNormals(p, t, n)
                                                    : std::vector<double>();
                next = p;
                for (std::size_t v = 0; v < n; ++v) {
                    if (ring[v].empty() || (pinBoundary && onBoundary[v])) {
                        continue;
                    }
                    double c[3] = {0, 0, 0};
                    for (int w : ring[v]) {
                        for (int a = 0; a < 3; ++a) {
                            c[a] += p[3 * w + a];
                        }
                    }
                    double d[3];
                    for (int a = 0; a < 3; ++a) {
                        d[a] = c[a] / static_cast<double>(ring[v].size()) - p[3 * v + a];
                    }
                    if (preserveShape) {
                        // Remove the normal component: slide along the surface
                        // rather than into it.
                        const double dn = d[0] * nrm[3 * v] + d[1] * nrm[3 * v + 1] +
                                          d[2] * nrm[3 * v + 2];
                        for (int a = 0; a < 3; ++a) {
                            d[a] -= dn * nrm[3 * v + a];
                        }
                    }
                    for (int a = 0; a < 3; ++a) {
                        next[3 * v + a] = p[3 * v + a] + k * d[a];
                    }
                }
                p.swap(next);
            }

            m.setPositions(p);
            lastPasses            = passes;
            const auto q          = measure(m);
            meanQuality           = q.first;
            worstQuality          = q.second;
        }

        /** @brief One line describing the last run. */
        std::string report() const {
            return "relaxed " + std::to_string(lastPasses) + " passes: mean q=" +
                   std::to_string(meanQuality) + ", worst q=" + std::to_string(worstQuality);
        }

    private:
        // Area-weighted vertex normals: the sum of incident face normals, whose
        // magnitude is already twice the face area, so no explicit weighting.
        static std::vector<double> vertexNormals(const std::vector<double> &p,
                                                 const std::vector<int> &t, std::size_t n) {
            std::vector<double> nrm(3 * n, 0.0);
            for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
                const int a = t[i], b = t[i + 1], c = t[i + 2];
                if (a < 0 || b < 0 || c < 0 || a >= static_cast<int>(n) ||
                    b >= static_cast<int>(n) || c >= static_cast<int>(n)) {
                    continue;
                }
                const double u[3] = {p[3 * b] - p[3 * a], p[3 * b + 1] - p[3 * a + 1],
                                     p[3 * b + 2] - p[3 * a + 2]};
                const double v[3] = {p[3 * c] - p[3 * a], p[3 * c + 1] - p[3 * a + 1],
                                     p[3 * c + 2] - p[3 * a + 2]};
                const double f[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                     u[0] * v[1] - u[1] * v[0]};
                for (int idx : {a, b, c}) {
                    for (int k = 0; k < 3; ++k) {
                        nrm[3 * idx + k] += f[k];
                    }
                }
            }
            for (std::size_t v = 0; v < n; ++v) {
                const double len = std::sqrt(nrm[3 * v] * nrm[3 * v] +
                                             nrm[3 * v + 1] * nrm[3 * v + 1] +
                                             nrm[3 * v + 2] * nrm[3 * v + 2]);
                if (len > 1e-20) {
                    for (int k = 0; k < 3; ++k) {
                        nrm[3 * v + k] /= len;
                    }
                }
            }
            return nrm;
        }

        // (mean, worst) triangle quality. Degenerate triangles score 0 rather
        // than dividing by zero.
        static std::pair<double, double> measure(const Mesh &m) {
            const std::vector<double> p = m.positions();
            const std::vector<int>    t = m.triangles();
            const std::size_t         n = p.size() / 3;
            double sum = 0.0, worst = 1.0;
            std::size_t count = 0;
            for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
                const int a = t[i], b = t[i + 1], c = t[i + 2];
                if (a < 0 || b < 0 || c < 0 || a >= static_cast<int>(n) ||
                    b >= static_cast<int>(n) || c >= static_cast<int>(n)) {
                    continue;
                }
                const double u[3] = {p[3 * b] - p[3 * a], p[3 * b + 1] - p[3 * a + 1],
                                     p[3 * b + 2] - p[3 * a + 2]};
                const double v[3] = {p[3 * c] - p[3 * a], p[3 * c + 1] - p[3 * a + 1],
                                     p[3 * c + 2] - p[3 * a + 2]};
                const double w[3] = {p[3 * c] - p[3 * b], p[3 * c + 1] - p[3 * b + 1],
                                     p[3 * c + 2] - p[3 * b + 2]};
                const double cr[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                      u[0] * v[1] - u[1] * v[0]};
                const double area2 =
                    std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
                const double sq = u[0] * u[0] + u[1] * u[1] + u[2] * u[2] +
                                  v[0] * v[0] + v[1] * v[1] + v[2] * v[2] +
                                  w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
                // 4*sqrt(3)*A / sum(edge^2), with A = area2/2.
                const double q = sq > 1e-20 ? (2.0 * std::sqrt(3.0) * area2) / sq : 0.0;
                sum += q;
                worst = std::min(worst, q);
                ++count;
            }
            if (count == 0) {
                return {0.0, 0.0};
            }
            return {sum / static_cast<double>(count), worst};
        }
    };

} // namespace scene
