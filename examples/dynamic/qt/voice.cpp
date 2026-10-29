// Copyright (c) fmaerten@gmail.com
// License: MIT

// VoiceInput: miniaudio capture + whisper.cpp transcription. See voice.h.

#include "voice.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Capture only: none of miniaudio's decoders, encoders or engine.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>

#include <whisper.h>

#ifndef ROSETTA_VOICE_SOURCE_DIR
#define ROSETTA_VOICE_SOURCE_DIR "."
#endif

namespace voice {

    namespace {

        constexpr int    k_rate        = WHISPER_SAMPLE_RATE; // 16 kHz
        constexpr double k_max_seconds = 60.0;  // a runaway recording stops growing here
        constexpr double k_min_seconds = 0.3;   // shorter is a click, not a sentence
        constexpr double k_pad_seconds = 1.25;  // whisper ignores input under 1 s

        // whisper's initial prompt is capped at half its text context (~224
        // tokens); stay well inside it.
        constexpr int k_max_vocabulary_chars = 600;

        const char *k_default_model = "ggml-base.en.bin";

        // Whisper marks non-speech as "[BLANK_AUDIO]", "(music)", "[typing]".
        QString clean(const std::string &raw) {
            QString s = QString::fromUtf8(raw.c_str());
            static const QRegularExpression tags(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)"));
            s.remove(tags);
            return s.simplified();
        }

    } // namespace

    struct VoiceInput::Impl {
        VoiceInput *q;

        ma_device dev{};
        bool      dev_open = false;

        std::mutex         mu;
        std::vector<float> pcm; // 16 kHz mono, guarded by mu

        std::atomic<bool> busy{false};
        std::thread       worker;

        whisper_context *ctx = nullptr; // touched by the worker only
        std::string      ctx_path;

        std::string prompt; // the vocabulary, as whisper's initial prompt

        explicit Impl(VoiceInput *owner) : q(owner) {}

        ~Impl() {
            close_device();
            if (worker.joinable()) {
                worker.join();
            }
            if (ctx) {
                whisper_free(ctx);
            }
        }

        void close_device() {
            if (dev_open) {
                ma_device_uninit(&dev); // stops it, and waits for the callback to return
                dev_open = false;
            }
        }

        static void on_audio(ma_device *d, void *, const void *in, ma_uint32 frames) {
            auto *self = static_cast<Impl *>(d->pUserData);
            if (!in) {
                return;
            }
            const auto *f = static_cast<const float *>(in);
            std::lock_guard lock(self->mu);
            if (self->pcm.size() < std::size_t(k_max_seconds * k_rate)) {
                self->pcm.insert(self->pcm.end(), f, f + frames);
            }
        }

        // Runs on the worker thread. Returns the text, or throws a message.
        std::string transcribe(std::vector<float> audio, const std::string &path) {
            if (!ctx || ctx_path != path) {
                if (ctx) {
                    whisper_free(ctx);
                    ctx = nullptr;
                }
                whisper_context_params cp = whisper_context_default_params();
                if (qEnvironmentVariable("ROSETTA_WHISPER_GPU") == QStringLiteral("0")) {
                    cp.use_gpu = false;
                }
                ctx = whisper_init_from_file_with_params(path.c_str(), cp);
                if (!ctx) {
                    throw std::string("could not load the whisper model ") + path;
                }
                ctx_path = path;
            }

            std::string lang = qEnvironmentVariable("ROSETTA_WHISPER_LANG").toStdString();
            if (lang.empty()) {
                lang = whisper_is_multilingual(ctx) ? "auto" : "en";
            }

            const std::size_t min_samples = std::size_t(k_pad_seconds * k_rate);
            if (audio.size() < min_samples) {
                audio.resize(min_samples, 0.0f);
            }

            whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            p.n_threads        = int(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
            p.language         = lang.c_str();
            p.translate        = false;
            p.no_context       = true;
            p.no_timestamps    = true;
            p.suppress_blank   = true;
            p.print_progress   = false;
            p.print_realtime   = false;
            p.print_timestamps = false;
            p.print_special    = false;
            p.initial_prompt   = prompt.empty() ? nullptr : prompt.c_str();

            if (whisper_full(ctx, p, audio.data(), int(audio.size())) != 0) {
                throw std::string("whisper could not transcribe the recording");
            }
            std::string text;
            for (int i = 0; i < whisper_full_n_segments(ctx); ++i) {
                text += whisper_full_get_segment_text(ctx, i);
            }
            return text;
        }
    };

    VoiceInput::VoiceInput(QObject *parent) : QObject(parent), d_(std::make_unique<Impl>(this)) {
        // whisper and ggml log every model load to stderr; the viewer has a console of its own.
        whisper_log_set([](ggml_log_level, const char *, void *) {}, nullptr);
    }

