// Copyright (c) fmaerten@gmail.com
// License: MIT

// A small "existing library" for the pendulum example: one class, documented
// the way a scientific library usually is — Doxygen comments in the header.
//
// Plain C++17: no rosetta include, no [[= rosetta::... ]]. The doc / range /
// readonly metadata of the fields lives in Pendulum.ann.json, so every target
// in manifest.json (nanobind, lua, mcp) builds with an off-the-shelf compiler.
// Chapter 16 of the book shows the same class with two of those annotations
// written inline instead; the metadata is identical.

#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace lab {

    /// A simple pendulum, integrated with a fixed time step.
    class Pendulum {
    public:
        // Every field is annotated in Pendulum.ann.json (doc, unit, range, ...).
        double      length      = 1.0;
        double      angle       = 30.0;
        double      damping     = 0.05;
        std::string integrator  = "symplectic";
        std::string name        = "pendulum";
        bool        recordTrace = true;
        double      time        = 0.0;
        std::vector<double> trace; // angle samples, one per simulated 0.05 s
        double      scratch     = 0.0; // hidden: public for the solver, not API

        /**
         * @brief Advance the simulation.
         * @param seconds how long to simulate
         * @param dt      the time step, in seconds
         * @return the angle at the end, in degrees
         */
        double simulate(double seconds, double dt) {
            const double k  = kPi / 180.0;
            const bool   se = integrator != "euler";
            double       th = angle * k, w = omega_;
            for (double t = 0; t < seconds; t += dt) {
                const double acc = -kG / length * std::sin(th) - damping * w;
                if (se) { // semi-implicit (symplectic) Euler: velocity first
                    w += acc * dt;
                    th += w * dt;
                } else { // explicit Euler: drifts, kept to show the difference
                    th += w * dt;
                    w += acc * dt;
                }
                sampleClock_ += dt;
                if (recordTrace && sampleClock_ >= 0.05) {
                    trace.push_back(th / k);
                    sampleClock_ = 0;
                }
            }
            omega_ = w;
            angle  = th / k;
            time += seconds;
            return angle;
        }

        /// Small-angle period, 2 pi sqrt(L/g), in seconds.
        double period() const { return 2 * kPi * std::sqrt(length / kG); }

        /// Small-angle frequency, 1 / period(), in hertz.
        double frequency() const { return 1.0 / period(); }

        /// The period, under its old name.
        double oldPeriod() const { return period(); }

        /// Back to rest at 30 degrees: clears time and the trace.
        void reset() {
            angle = 30.0;
            omega_ = 0.0;
            time   = 0.0;
            trace.clear();
        }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static constexpr double kG  = 9.81;

        double omega_       = 0.0; // angular velocity, rad/s
        double sampleClock_ = 0.0;
    };

} // namespace lab
