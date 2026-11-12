#include <QApplication>
#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QDateTime>
#include <QPainter>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

struct Panel {
    QString name, unit;
    double ymin, ymax;
    QLabel*      header = nullptr;
    QLineSeries* series = nullptr;
    QValueAxis*  ax     = nullptr;
    QString      lastStatus = "OK";
    int          n = 0;
};

static QString statusColor(const QString& s) {
    if (s == "OK")       return "#2e7d32";
    if (s == "WARNING")  return "#f9a825";
    if (s == "CRITICAL") return "#c62828";
    if (s == "INVALID")  return "#6a1b9a";
    return "#616161";
}

class Dashboard : public QMainWindow {
public:
    Dashboard(const QString& host, quint16 port) {
        setWindowTitle("Sensor Hub Dashboard");
        resize(1000, 760);

        auto* central = new QWidget;
        auto* root = new QVBoxLayout(central);
        connLabel_ = new QLabel;
        root->addWidget(connLabel_);
        setConn("Connecting...", false);

        auto* grid = new QGridLayout;
        root->addLayout(grid, 1);

        panels_ = {
            {"temperature", "°C",  10,   50},
            {"humidity",    "%",    0,  100},
            {"pressure",    "hPa", 940, 1060},
            {"light",       "lx",   0, 1100},
        };

        int idx = 0;
        for (auto& p : panels_) {
            auto* box = new QWidget;
            auto* v = new QVBoxLayout(box);
            v->setContentsMargins(0, 0, 0, 0);

            p.header = new QLabel;
            p.header->setAlignment(Qt::AlignCenter);
            p.header->setMinimumHeight(36);
            setHeader(p, p.name + ": waiting...", "WAITING");

            auto* chart = new QChart;
            chart->legend()->hide();
            p.series = new QLineSeries;
            chart->addSeries(p.series);
            p.ax = new QValueAxis;
            p.ax->setRange(0, 60);
            p.ax->setLabelFormat("%d");
            auto* ay = new QValueAxis;
            ay->setRange(p.ymin, p.ymax);
            chart->addAxis(p.ax, Qt::AlignBottom);
            chart->addAxis(ay, Qt::AlignLeft);
            p.series->attachAxis(p.ax);
            p.series->attachAxis(ay);

            auto* view = new QChartView(chart);
            view->setRenderHint(QPainter::Antialiasing);

            v->addWidget(p.header);
            v->addWidget(view, 1);
            grid->addWidget(box, idx / 2, idx % 2);
            ++idx;
        }

        root->addWidget(new QLabel("Alarms:"));
        alarms_ = new QListWidget;
        alarms_->setMaximumHeight(140);
        root->addWidget(alarms_);
        setCentralWidget(central);

        sock_ = new QTcpSocket(this);
        connect(sock_, &QTcpSocket::connected, this, [this] { setConn("Connected", true); });
        connect(sock_, &QTcpSocket::disconnected, this,
                [this] { setConn("Disconnected - retrying...", false); });
        connect(sock_, &QTcpSocket::readyRead, this, [this] {
            while (sock_->canReadLine()) handleLine(sock_->readLine());
        });

        auto* timer = new QTimer(this);               // tự kết nối lại mỗi 2 giây
        connect(timer, &QTimer::timeout, this, [this, host, port] {
            if (sock_->state() == QAbstractSocket::UnconnectedState)
                sock_->connectToHost(host, port);
        });
        timer->start(2000);
        sock_->connectToHost(host, port);
    }

private:
    void setConn(const QString& text, bool ok) {
        connLabel_->setText(text);
        connLabel_->setStyleSheet(QString("padding:6px; color:white; background:%1;")
                                  .arg(ok ? "#2e7d32" : "#c62828"));
    }

    void setHeader(Panel& p, const QString& text, const QString& status) {
        p.header->setText(text);
        p.header->setStyleSheet(QString("font-weight:bold; color:white; background:%1;")
                                .arg(statusColor(status)));
    }

    void handleLine(const QByteArray& line) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
        QJsonObject o = doc.object();

        const QString t      = o.value("t").toString();
        const QString status = o.value("status").toString();
        const double  raw    = o.value("raw").toDouble();
        const double  avg    = o.value("avg").toDouble();

        for (auto& p : panels_) {
            if (p.name != t) continue;
            const bool invalid = (status == "INVALID");

            if (!invalid) {                           // mẫu lỗi không đưa lên biểu đồ
                p.series->append(p.n++, avg);
                if (p.series->count() > 60) p.series->remove(0);
                p.ax->setRange(qMax(0, p.n - 60), qMax(60, p.n));
            }

            const double shown = invalid ? raw : avg;
            const QString text = QString("%1: %2 %3  [%4]")
                                 .arg(p.name).arg(shown, 0, 'f', 1).arg(p.unit).arg(status);
            setHeader(p, text, status);

            if (status != p.lastStatus) {             // ghi alarm khi trạng thái đổi
                if (status != "OK") {
                    alarms_->insertItem(0, QDateTime::currentDateTime().toString("hh:mm:ss  ") + text);
                    while (alarms_->count() > 50) delete alarms_->takeItem(alarms_->count() - 1);
                }
                p.lastStatus = status;
            }
            break;
        }
    }

    QTcpSocket*   sock_ = nullptr;
    QLabel*       connLabel_ = nullptr;
    QListWidget*  alarms_ = nullptr;
    QList<Panel>  panels_;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QString host = argc > 1 ? argv[1] : "127.0.0.1";
    quint16 port = argc > 2 ? QString(argv[2]).toUShort() : 5555;
    Dashboard w(host, port);
    w.show();
    return app.exec();
}
