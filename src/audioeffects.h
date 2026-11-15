#pragma once

#include <QAudioBuffer>
#include <QAudioSink>
#include <QAudioDevice>
#include <QTimer>
#include <QVariantList>
#include <array>
#include <functional>
#include <memory>
#include <vector>

// Per-channel biquads; no device access is needed to exercise the DSP.
class AudioDsp {
public:
    static constexpr int spectrumBands = 96;
    static constexpr std::array<double, 5> frequencies{60, 250, 1000, 4000, 12000};
    void configure(int rate, int channels, const std::array<double, 5> &gains);
    void reset();
    void process(float *samples, int frames);
    static QVariantList spectrum(const float *samples, int frames, int channels, int rate);
private:
    struct Filter {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::vector<std::array<double, 2>> memory;
    };
    std::array<Filter, 5> m_filters;
    int m_channels = 0;
    double m_headroom = 1;
};

// One output per decoder preserves the existing two-player crossfade.
class AudioEffects : public QObject {
public:
    explicit AudioEffects(QObject *parent = nullptr);
    void setGains(const std::array<double, 5> &gains);
    bool write(const QAudioBuffer &buffer, double volume);
    void clear();
    QByteArray processed() const { return m_processed; }
    QString error() const { return m_error; }
    static QByteArray floatSamples(const QAudioBuffer &buffer);
private:
    void pump();
    std::array<double, 5> m_gains{};
    AudioDsp m_dsp;
    QAudioFormat m_format;
    QAudioDevice m_device;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_output = nullptr;
    QByteArray m_pending, m_processed;
    QString m_error;
    QTimer m_pump;
};
