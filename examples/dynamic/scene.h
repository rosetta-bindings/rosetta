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
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
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

        // One-click versions of the two solvers below, with their default
        // settings — what an Actions button can call, since a button passes
        // no arguments. Each returns the solver's report. For other settings,
        // create a Decimator / Relaxer and call its run() on this mesh.
        std::string decimate(); // keep half the triangles
        std::string relax();    // 10 passes, step 0.5, shape preserved

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
                    // Counter-clockwise seen from OUTSIDE, so the face
                    // normal cross(b-a, c-a) points outward. Wound the
                    // other way the sphere is lit from inside: the
                    // diffuse term reads inverted and no specular
                    // highlight can ever appear on a visible fragment.
                    for (int idx : {a, a + 1, b, a + 1, b + 1, b}) {
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
            // The scan's index table is wound CLOCKWISE seen from outside, so
            // taken verbatim every face normal points into the model: the
            // lighting reads inverted and no specular highlight can land on a
            // visible fragment. Swapping two indices per triangle flips it.
            // (Check: the signed volume sum(dot(a, cross(b,c)))/6 is negative
            // for the raw table and positive after the swap, as it is for
            // cube() and sphere().)
            m.t_.reserve(bunny::indexCount);
            for (std::size_t i = 0; i + 2 < bunny::indexCount; i += 3) {
                m.t_.push_back(static_cast<int>(bunny::indices[i]));
                m.t_.push_back(static_cast<int>(bunny::indices[i + 2]));
                m.t_.push_back(static_cast<int>(bunny::indices[i + 1]));
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

        // Replace the geometry outright — what a solver that changes the
        // TOPOLOGY (Decimator) needs, where setPositions() only moves vertices.
        // Ignored unless the positions are xyz triples and every index names
        // one of them.
        void setGeometry(const std::vector<double> &p, const std::vector<int> &t) {
            const int n = static_cast<int>(p.size() / 3);
            if (p.size() % 3 != 0 || t.size() % 3 != 0) {
                return;
            }
            for (int i : t) {
                if (i < 0 || i >= n) {
                    return;
                }
            }
            v_ = p;
            t_ = t;
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

    /**
     * @brief Simplifies a triangle mesh by quadric-error edge collapse.
     *
     * The second solver, and the one that changes TOPOLOGY: Relaxer moves
     * vertices, Decimator removes them. Same shape as Relaxer otherwise —
     * tuning parameters as public fields, one run(Mesh &) that works in place,
     * outcome fields a UI shows read-only — so it gets a property panel, a
     * console command and MCP tools without one line of UI code.
     *
     * The scheme is Garland & Heckbert's (SIGGRAPH 1997). Every vertex carries
     * a quadric: the sum of the squared-distance forms of the planes of its
     * triangles. Collapsing an edge merges its two vertices into one, placed
     * where the summed quadric is smallest, and its cost is the quadric's value
     * there — the squared distance to the planes the original surface had, so
     * flat regions go first and creases and tips last. Edges are collapsed
     * cheapest first until `ratio` of the triangles remain, or the next one
     * would move the surface by more than `maxError`.
     *
     * A collapse is refused when it would break the mesh rather than coarsen
     * it: when the edge's endpoints share neighbours other than the ones
     * across its own triangles (the link condition — the result would not be a
     * manifold), or when any triangle around it would turn by more than ~80°
     * (a fold-over). With `preserveBoundary`, open rims are held by
     * heavily weighted planes perpendicular to them, so a hole does not grow.
     */
    class Decimator {
    public:
        Decimator() = default;

        // ---- tuning: what a property sheet edits ------------------------
        double ratio            = 0.5;  // slider [0.01, 1], fraction of triangles to keep
        double maxError         = 0.0;  // [0, 0.1], object units; 0 = no limit
        bool   preserveBoundary = true; // hold open rims in place

        // ---- outcome: filled by run(), read-only to a UI ----------------
        int    trianglesBefore = 0;
        int    trianglesAfter  = 0;
        int    collapses       = 0;
        double worstError      = 0.0; // largest surface deviation a collapse introduced
        double meanQuality     = 0.0;

        /**
         * @brief Decimate `m` in place.
         *
         * Like Relaxer::run, takes the mesh by mutable reference — the
         * dynamic model passes it without a copy, so the view redraws the
         * very object that was simplified.
         */
        void run(Mesh &m) {
            std::vector<double>    p   = m.positions();
            const std::vector<int> tin = m.triangles();
            const int              n   = static_cast<int>(p.size() / 3);

            // Coincident vertices are one vertex as far as the surface goes:
            // subdivide() and the sphere's seam both leave copies, and
            // unwelded, every edge between them reads as an open rim.
            const std::vector<int> weld = welded(p);

            std::vector<std::array<int, 3>> F;
            F.reserve(tin.size() / 3);
            for (std::size_t i = 0; i + 2 < tin.size(); i += 3) {
                if (tin[i] < 0 || tin[i + 1] < 0 || tin[i + 2] < 0 || tin[i] >= n ||
                    tin[i + 1] >= n || tin[i + 2] >= n) {
                    continue;
                }
                const int a = weld[tin[i]], b = weld[tin[i + 1]], c = weld[tin[i + 2]];
                if (a < 0 || b < 0 || c < 0 || a >= n || b >= n || c >= n || a == b ||
                    b == c || a == c) {
                    continue; // out of range or degenerate: dropped
                }
                F.push_back({a, b, c});
            }
            trianglesBefore = static_cast<int>(F.size());
            collapses       = 0;
            worstError      = 0.0;
            if (F.empty()) {
                trianglesAfter = 0;
                meanQuality    = 0.0;
                return;
            }

            const double keep   = ratio < 0.001 ? 0.001 : (ratio > 1.0 ? 1.0 : ratio);
            const int    target = std::max(1, static_cast<int>(std::ceil(keep * F.size())));

            std::vector<char>             faceAlive(F.size(), 1);
            std::vector<std::vector<int>> vf(n); // vertex -> incident faces
            for (int f = 0; f < static_cast<int>(F.size()); ++f) {
                for (int v : F[f]) {
                    vf[v].push_back(f);
                }
            }

            // ---- quadrics ----------------------------------------------------
            std::vector<Quadric> Q(n);
            for (const auto &f : F) {
                double nrm[3];
                cross(p, f[0], f[1], f[2], nrm);
                const double len = std::sqrt(dot3(nrm, nrm));
                if (len < 1e-30) {
                    continue;
                }
                for (double &c : nrm) {
                    c /= len;
                }
                const double d = -(nrm[0] * p[3 * f[0]] + nrm[1] * p[3 * f[0] + 1] +
                                   nrm[2] * p[3 * f[0] + 2]);
                for (int v : f) {
                    Q[v].addPlane(nrm[0], nrm[1], nrm[2], d, 1.0);
                }
            }

            // Edges, each with how many faces use it (1 = boundary).
            std::vector<std::pair<std::pair<int, int>, int>> tally; // (edge, face)
            tally.reserve(F.size() * 3);
            for (int f = 0; f < static_cast<int>(F.size()); ++f) {
                for (int e = 0; e < 3; ++e) {
                    const int a = F[f][e], b = F[f][(e + 1) % 3];
                    tally.push_back({{std::min(a, b), std::max(a, b)}, f});
                }
            }
            std::sort(tally.begin(), tally.end());
            std::vector<char>                onBoundary(n, 0);
            std::vector<std::pair<int, int>> edges;
            for (std::size_t i = 0; i < tally.size();) {
                std::size_t j = i;
                while (j < tally.size() && tally[j].first == tally[i].first) {
                    ++j;
                }
                const auto [a, b] = tally[i].first;
                edges.push_back({a, b});
                if (j - i == 1) {
                    onBoundary[a] = onBoundary[b] = 1;
                    if (preserveBoundary) {
                        // A plane through the rim edge, perpendicular to its
                        // face: moving off the rim costs, so the rim stays.
                        double fn[3];
                        const auto &f = F[tally[i].second];
                        cross(p, f[0], f[1], f[2], fn);
                        const double e[3] = {p[3 * b] - p[3 * a], p[3 * b + 1] - p[3 * a + 1],
                                             p[3 * b + 2] - p[3 * a + 2]};
                        double pn[3] = {e[1] * fn[2] - e[2] * fn[1], e[2] * fn[0] - e[0] * fn[2],
                                        e[0] * fn[1] - e[1] * fn[0]};
                        const double len = std::sqrt(dot3(pn, pn));
                        if (len > 1e-30) {
                            for (double &c : pn) {
                                c /= len;
                            }
                            const double d = -(pn[0] * p[3 * a] + pn[1] * p[3 * a + 1] +
                                               pn[2] * p[3 * a + 2]);
                            Q[a].addPlane(pn[0], pn[1], pn[2], d, k_boundaryWeight);
                            Q[b].addPlane(pn[0], pn[1], pn[2], d, k_boundaryWeight);
                        }
                    }
                }
                i = j;
            }

            // ---- the queue -----------------------------------------------------
            // Lazy deletion: an entry records both endpoints' versions, and is
            // stale once either vertex has been collapsed into or away.
            std::vector<unsigned> ver(n, 0);
            std::vector<char>     vAlive(n, 1);
            std::priority_queue<Candidate, std::vector<Candidate>, std::greater<Candidate>> heap;
            const auto push = [&](int a, int b) {
                Candidate c{0.0, a, b, ver[a], ver[b], {0, 0, 0}};
                place(Q[a] + Q[b], p, a, b, c);
                heap.push(c);
            };
            for (const auto &[a, b] : edges) {
                push(a, b);
            }

            std::vector<int> ra, rb;
            const auto ring = [&](int v, std::vector<int> &out) {
                out.clear();
                for (int f : vf[v]) {
                    if (faceAlive[f]) {
                        for (int w : F[f]) {
                            if (w != v) {
                                out.push_back(w);
                            }
                        }
                    }
                }
                std::sort(out.begin(), out.end());
                out.erase(std::unique(out.begin(), out.end()), out.end());
            };

            int alive = static_cast<int>(F.size());
            while (alive > target && !heap.empty()) {
                const Candidate c = heap.top();
                heap.pop();
                const int a = c.a, b = c.b;
                if (!vAlive[a] || !vAlive[b] || ver[a] != c.va || ver[b] != c.vb) {
                    continue; // stale
                }
                const double err = std::sqrt(c.cost);
                if (maxError > 0.0 && err > maxError) {
                    break; // everything left costs more
                }

                int shared = 0;
                for (int f : vf[a]) {
                    shared += faceAlive[f] && (F[f][0] == b || F[f][1] == b || F[f][2] == b);
                }
                if (shared == 0 || shared > 2) {
                    continue; // gone, or non-manifold
                }
                if (onBoundary[a] && onBoundary[b] && shared == 2) {
                    continue; // an interior edge spanning a rim: would pinch it shut
                }
                // Link condition: the only common neighbours are the
                // vertices opposite the edge, one per shared face.
                ring(a, ra);
                ring(b, rb);
                int common = 0;
                for (std::size_t i = 0, j = 0; i < ra.size() && j < rb.size();) {
                    if (ra[i] < rb[j]) {
                        ++i;
                    } else if (rb[j] < ra[i]) {
                        ++j;
                    } else {
                        ++common, ++i, ++j;
                    }
                }
                if (common != shared) {
                    continue;
                }
                if (folds(F, faceAlive, vf, p, a, b, c.x) || duplicates(F, faceAlive, vf, a, b)) {
                    continue;
                }

                // Collapse b into a.
                for (int k = 0; k < 3; ++k) {
                    p[3 * a + k] = c.x[k];
                }
                Q[a]          = Q[a] + Q[b];
                onBoundary[a] = onBoundary[a] || onBoundary[b];
                vAlive[b]     = 0;
                ++ver[a];
                ++ver[b];
                for (int f : vf[b]) {
                    if (!faceAlive[f]) {
                        continue;
                    }
                    auto &tri = F[f];
                    if (tri[0] == a || tri[1] == a || tri[2] == a) {
                        faceAlive[f] = 0; // the edge's own triangles vanish
                        --alive;
                    } else {
                        for (int &v : tri) {
                            v = v == b ? a : v;
                        }
                        vf[a].push_back(f);
                    }
                }
                vf[b].clear();
                auto &fa = vf[a];
                fa.erase(std::remove_if(fa.begin(), fa.end(), [&](int f) { return !faceAlive[f]; }),
                         fa.end());

                worstError = std::max(worstError, err);
                ++collapses;
                ring(a, ra);
                for (int w : ra) {
                    push(a, w);
                }
            }

            // ---- compact: drop dead faces and unused vertices ------------------
            std::vector<int>    remap(n, -1);
            std::vector<double> np;
            std::vector<int>    nt;
            nt.reserve(static_cast<std::size_t>(alive) * 3);
            for (std::size_t f = 0; f < F.size(); ++f) {
                if (!faceAlive[f]) {
                    continue;
                }
                for (int v : F[f]) {
                    if (remap[v] < 0) {
                        remap[v] = static_cast<int>(np.size() / 3);
                        np.insert(np.end(), {p[3 * v], p[3 * v + 1], p[3 * v + 2]});
                    }
                    nt.push_back(remap[v]);
                }
            }
            m.setGeometry(np, nt);
            trianglesAfter = static_cast<int>(nt.size() / 3);
            meanQuality    = Relaxer::quality(m);
        }

        /** @brief One line describing the last run. */
        std::string report() const {
            return "decimated " + std::to_string(trianglesBefore) + " -> " +
                   std::to_string(trianglesAfter) + " triangles in " + std::to_string(collapses) +
                   " collapses: worst error " + std::to_string(worstError) + ", mean q=" +
                   std::to_string(meanQuality);
        }

    private:
        // Rim planes outweigh the surface's own by this much.
        static constexpr double k_boundaryWeight = 1000.0;

        // A collapse that turns any surviving triangle further than this
        // (cos 80°) is a fold-over in the making.
        static constexpr double k_minCosTurn = 0.17;

        // Symmetric 4x4 quadric, upper triangle:
        // aa ab ac ad / bb bc bd / cc cd / dd.
        struct Quadric {
            std::array<double, 10> q{};

            void addPlane(double a, double b, double c, double d, double w) {
                q[0] += w * a * a, q[1] += w * a * b, q[2] += w * a * c, q[3] += w * a * d;
                q[4] += w * b * b, q[5] += w * b * c, q[6] += w * b * d;
                q[7] += w * c * c, q[8] += w * c * d;
                q[9] += w * d * d;
            }
            Quadric operator+(const Quadric &o) const {
                Quadric r;
                for (int i = 0; i < 10; ++i) {
                    r.q[i] = q[i] + o.q[i];
                }
                return r;
            }
            double eval(const double x[3]) const {
                return q[0] * x[0] * x[0] + 2 * q[1] * x[0] * x[1] + 2 * q[2] * x[0] * x[2] +
                       2 * q[3] * x[0] + q[4] * x[1] * x[1] + 2 * q[5] * x[1] * x[2] +
                       2 * q[6] * x[1] + q[7] * x[2] * x[2] + 2 * q[8] * x[2] + q[9];
            }
        };

        struct Candidate {
            double   cost;
            int      a, b;
            unsigned va, vb;
            double   x[3]; // where the merged vertex goes
            bool operator>(const Candidate &o) const { return cost > o.cost; }
        };

        static double dot3(const double u[3], const double v[3]) {
            return u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
        }

        // (p[b]-p[a]) x (p[c]-p[a]): the face normal, twice the area long.
        static void cross(const std::vector<double> &p, int a, int b, int c, double out[3]) {
            const double u[3] = {p[3 * b] - p[3 * a], p[3 * b + 1] - p[3 * a + 1],
                                 p[3 * b + 2] - p[3 * a + 2]};
            const double v[3] = {p[3 * c] - p[3 * a], p[3 * c + 1] - p[3 * a + 1],
                                 p[3 * c + 2] - p[3 * a + 2]};
            out[0] = u[1] * v[2] - u[2] * v[1];
            out[1] = u[2] * v[0] - u[0] * v[2];
            out[2] = u[0] * v[1] - u[1] * v[0];
        }

        // Where the merged vertex goes: the quadric's minimum when it is well
        // defined and near the edge, else the best of the endpoints and the
        // midpoint (a flat or straight region has no unique minimum).
        static void place(const Quadric &Q, const std::vector<double> &p, int a, int b,
                          Candidate &c) {
            const auto &q   = Q.q;
            const double m[3] = {0.5 * (p[3 * a] + p[3 * b]), 0.5 * (p[3 * a + 1] + p[3 * b + 1]),
                                 0.5 * (p[3 * a + 2] + p[3 * b + 2])};
            const double e[3] = {p[3 * b] - p[3 * a], p[3 * b + 1] - p[3 * a + 1],
                                 p[3 * b + 2] - p[3 * a + 2]};
            const double len2 = dot3(e, e);

            const double det = q[0] * (q[4] * q[7] - q[5] * q[5]) -
                               q[1] * (q[1] * q[7] - q[5] * q[2]) +
                               q[2] * (q[1] * q[5] - q[4] * q[2]);
            const double tr = q[0] + q[4] + q[7];
            if (std::abs(det) > 1e-10 * tr * tr * tr) {
                // Solve A x = -b by Cramer's rule.
                const double r[3] = {-q[3], -q[6], -q[8]};
                const double x[3] = {
                    (r[0] * (q[4] * q[7] - q[5] * q[5]) - q[1] * (r[1] * q[7] - q[5] * r[2]) +
                     q[2] * (r[1] * q[5] - q[4] * r[2])) / det,
                    (q[0] * (r[1] * q[7] - q[5] * r[2]) - r[0] * (q[1] * q[7] - q[5] * q[2]) +
                     q[2] * (q[1] * r[2] - r[1] * q[2])) / det,
                    (q[0] * (q[4] * r[2] - r[1] * q[5]) - q[1] * (q[1] * r[2] - r[1] * q[2]) +
                     r[0] * (q[1] * q[5] - q[4] * q[2])) / det};
                const double d[3] = {x[0] - m[0], x[1] - m[1], x[2] - m[2]};
                if (dot3(d, d) <= 4.0 * len2) { // a far-off minimum is numerical noise
                    std::copy(x, x + 3, c.x);
                    c.cost = std::max(0.0, Q.eval(x));
                    return;
                }
            }
            const double *options[3] = {&p[3 * a], &p[3 * b], m};
            c.cost = std::numeric_limits<double>::max();
            for (const double *o : options) {
                const double cost = std::max(0.0, Q.eval(o));
                if (cost < c.cost) {
                    c.cost = cost;
                    std::copy(o, o + 3, c.x);
                }
            }
        }

        // Each vertex mapped to the first one at the same place (within a
        // hair of the bounding box), found by sorting quantised positions.
        static std::vector<int> welded(const std::vector<double> &p) {
            const int n = static_cast<int>(p.size() / 3);
            double    lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
            for (int v = 0; v < n; ++v) {
                for (int k = 0; k < 3; ++k) {
                    lo[k] = v == 0 ? p[k] : std::min(lo[k], p[3 * v + k]);
                    hi[k] = v == 0 ? p[k] : std::max(hi[k], p[3 * v + k]);
                }
            }
            const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) +
                                          (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                          (hi[2] - lo[2]) * (hi[2] - lo[2]));
            const double tol = diag > 0.0 ? 1e-7 * diag : 1.0;
            std::vector<std::array<long long, 3>> key(n);
            for (int v = 0; v < n; ++v) {
                for (int k = 0; k < 3; ++k) {
                    key[v][k] = std::llround((p[3 * v + k] - lo[k]) / tol);
                }
            }
            std::vector<int> order(n);
            for (int v = 0; v < n; ++v) {
                order[v] = v;
            }
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return key[a] != key[b] ? key[a] < key[b] : a < b;
            });
            std::vector<int> to(n);
            for (int i = 0; i < n; ++i) {
                to[order[i]] = (i > 0 && key[order[i]] == key[order[i - 1]]) ? to[order[i - 1]]
                                                                             : order[i];
            }
            return to;
        }

        // Would collapsing b into a turn one of b's triangles into a copy of
        // one a already has? That is how a closed mesh's last tetrahedron
        // would flatten into two back-to-back triangles.
        static bool duplicates(const std::vector<std::array<int, 3>> &F,
                               const std::vector<char> &faceAlive,
                               const std::vector<std::vector<int>> &vf, int a, int b) {
            const auto sorted = [](std::array<int, 3> t) {
                std::sort(t.begin(), t.end());
                return t;
            };
            for (int fb : vf[b]) {
                const auto &tb = F[fb];
                if (!faceAlive[fb] || tb[0] == a || tb[1] == a || tb[2] == a) {
                    continue;
                }
                std::array<int, 3> moved = tb;
                for (int &v : moved) {
                    v = v == b ? a : v;
                }
                moved = sorted(moved);
                for (int fa : vf[a]) {
                    if (faceAlive[fa] && sorted(F[fa]) == moved) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Would moving a and b to x fold any surviving triangle around them?
        static bool folds(const std::vector<std::array<int, 3>> &F,
                          const std::vector<char> &faceAlive,
                          const std::vector<std::vector<int>> &vf, const std::vector<double> &p,
                          int a, int b, const double x[3]) {
            for (int v : {a, b}) {
                for (int f : vf[v]) {
                    if (!faceAlive[f]) {
                        continue;
                    }
                    const auto &t = F[f];
                    const bool hasA = t[0] == a || t[1] == a || t[2] == a;
                    const bool hasB = t[0] == b || t[1] == b || t[2] == b;
                    if (hasA && hasB) {
                        continue; // removed by the collapse
                    }
                    double before[3];
                    cross(p, t[0], t[1], t[2], before);
                    double q[3][3];
                    for (int k = 0; k < 3; ++k) {
                        const bool moved = t[k] == a || t[k] == b;
                        for (int j = 0; j < 3; ++j) {
                            q[k][j] = moved ? x[j] : p[3 * t[k] + j];
                        }
                    }
                    const double u[3] = {q[1][0] - q[0][0], q[1][1] - q[0][1], q[1][2] - q[0][2]};
                    const double w[3] = {q[2][0] - q[0][0], q[2][1] - q[0][1], q[2][2] - q[0][2]};
                    const double after[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
                                             u[0] * w[1] - u[1] * w[0]};
                    const double lb = std::sqrt(dot3(before, before));
                    const double la = std::sqrt(dot3(after, after));
                    if (la < 1e-24 || dot3(before, after) < k_minCosTurn * lb * la) {
                        return true;
                    }
                }
            }
            return false;
        }
    };

    /**
     * @brief Cuts a mesh with a plane and keeps one side.
     *
     * The third solver: a plane given by an orientation and a position, and
     * one run(Mesh &) that removes everything on the other side. Triangles
     * that cross the plane are clipped exactly, not dropped, so the cut edge
     * follows the plane; a new vertex on an edge is made once and shared by
     * the two triangles on either side of it, so the section has no cracks.
     * The cut is left OPEN — you see into the mesh.
     *
     * The plane's normal points along (azimuth, elevation): elevation 90 is
     * straight up (+Y), so the default is a horizontal plane. `offset` moves
     * it along that normal, measured from the centre of the mesh's bounding
     * box, in the mesh's own (object) space — the mesh's origin, size and
     * spin do not enter into it. `keep` names the side that stays: "above"
     * is the side the normal points to.
     */
    class Cutter {
    public:
        Cutter() = default;

        // ---- the plane: what a property sheet edits ---------------------
        double      azimuth   = 0.0;     // [0, 360] deg, around Y from +X
        double      elevation = 90.0;    // [-90, 90] deg, 90 = normal straight up
        double      offset    = 0.0;     // [-1, 1], from the bounding-box centre
        std::string keep      = "above"; // "above" | "below"

        // ---- outcome: filled by run(), read-only to a UI ----------------
        int trianglesBefore = 0;
        int trianglesAfter  = 0;
        int newVertices     = 0; // created on the cut

        /**
         * @brief Cut `m` in place. Leaves it untouched when nothing would
         * remain on the kept side.
         */
        void run(Mesh &m) {
            const std::vector<double> p = m.positions();
            const std::vector<int>    t = m.triangles();
            const int                 n = static_cast<int>(p.size() / 3);
            trianglesBefore             = static_cast<int>(t.size() / 3);
            trianglesAfter              = trianglesBefore;
            newVertices                 = 0;
            if (n == 0 || t.empty()) {
                return;
            }

            // The plane: unit normal, through the bounding-box centre moved
            // by `offset` along it.
            constexpr double deg = 3.14159265358979323846 / 180.0;
            const double     az = azimuth * deg, el = elevation * deg;
            const double     nrm[3] = {std::cos(el) * std::cos(az), std::sin(el),
                                       std::cos(el) * std::sin(az)};
            double lo[3] = {p[0], p[1], p[2]}, hi[3] = {p[0], p[1], p[2]};
            for (int v = 1; v < n; ++v) {
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], p[3 * v + k]);
                    hi[k] = std::max(hi[k], p[3 * v + k]);
                }
            }
            const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) +
                                          (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                          (hi[2] - lo[2]) * (hi[2] - lo[2]));
            double c[3];
            for (int k = 0; k < 3; ++k) {
                c[k] = 0.5 * (lo[k] + hi[k]) + offset * nrm[k];
            }

            // Signed distance of every vertex, positive on the kept side.
            // Within a hair of the plane counts as ON it, which is kept and
            // needs no new vertex — no sliver triangles along the cut.
            const double     side = keep == "below" ? -1.0 : 1.0;
            const double     eps  = 1e-9 * (diag > 0.0 ? diag : 1.0);
            std::vector<double> f(n);
            for (int v = 0; v < n; ++v) {
                const double d = nrm[0] * (p[3 * v] - c[0]) + nrm[1] * (p[3 * v + 1] - c[1]) +
                                 nrm[2] * (p[3 * v + 2] - c[2]);
                f[v] = std::abs(d) < eps ? 0.0 : side * d;
            }

            std::vector<double>              np = p; // new vertices are appended
            std::vector<int>                 nt;
            std::map<std::pair<int, int>, int> onEdge; // edge -> its cut vertex
            const auto cutVertex = [&](int a, int b) {
                const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
                if (auto it = onEdge.find(key); it != onEdge.end()) {
                    return it->second;
                }
                const double s = f[a] / (f[a] - f[b]);
                for (int k = 0; k < 3; ++k) {
                    np.push_back(p[3 * a + k] + s * (p[3 * b + k] - p[3 * a + k]));
                }
                const int id = static_cast<int>(np.size() / 3) - 1;
                onEdge.emplace(key, id);
                ++newVertices;
                return id;
            };

            for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
                const int tri[3] = {t[i], t[i + 1], t[i + 2]};
                if (tri[0] < 0 || tri[1] < 0 || tri[2] < 0 || tri[0] >= n || tri[1] >= n ||
                    tri[2] >= n) {
                    continue;
                }
                // Clip the triangle against the kept half-space (Sutherland-
                // Hodgman): at most four corners, in order, then a fan.
                int poly[4], count = 0;
                for (int e = 0; e < 3; ++e) {
                    const int a = tri[e], b = tri[(e + 1) % 3];
                    if (f[a] >= 0.0) {
                        poly[count++] = a;
                    }
                    if ((f[a] > 0.0 && f[b] < 0.0) || (f[a] < 0.0 && f[b] > 0.0)) {
                        poly[count++] = cutVertex(a, b);
                    }
                }
                for (int k = 1; k + 1 < count; ++k) {
                    nt.insert(nt.end(), {poly[0], poly[k], poly[k + 1]});
                }
            }
            if (nt.empty()) {
                trianglesAfter = trianglesBefore;
                newVertices    = 0;
                return; // nothing on the kept side: leave the mesh as it was
            }

            // Drop the vertices nothing uses any more.
            std::vector<int>    remap(np.size() / 3, -1);
            std::vector<double> cp;
            for (int &v : nt) {
                if (remap[v] < 0) {
                    remap[v] = static_cast<int>(cp.size() / 3);
                    cp.insert(cp.end(), {np[3 * v], np[3 * v + 1], np[3 * v + 2]});
                }
                v = remap[v];
            }
            m.setGeometry(cp, nt);
            trianglesAfter = static_cast<int>(nt.size() / 3);
        }

        /** @brief One line describing the last run. */
        std::string report() const {
            return "cut " + std::to_string(trianglesBefore) + " -> " +
                   std::to_string(trianglesAfter) + " triangles, " +
                   std::to_string(newVertices) + " new vertices along the cut";
        }
    };

    // Defined here, once both solvers are complete types.
    inline std::string Mesh::decimate() {
        Decimator d;
        d.run(*this);
        return d.report();
    }

    inline std::string Mesh::relax() {
        Relaxer r;
        r.run(*this);
        return r.report();
    }

} // namespace scene
