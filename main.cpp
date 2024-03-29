#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QThread>
#include <QRandomGenerator>
#include <QTime>
#include <QRegularExpression>
#include <QElapsedTimer>

QElapsedTimer timer;

struct Task {
    int dest_id;
    int timeout_ms;
    QString payload;
    int count;
};

class Node : public QObject {
    Q_OBJECT
public:
    explicit Node(int id, const QList<Task>& tasks, double errorRate)
        : id(id), tasks(tasks), error_rate(errorRate) {}

public slots:
    void executeTasks() {
        for (const Task& task : tasks) {
            for (int i = 0; i < task.count; ++i) {
                QThread::msleep(task.timeout_ms);

                if (QRandomGenerator::global()->generateDouble() < error_rate) {
                    qDebug() << "Error occurred while sending packet from" << id << "to" << task.dest_id;
                    continue;
                }

                QString packet = generatePacket(task.payload);
                emit packetSent(id, task.dest_id, packet);
            }
        }
    }

signals:
    void packetSent(int node_id, int dest_id, const QString& packet);

private:
    int id;
    QList<Task> tasks;
    double error_rate;

    QString generatePacket(const QString& payload) {
        qint64 elapsedTime = timer.elapsed();
        return QString("[%1.%2] RandomValue: %3 - Payload: %4")
            .arg(elapsedTime / 1000, 3, 10, QChar('0'))
            .arg(elapsedTime % 1000, 3, 10, QChar('0'))
            .arg(qAbs(QRandomGenerator::global()->generate()))
            .arg(payload);
    }
};

class NetworkPlayer : public QObject {
    Q_OBJECT
public:
    explicit NetworkPlayer(const QString& filename) {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open file:" << filename;
            return;
        }

        QByteArray jsonData = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isNull()) {
            qWarning() << "Failed to parse JSON data";
            return;
        }

        QJsonObject rootObj = doc.object();
        double commonErrorRate = rootObj["common"].toObject()["error_rate"].toDouble();

        QJsonArray nodesArray = rootObj["nodes"].toArray();
        for (const auto& nodeData : nodesArray) {
            QJsonObject nodeObj = nodeData.toObject();
            int nodeId = nodeObj["id"].toInt();
            QList<Task> tasks;

            QJsonArray tasksArray = nodeObj["tasks"].toArray();
            for (const auto& taskData : tasksArray) {
                QJsonObject taskObj = taskData.toObject();
                Task task;
                task.dest_id = taskObj["dest_id"].toInt();
                task.timeout_ms = taskObj["timeout_ms"].toInt();
                task.payload = taskObj["payload"].toString();
                task.count = taskObj["count"].toInt();
                tasks.append(task);
            }

            Node* node = new Node(nodeId, tasks, commonErrorRate);
            nodes.append(node);
            connect(node, &Node::packetSent, this, &NetworkPlayer::handlePacket, Qt::QueuedConnection);
        }
    }

    void run() {
        for (Node* node : nodes) {
            QThread* thread = new QThread;
            node->moveToThread(thread);
            connect(thread, &QThread::started, node, &Node::executeTasks);
            thread->start();
        }
    }

    QList<Node*> getNodes() const {
        return nodes;
    }

public slots:
    void handlePacket(int node_id, int src_id, const QString& packet) {
        QRegularExpression rx("\\[(\\d+)\\.(\\d{3})\\] RandomValue: (.+) - Payload: (.+)");
        QRegularExpressionMatch match = rx.match(packet);
        if (match.hasMatch()) {
            QString elapsedTimeStr = match.captured(1) + "." + match.captured(2);
            QString randomValueStr = match.captured(3);
            QString payload = match.captured(4);

            qDebug().noquote() << QString("[%1]:(%2) Message from %3 - '%4'")
                                      .arg(elapsedTimeStr)
                                      .arg(node_id)
                                      .arg(src_id)
                                      .arg(payload);
        } else {
            qDebug() << "Error: Invalid packet format" << packet;
        }
    }

private:
    QList<Node*> nodes;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    timer.start();

    if (argc != 2) {
        qWarning() << "Usage: " << argv[0] << " <input_json_file>";
        return 1;
    }

    QString filename = argv[1];
    NetworkPlayer player(filename);
    player.run();

    return app.exec();
}

#include "main.moc"


