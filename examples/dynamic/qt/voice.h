// Copyright (c) fmaerten@gmail.com
// License: MIT

// Speech input for the Claude panel: talk to the scene instead of typing.
//
// Push to talk: start() opens the default microphone, stop() closes it and
// transcribes what was said, LOCALLY, with whisper.cpp — no audio leaves the
// machine. The text then lands in the panel's prompt box like typed text, and
// goes to Claude the same way: speech is one more way in, nothing behind the
// prompt box changes.
//
// Capture is miniaudio (one header; CoreAudio, WASAPI, ALSA / PulseAudio), so
// the viewer needs no Qt Multimedia. miniaudio converts whatever the device
// delivers to what whisper wants: 16 kHz, mono, float.
//
// The library's own names are what plain speech recognition gets wrong
// ("Relaxer", "subdivide", "bunny"), and they are exactly what the dynamic
// registry knows. set_vocabulary() hands them to whisper as its initial
// prompt, which biases the decoder towards those spellings.
//
// Transcription runs on a worker thread; results come back as signals on the
// GUI thread. The model is loaded on first use and kept.
//
// Configuration (environment):
//   ROSETTA_WHISPER_MODEL  path to a ggml model file. Default: the first of
//                          <viewer dir>/models/ggml-base.en.bin,
//                          <source dir>/models/ggml-base.en.bin
//   ROSETTA_WHISPER_LANG   spoken language ("en", "fr", ..., or "auto");
//                          default "en" for an English-only (.en) model, else
//                          "auto". An .en model only understands English.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace voice {

    class VoiceInput : public QObject {
        Q_OBJECT

    public:
        explicit VoiceInput(QObject *parent = nullptr);
        ~VoiceInput() override;

        /** @brief The model file that will be used (may not exist). */
        QString model_path() const;

        /** @brief True when the model file exists. */
        bool model_available() const;

        /** @brief Words to bias recognition towards (class, method, object names). */
        void set_vocabulary(const QStringList &words);

        bool recording() const;
        bool transcribing() const;

        /** @brief Open the microphone and start recording. Emits failed() on error. */
        void start();

        /** @brief Stop recording and transcribe what was recorded. */
        void stop();

        /** @brief Stop recording and throw the audio away. */
        void cancel();

        /** @brief Seconds recorded so far (while recording). */
        double seconds() const;

    signals:
        void recordingChanged(bool on);
        void transcribingChanged(bool on);
        void transcribed(const QString &text);
        void failed(const QString &why);

    private:
        struct Impl;
        std::unique_ptr<Impl> d_;
    };

} // namespace voice