    VoiceInput::~VoiceInput() = default;

    QString VoiceInput::model_path() const {
        const QString env = qEnvironmentVariable("ROSETTA_WHISPER_MODEL");
        if (!env.isEmpty()) {
            return env;
        }
        const QString near_exe = QCoreApplication::applicationDirPath() +
                                 QStringLiteral("/models/") + QLatin1String(k_default_model);
        if (QFileInfo::exists(near_exe)) {
            return near_exe;
        }
        return QStringLiteral(ROSETTA_VOICE_SOURCE_DIR "/models/") + QLatin1String(k_default_model);
    }

    bool VoiceInput::model_available() const { return QFileInfo(model_path()).isFile(); }

    void VoiceInput::set_vocabulary(const QStringList &words) {
        QString s = QStringLiteral("Glossary:");
        for (const QString &w : words) {
            if (s.size() + w.size() + 2 > k_max_vocabulary_chars) {
                break;
            }
            s += (s.endsWith(':') ? QStringLiteral(" ") : QStringLiteral(", ")) + w;
        }
        // The worker reads it only while busy, and this is called between turns.
        if (!d_->busy) {
            d_->prompt = words.isEmpty() ? std::string() : (s + QStringLiteral(".")).toStdString();
        }
    }

    bool VoiceInput::recording() const { return d_->dev_open; }
    bool VoiceInput::transcribing() const { return d_->busy; }

    double VoiceInput::seconds() const {
        std::lock_guard lock(d_->mu);
        return double(d_->pcm.size()) / k_rate;
    }

    void VoiceInput::start() {
        if (d_->dev_open || d_->busy) {
            return;
        }
        if (!model_available()) {
            emit failed(QStringLiteral("No whisper model at %1. Download one with\n"
                                       "    cmake -P fetch_whisper_model.cmake\n"
                                       "(in examples/dynamic), or set ROSETTA_WHISPER_MODEL.")
                            .arg(model_path()));
            return;
        }
        {
            std::lock_guard lock(d_->mu);
            d_->pcm.clear();
        }

        ma_device_config cfg  = ma_device_config_init(ma_device_type_capture);
        cfg.capture.format    = ma_format_f32;
        cfg.capture.channels  = 1;
        cfg.sampleRate        = k_rate; // miniaudio resamples from the device's own rate
        cfg.dataCallback      = &Impl::on_audio;
        cfg.pUserData         = d_.get();

        ma_result r = ma_device_init(nullptr, &cfg, &d_->dev);
        if (r != MA_SUCCESS) {
            emit failed(QStringLiteral("Could not open the microphone (%1).")
                            .arg(QString::fromUtf8(ma_result_description(r))));
            return;
        }
        d_->dev_open = true;
        r            = ma_device_start(&d_->dev);
        if (r != MA_SUCCESS) {
            d_->close_device();
            emit failed(QStringLiteral("Could not start the microphone (%1).")
                            .arg(QString::fromUtf8(ma_result_description(r))));
            return;
        }
        emit recordingChanged(true);
    }

    void VoiceInput::cancel() {
        if (!d_->dev_open) {
            return;
        }
        d_->close_device();
        emit recordingChanged(false);
    }

    void VoiceInput::stop() {
        if (!d_->dev_open) {
            return;
        }
        d_->close_device();
        emit recordingChanged(false);

        std::vector<float> audio;
        {
            std::lock_guard lock(d_->mu);
            audio.swap(d_->pcm);
        }
        if (audio.size() < std::size_t(k_min_seconds * k_rate)) {
            emit failed(QStringLiteral("Nothing recorded — click the microphone, speak, then click "
                                       "it again."));
            return;
        }

        if (d_->worker.joinable()) {
            d_->worker.join(); // the previous transcription has finished (busy was false)
        }
        d_->busy = true;
        emit transcribingChanged(true);

        d_->worker = std::thread([this, audio = std::move(audio),
                                  path = model_path().toStdString()]() mutable {
            QString text, error;
            try {
                text = clean(d_->transcribe(std::move(audio), path));
            } catch (const std::string &e) {
                error = QString::fromStdString(e);
            }
            // Back on the GUI thread. If the panel is gone by then, the call is dropped.
            QMetaObject::invokeMethod(
                this,
                [this, text, error] {
                    d_->busy = false;
                    emit transcribingChanged(false);
                    if (!error.isEmpty()) {
                        emit failed(error);
                    } else if (text.isEmpty()) {
                        emit failed(QStringLiteral("No speech recognised."));
                    } else {
                        emit transcribed(text);
                    }
                },
                Qt::QueuedConnection);
        });
    }

} // namespace voice
