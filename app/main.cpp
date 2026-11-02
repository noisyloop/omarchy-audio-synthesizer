// soundgineer: the panel in a normal desktop window. A thin Qt launcher that
// loads plugin/ui/PanelBody.qml (embedded) and connects it to
// soundgineer-engine over $XDG_RUNTIME_DIR/soundgineer.sock. Theme colors come
// from Omarchy's current theme and follow theme switches live.

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QRegularExpression>
#include <QtQml>

class LineSocket : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString path READ path CONSTANT)
public:
    explicit LineSocket(QObject* parent = nullptr) : QObject(parent) {
        connect(&sock_, &QLocalSocket::connected, this, &LineSocket::connectedChanged);
        connect(&sock_, &QLocalSocket::disconnected, this, &LineSocket::connectedChanged);
        connect(&sock_, &QLocalSocket::readyRead, this, [this] {
            buf_ += sock_.readAll();
            if (buf_.size() > 4 * 1024 * 1024) { buf_.clear(); sock_.abort(); return; }
            qsizetype nl;
            while ((nl = buf_.indexOf('\n')) >= 0) {
                const QString line = QString::fromUtf8(buf_.left(nl));
                buf_.remove(0, nl + 1);
                emit lineReceived(line);
            }
        });
    }
    bool connected() const { return sock_.state() == QLocalSocket::ConnectedState; }
    QString path() const {
        const QByteArray rt = qgetenv("XDG_RUNTIME_DIR");
        return QString::fromLocal8Bit(rt.isEmpty() ? QByteArray("/tmp") : rt) + "/soundgineer.sock";
    }
    Q_INVOKABLE void open() {
        if (sock_.state() == QLocalSocket::UnconnectedState) sock_.connectToServer(path());
    }
    Q_INVOKABLE void writeLine(const QString& line) {
        if (connected()) { sock_.write(line.toUtf8()); sock_.flush(); }
    }
signals:
    void connectedChanged();
    void lineReceived(const QString& line);
private:
    QLocalSocket sock_;
    QByteArray buf_;
};

class OmarchyTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
public:
    explicit OmarchyTheme(QObject* parent = nullptr) : QObject(parent) {
        file_ = QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml";
        load();
        watcher_.addPath(QFileInfo(file_).absolutePath());
        if (QFile::exists(file_)) watcher_.addPath(file_);
        connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &OmarchyTheme::reload);
        connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &OmarchyTheme::reload);
    }
    QVariantMap colors() const { return colors_; }
private:
    void reload() {
        load();
        if (QFile::exists(file_) && !watcher_.files().contains(file_)) watcher_.addPath(file_);
        emit changed();
    }
    void load() {
        // Defaults: Omarchy's Tokyo Night, used when no theme file exists.
        colors_ = {{"background", "#1a1b26"}, {"foreground", "#a9b1d6"}, {"accent", "#7aa2f7"}, {"urgent", "#f7768e"}};
        QFile f(file_);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        static const QRegularExpression re(QStringLiteral("^\\s*([a-z_0-9]+)\\s*=\\s*\"(#[0-9a-fA-F]{6,8})\""));
        while (!f.atEnd()) {
            const auto m = re.match(QString::fromUtf8(f.readLine()));
            if (m.hasMatch()) colors_[m.captured(1)] = m.captured(2);
        }
        if (colors_.contains("color1") && !colors_.contains("red")) colors_["urgent"] = colors_["color1"];
        if (colors_.contains("red")) colors_["urgent"] = colors_["red"];
    }
signals:
    void changed();
private:
    QString file_;
    QVariantMap colors_;
    QFileSystemWatcher watcher_;
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Soundgineer");
    QGuiApplication::setDesktopFileName("soundgineer");  // Wayland app id, for Hyprland window rules
    qmlRegisterType<LineSocket>("Soundgineer", 1, 0, "LineSocket");
    OmarchyTheme theme;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("omarchyTheme", &theme);
    engine.load(QUrl("qrc:/app/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    // Testing/docs: SG_SCREENSHOT=out.png grabs the window after 2.5 s and quits.
    if (const QByteArray shot = qgetenv("SG_SCREENSHOT"); !shot.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QTimer::singleShot(2500, &app, [window, shot] {
            if (window) window->grabWindow().save(QString::fromLocal8Bit(shot));
            QCoreApplication::quit();
        });
    }
    return app.exec();
}

#include "main.moc"
