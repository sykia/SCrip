#include "audioeffects.h"
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

void AudioDsp::configure(int rate, int channels, const std::array<double, 5> &gains)
{
    m_channels = channels;
    double maximum = 0;
    for (int i = 0; i < 5; ++i) {
        const double db = std::clamp(gains[i], -12.0, 12.0);
        maximum = std::max(maximum, db);
        const double a = std::pow(10.0, db / 40);
        const double omega = 2 * std::numbers::pi * std::min(frequencies[i], rate * 0.45) / rate;
        const double alpha = std::sin(omega) / 1.4; // Q = 0.7, broad musical bands.
        const double denominator = 1 + alpha / a;
        auto &f = m_filters[i];
        f.b0 = (1 + alpha * a) / denominator;
        f.b1 = -2 * std::cos(omega) / denominator;
        f.b2 = (1 - alpha * a) / denominator;
        f.a1 = f.b1;
        f.a2 = (1 - alpha / a) / denominator;
        f.memory.resize(channels);
    }
    // Automatic headroom for boosted bands. Final limiting guards extreme overlaps.
    m_headroom = std::pow(10.0, -maximum / 20);
}
void AudioDsp::reset()
{
    for (auto &f : m_filters) for (auto &state : f.memory) state = {};
}
void AudioDsp::process(float *samples, int frames)
{
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < m_channels; ++channel) {
            double value = samples[frame * m_channels + channel];
            if (!std::isfinite(value)) value = 0;
            for (auto &f : m_filters) {
                auto &z = f.memory[channel];
                const double output = f.b0 * value + z[0];
                z[0] = f.b1 * value - f.a1 * output + z[1];
                z[1] = f.b2 * value - f.a2 * output;
                value = output;
            }
            samples[frame * m_channels + channel] = std::clamp(value * m_headroom, -1.0, 1.0);
        }
    }
}
QVariantList AudioDsp::spectrum(const float *samples, int frames, int channels, int rate)
{
    constexpr int size = 4096, bands = spectrumBands;
    std::array<double, size / 2> power{};
    const int count = std::min(frames, size);
    if (count < 2 || channels < 1 || rate < 1) return QVariantList(bands, 0.0);
    const int analyzedChannels = std::min(channels, 2);
    for (int channel = 0; channel < analyzedChannels; ++channel) {
        std::array<std::complex<double>, size> bins{};
        for (int i = 0; i < count; ++i) {
            const double sample = samples[i * channels + channel];
            bins[i] = (std::isfinite(sample) ? sample : 0) * (0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / (count - 1)));
        }
        for (int i = 1, j = 0; i < size; ++i) {
            int bit = size >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(bins[i], bins[j]);
        }
        for (int length = 2; length <= size; length <<= 1) {
            const auto step = std::polar(1.0, -2 * std::numbers::pi / length);
            for (int i = 0; i < size; i += length) {
                std::complex<double> phase{1, 0};
                for (int j = 0; j < length / 2; ++j) {
                    const auto a = bins[i + j], b = bins[i + j + length / 2] * phase;
                    bins[i + j] = a + b; bins[i + j + length / 2] = a - b;
                    phase *= step;
                }
            }
        }
        for (int i = 1; i < size / 2; ++i) power[i] += std::norm(bins[i]) / analyzedChannels;
    }
    QVariantList result;
    const double highest = std::min(18000.0, rate * 0.48);
    for (int band = 0; band < bands; ++band) {
        const double low = 30 * std::pow(highest / 30, band / double(bands));
        const double high = 30 * std::pow(highest / 30, (band + 1) / double(bands));
        const int first = std::clamp(int(std::round(low * size / rate)), 1, size / 2 - 1);
        const int last = std::clamp(int(std::round(high * size / rate)), first + 1, size / 2);
        double peak = 0;
        for (int bin = first; bin < last; ++bin) peak = std::max(peak, power[bin]);
        const double amplitude = std::sqrt(peak) * 4 / count;
        const double db = 20 * std::log10(std::max(amplitude, 1e-6));
        result.append(std::clamp((db + 60) / 60, 0.0, 1.0));
    }
    return result;
}
AudioEffects::AudioEffects(QObject *parent) : QObject(parent)
{
    m_pump.setInterval(5);
    connect(&m_pump, &QTimer::timeout, this, [this] { pump(); });
}
void AudioEffects::setGains(const std::array<double, 5> &gains)
{
    m_gains = gains;
    if (m_format.isValid()) m_dsp.configure(m_format.sampleRate(), m_format.channelCount(), gains);
}
void AudioEffects::clear()
{
    m_pump.stop();
    m_output = nullptr;
    if (m_sink) m_sink->reset();
    m_sink.reset();
    m_format = {};
    m_pending.clear(); m_processed.clear(); m_dsp.reset();
}
QByteArray AudioEffects::floatSamples(const QAudioBuffer &buffer)
{
    const auto format = buffer.format();
    QByteArray result(buffer.sampleCount() * sizeof(float), Qt::Uninitialized);
    auto *output = reinterpret_cast<float *>(result.data());
    const auto *input = buffer.constData<char>();
    for (int i = 0; i < buffer.sampleCount(); ++i) {
        const float value = format.normalizedSampleValue(input + i * format.bytesPerSample());
        output[i] = std::isfinite(value) ? value : 0;
    }
    return result;
}
bool AudioEffects::write(const QAudioBuffer &buffer, double volume)
{
    if (!buffer.isValid() || buffer.frameCount() <= 0) return true;
    QAudioFormat format = buffer.format();
    format.setSampleFormat(QAudioFormat::Float);
    const auto device = QMediaDevices::defaultAudioOutput();
    if (!m_sink || m_format != format || m_device != device) {
        clear(); m_error.clear();
        if (device.isNull() || !device.isFormatSupported(format)) {
            m_error = QStringLiteral("Выходное устройство не поддерживает обработку этого аудиоформата.");
            return false;
        }
        m_format = format; m_device = device;
        m_dsp.configure(format.sampleRate(), format.channelCount(), m_gains);
        m_sink = std::make_unique<QAudioSink>(device, format);
        m_sink->setBufferSize(format.bytesForDuration(100000));
        m_output = m_sink->start();
        if (!m_output) {
            m_error = QStringLiteral("Не удалось запустить обработанный аудиовыход.");
            clear(); return false;
        }
        m_pump.start();
    }
    // The media player's clock paces decoded buffers; this bounded queue only absorbs device jitter.
    if (m_sink->error() == QAudio::OpenError || m_sink->error() == QAudio::IOError || m_sink->error() == QAudio::FatalError) {
        m_error = QStringLiteral("Аудиовыход недоступен. Эквалайзер выключен.");
        clear(); return false;
    }
    m_sink->setVolume(std::clamp(volume, 0.0, 1.0));
    m_processed = floatSamples(buffer);
    m_dsp.process(reinterpret_cast<float *>(m_processed.data()), buffer.frameCount());
    m_pending.append(m_processed);
    pump();
    if (m_pending.size() > format.bytesForDuration(250000)) {
        m_error = QStringLiteral("Аудиовыход не успевает обрабатывать звук. Эквалайзер выключен.");
        clear(); return false;
    }
    return true;
}
void AudioEffects::pump()
{
    if (!m_output || !m_sink || m_pending.isEmpty()) return;
    const qint64 count = std::min<qint64>(m_pending.size(), m_sink->bytesFree());
    if (count <= 0) return;
    const qint64 written = m_output->write(m_pending.constData(), count);
    if (written > 0) m_pending.remove(0, written);
}
