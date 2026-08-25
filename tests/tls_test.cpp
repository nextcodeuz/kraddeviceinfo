// Windows TLS test: HTTPS GET via Qt5Network
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslSocket>
#include <QTimer>
#include <cstdio>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    printf("TLS support: %s\n", QSslSocket::supportsSsl() ? "YES" : "NO");
    printf("SSL library: %s\n", QSslSocket::sslLibraryBuildVersionString().toUtf8().constData());
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl("https://ipwho.is/"));
    req.setTransferTimeout(15000);
    auto* rep = nam.get(req);
    QObject::connect(rep, &QNetworkReply::finished, [&] {
        if (rep->error() == QNetworkReply::NoError) {
            QByteArray body = rep->readAll();
            printf("HTTPS GET: OK (%d bytes)\n", int(body.size()));
            printf("sample: %s\n", body.left(80).constData());
        } else {
            printf("HTTPS GET FAIL: %s\n", rep->errorString().toUtf8().constData());
        }
        app.exit(rep->error() == QNetworkReply::NoError ? 0 : 1);
    });
    QTimer::singleShot(20000, [&] { printf("timeout\n"); app.exit(2); });
    return app.exec();
}
