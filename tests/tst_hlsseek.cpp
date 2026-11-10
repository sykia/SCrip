#include <QtTest>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioOutput>
#include <QMediaPlayer>

// Запуск: SCRIP_HLS_TEST_PLAYLIST=/.../test.m3u8 ./scrip_hls_seek_tests
// Файл создаёт tests/run-hls-seek.sh; музыка и аккаунт для теста не нужны.
class HlsSeekTest : public QObject {
    Q_OBJECT
private slots:
    void seekKeepsDecodedAudio() {
        const auto path = qEnvironmentVariable("SCRIP_HLS_TEST_PLAYLIST");
        if (path.isEmpty()) QSKIP("Задайте SCRIP_HLS_TEST_PLAYLIST с тестовым fMP4 HLS");
        QMediaPlayer player;
        QAudioOutput audio;
        QAudioBufferOutput output;
        player.setAudioOutput(&audio);
        player.setAudioBufferOutput(&output);
        int buffers = 0;
        connect(&output, &QAudioBufferOutput::audioBufferReceived, this,
                [&buffers](const QAudioBuffer &buffer) { if (buffer.isValid()) ++buffers; });
        player.setSource(QUrl::fromLocalFile(path));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 500 && buffers > 10, 10000);
        QVERIFY(player.isSeekable());
        for (qint64 target : {60000, 15000, 70000, 0}) {
            buffers = 0;
            player.setPosition(target);
            // Сам setPosition не подтверждает воспроизведение: ждём PCM и ход часов.
            QTRY_VERIFY_WITH_TIMEOUT(player.position() > target + 500 && buffers > 10, 10000);
            QCOMPARE(player.error(), QMediaPlayer::NoError);
        }
        player.pause();
        player.setPosition(30000);
        QCOMPARE(player.playbackState(), QMediaPlayer::PausedState);
        buffers = 0;
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 30500 && buffers > 10, 10000);
        player.setPosition(50000);
        player.setPosition(10000);
        player.setPosition(40000);
        buffers = 0;
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 40500 && buffers > 10, 10000);
    }
};
QTEST_GUILESS_MAIN(HlsSeekTest)
#include "tst_hlsseek.moc"
