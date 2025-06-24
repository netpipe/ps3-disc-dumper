// ps3_dumper_full.cpp
#include <QApplication>
#include <QFileDialog>
#include <QTextStream>
#include <QFileInfo>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFile>
#include <QByteArray>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMainWindow>
#include <QTemporaryFile>
#include <QDateTime>
#include <QMap>
#include <QtEndian>

// Read PARAM.SFO metadata
QMap<QString, QString> readSFO(const QString &filePath) {
    QMap<QString, QString> result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return result;
    QByteArray data = file.readAll();

    if (data.mid(0, 4) != "PSF\0") return result;

    quint32 keyTableOffset = qFromLittleEndian<quint32>((uchar*)data.data() + 8);
    quint32 dataTableOffset = qFromLittleEndian<quint32>((uchar*)data.data() + 12);
    quint32 entryCount = qFromLittleEndian<quint32>((uchar*)data.data() + 16);

    for (uint i = 0; i < entryCount; ++i) {
        int entryOffset = 20 + (i * 16);
        quint16 keyOffset = qFromLittleEndian<quint16>((uchar*)data.data() + entryOffset + 0);
        quint16 dataFmt = qFromLittleEndian<quint16>((uchar*)data.data() + entryOffset + 2);
        quint32 dataLen = qFromLittleEndian<quint32>((uchar*)data.data() + entryOffset + 4);
        quint32 dataOffset = qFromLittleEndian<quint32>((uchar*)data.data() + entryOffset + 12);

        QString key = QString::fromUtf8(data.mid(keyTableOffset + keyOffset));
        QByteArray valueRaw = data.mid(dataTableOffset + dataOffset, dataLen);

        if (dataFmt == 0x0400)
            result[key] = QString::fromUtf8(valueRaw.constData());
    }

    return result;
}

// Join split files like .66600, .66601, etc.
QByteArray joinSplitFiles(const QString &baseFilePath) {
    QFileInfo info(baseFilePath);
    QString baseName = info.absoluteFilePath();
    QByteArray joined;
    int index = 0;
    while (true) {
        QString partPath = baseName + QString(".%1").arg(index, 5, 10, QChar('0')).right(6);
        QFile part(partPath);
        if (!part.exists()) break;
        if (!part.open(QIODevice::ReadOnly)) break;
        joined += part.readAll();
        part.close();
        ++index;
    }
    return joined;
}

QString hashFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return "ERROR";

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        hash.addData(file.read(8192));
    }
    return hash.result().toHex();
}

// GUI Table Window
class DumpWindow : public QMainWindow {
public:
    DumpWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        table = new QTableWidget(this);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({"File Path", "SHA-1 Hash"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        setCentralWidget(table);
        resize(800, 600);
    }

    void addRow(const QString &path, const QString &hash) {
        int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(path));
        table->setItem(row, 1, new QTableWidgetItem(hash));
    }

private:
    QTableWidget *table;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QString dirPath = QFileDialog::getExistingDirectory(nullptr, "Select PS3 Disc Root Folder");
    if (dirPath.isEmpty()) return 0;

    QString sfoPath = dirPath + "/PS3_GAME/PARAM.SFO";
    QString titleInfo;
    if (QFile::exists(sfoPath)) {
        auto info = readSFO(sfoPath);
        titleInfo += "Game Title: " + info.value("TITLE", "Unknown") + "\n";
        titleInfo += "Game ID: " + info.value("TITLE_ID", "Unknown") + "\n\n";
    } else {
        titleInfo = "PARAM.SFO not found.\n\n";
    }

    DumpWindow *window = new DumpWindow();
    window->setWindowTitle("PS3 Disc Dumper - Qt Demo");
    window->show();

    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) files << it.next();

    QProgressDialog progress("Hashing files...", "Cancel", 0, files.size());
    progress.setWindowModality(Qt::WindowModal);

    QFile logFile("dump_log_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".txt");
    logFile.open(QIODevice::WriteOnly);
    QTextStream log(&logFile);
    log << titleInfo;

    int i = 0;
    for (const QString &file : files) {
        if (progress.wasCanceled()) break;

        QString relPath = QDir(dirPath).relativeFilePath(file);
        QString hash;

        if (file.endsWith(".66600")) {
            QByteArray joined = joinSplitFiles(file.left(file.size() - 7));
            QCryptographicHash h(QCryptographicHash::Sha1);
            h.addData(joined);
            hash = h.result().toHex();
        } else {
            hash = hashFile(file);
        }

        log << relPath << ": " << hash << "\n";
        window->addRow(relPath, hash);

        progress.setValue(++i);
    }

    logFile.close();
    return app.exec();
}
