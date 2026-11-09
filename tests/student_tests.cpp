#include "aiws/gui/main_window.hpp"

#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QLabel>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdint>
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"

#ifdef __unix__
#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace{
bool has_label(const aiws::gui::MainWindow& window, const QString& part){
    for(auto* label : window.findChildren<QLabel*>()){
        if(label->text().contains(part)){
            return true;
        }
    }
    return false;
}

bool put_file(const QString& path, const QByteArray& bytes){
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QPushButton* button(const aiws::gui::MainWindow& window, const QString& text){
    for(auto* candidate : window.findChildren<QPushButton*>()){
        if(candidate->text() == text){
            return candidate;
        }
    }
    return nullptr;
}

#ifdef __unix__
struct Socket{
    int fd{-1};
    Socket() = default;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket(){
        if(fd >= 0){
            ::close(fd);
        }
    }
    bool connect_to(const QString& path){
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if(fd < 0){
            return false;
        }
        timeval timeout{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto bytes = path.toUtf8();
        if(static_cast<std::size_t>(bytes.size()) >= sizeof(address.sun_path)){
            return false;
        }
        std::memcpy(address.sun_path, bytes.constData(), static_cast<std::size_t>(bytes.size()) + 1);
        return ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    }
};

QByteArray request_for(const QByteArray& path){
    const std::uint32_t length = htonl(static_cast<std::uint32_t>(path.size()));
    QByteArray request(reinterpret_cast<const char*>(&length), sizeof(length));
    return request + path;
}

bool write_bytes(int fd, const QByteArray& bytes){
    return ::send(fd, bytes.constData(), static_cast<std::size_t>(bytes.size()), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(bytes.size());
}
#endif
}

class M4Tests : public QObject{
    Q_OBJECT
private slots:
    void successful_background_build(){
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath("first.txt");
        QVERIFY(put_file(file, "alpha beta gamma"));
        aiws::gui::MainWindow window;
        QSignalSpy finished(&window, &aiws::gui::MainWindow::build_finished);
        QVERIFY(window.add_document_file(file));
        window.set_build_delay_ms(100);
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QVERIFY(has_label(window, "Building"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(has_label(window, "Corpus built."));
        QVERIFY(has_label(window, "Corpus: 1 chunks"));

        // Check search, context and term inspection after the build.
        auto* search = window.findChild<aiws::gui::SearchWidget*>();
        QVERIFY(search);
        search->findChild<QLineEdit*>()->setText("alpha");
        QVERIFY(button(window, "Search")->isEnabled());
        button(window, "Search")->click();
        QCOMPARE(search->findChild<QTableView*>()->model()->rowCount(), 1);
        button(window, "Build Context")->click();
        auto* context = window.findChild<aiws::gui::ContextWidget*>();
        QVERIFY(context->findChild<QPlainTextEdit*>()->toPlainText().contains("alpha"));
        auto* inspector = window.findChild<aiws::gui::TermInspectorWidget*>();
        inspector->findChild<QLineEdit*>()->setText("alpha");
        button(window, "Inspect")->click();
        QVERIFY(has_label(window, "Term inspected."));
    }

    void stale_build_cannot_become_current(){
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString first = dir.filePath("first.txt");
        const QString second = dir.filePath("second.txt");
        QVERIFY(put_file(first, "alpha beta"));
        QVERIFY(put_file(second, "another document"));
        aiws::gui::MainWindow window;
        QSignalSpy finished(&window, &aiws::gui::MainWindow::build_finished);
        window.set_build_delay_ms(400);
        QVERIFY(window.add_document_file(first));
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QVERIFY(window.add_document_file(second));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(!has_label(window, "Corpus built."));
        QVERIFY(has_label(window, "Not built"));
        QVERIFY(!button(window, "Inspect")->isEnabled());
        QVERIFY(button(window, "Build Corpus")->isEnabled());
    }

    void build_failure_is_reported(){
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath("dup.txt");
        QVERIFY(put_file(file, "repeat"));
        aiws::gui::MainWindow window;
        // Duplicate document IDs trigger ProcessingCore's build exception.
        QVERIFY(window.add_document_file(file));
        QVERIFY(window.add_document_file(file));
        QVERIFY(QMetaObject::invokeMethod(&window, "build_corpus", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(has_label(window, "Corpus: Failed"), 3000);
        QVERIFY(button(window, "Build Corpus")->isEnabled());
        QVERIFY(!button(window, "Inspect")->isEnabled());
        button(window, "Reset")->click();
        QVERIFY(window.add_document_file(file));
        button(window, "Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(has_label(window, "Corpus built."), 3000);
    }

    void reset_during_build_rejects_old_completion(){
        QTemporaryDir dir;
        const auto path = dir.filePath("reset.txt");
        QVERIFY(put_file(path, "alpha beta"));
        aiws::gui::MainWindow window;
        QSignalSpy finished(&window, &aiws::gui::MainWindow::build_finished);
        window.set_build_delay_ms(300);
        QVERIFY(window.add_document_file(path));
        button(window, "Build Corpus")->click();
        button(window, "Reset")->click();
        QCOMPARE(window.findChild<QListWidget*>()->count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(has_label(window, "Corpus: Not built"));
        QVERIFY(!button(window, "Inspect")->isEnabled());
        QVERIFY(!button(window, "Build Corpus")->isEnabled());
    }

    void unreadable_add_changes_nothing(){
        QTemporaryDir dir;
        const auto path = dir.filePath("valid.txt");
        QVERIFY(put_file(path, "alpha"));
        aiws::gui::MainWindow window;
        QVERIFY(window.add_document_file(path));
        button(window, "Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(has_label(window, "Corpus built."), 3000);
        QVERIFY(!window.add_document_file(dir.filePath("missing.txt")));
        QVERIFY(!window.add_document_file(dir.path()));
        QCOMPARE(window.findChild<QListWidget*>()->count(), 1);
        QVERIFY(has_label(window, "Corpus built."));
        QVERIFY(button(window, "Inspect")->isEnabled());
    }

    void gui_events_continue_during_build(){
        QTemporaryDir dir;
        const auto path = dir.filePath("slow.txt");
        QVERIFY(put_file(path, "alpha"));
        aiws::gui::MainWindow window;
        QVERIFY(window.add_document_file(path));
        window.set_build_delay_ms(1000);
        QSignalSpy finished(&window, &aiws::gui::MainWindow::build_finished);
        QTimer heartbeat;
        heartbeat.setInterval(5);
        QSignalSpy ticks(&heartbeat, &QTimer::timeout);
        heartbeat.start();
        button(window, "Build Corpus")->click();
        QTRY_VERIFY_WITH_TIMEOUT(ticks.count() >= 2, 300);
        QCOMPARE(finished.count(), 0);
        QVERIFY(button(window, "Reset")->isEnabled());
        QVERIFY(button(window, "Add Document")->isEnabled());
        QVERIFY(button(window, "Exit")->isEnabled());
        QVERIFY(!button(window, "Build Corpus")->isEnabled());
        window.findChild<QListWidget*>()->setCurrentRow(0);
        auto* search = window.findChild<aiws::gui::SearchWidget*>();
        search->findChild<QLineEdit*>()->setText("edited while building");
        QCOMPARE(search->query(), QString("edited while building"));
        button(window, "Reset")->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
    }

    void repeated_builds_and_close_during_build(){
        QTemporaryDir dir;
        const auto path = dir.filePath("repeat.txt");
        QVERIFY(put_file(path, "alpha beta"));
        aiws::gui::MainWindow window;
        QVERIFY(window.add_document_file(path));
        QSignalSpy finished(&window, &aiws::gui::MainWindow::build_finished);
        for(int i = 1; i <= 4; ++i){
            button(window, "Build Corpus")->click();
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), i, 3000);
            QVERIFY(has_label(window, "Corpus built."));
        }
        window.show();
        window.set_build_delay_ms(1000);
        button(window, "Build Corpus")->click();
        window.close();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 3000);
    }

#ifdef __unix__
    void remote_add_paths_rejections_and_sender_directory(){
        QTemporaryDir dir("/tmp/aiws-test-XXXXXX");
        QVERIFY(dir.isValid());
        const auto socket = dir.filePath("ipc.sock");
        const auto name = QString::fromUtf8("space caf\xc3\xa9\nfile.txt");
        QVERIFY(put_file(dir.filePath(name), "remote alpha"));
        aiws::gui::MainWindow window;
        QVERIFY(window.start_ipc_server(socket));
        QProcess sender;
        sender.setWorkingDirectory(dir.path());
        const auto program = QCoreApplication::applicationDirPath() + "/aiws_send";
        sender.start(program, {"--socket", socket, "add", name});
        // The reply must arrive even while the GUI is waiting.
        QVERIFY(sender.waitForFinished(2500));
        QCOMPARE(sender.exitStatus(), QProcess::NormalExit);
        QCOMPARE(sender.exitCode(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QListWidget*>()->count(), 1, 1000);
        QCOMPARE(window.findChild<QListWidget*>()->item(0)->text(), name);
        sender.start(program, {"--socket", socket, "add", "missing.txt"});
        QVERIFY(sender.waitForFinished(2500));
        QCOMPARE(sender.exitCode(), 1);
        QVERIFY(!sender.readAllStandardError().isEmpty());
        QCOMPARE(window.findChild<QListWidget*>()->count(), 1);
        sender.start(program, {"--socket", dir.filePath("absent.sock"), "add", name});
        QVERIFY(sender.waitForFinished(2500));
        QCOMPARE(sender.exitCode(), 2);
    }

    void fragmented_remote_add_invalidates_running_build(){
        QTemporaryDir dir("/tmp/aiws-test-XXXXXX");
        const auto socket = dir.filePath("ipc.sock");
        const auto first = dir.filePath("first.txt");
        const auto second = dir.filePath("second.txt");
        QVERIFY(put_file(first, "alpha"));
        QVERIFY(put_file(second, "beta"));
        aiws::gui::MainWindow window;
        QVERIFY(window.add_document_file(first));
        QVERIFY(window.start_ipc_server(socket));
        window.set_build_delay_ms(600);
        QSignalSpy finished(&window, &aiws::gui::MainWindow::build_finished);
        button(window, "Build Corpus")->click();
        Socket client;
        QVERIFY(client.connect_to(socket));
        const auto request = request_for(second.toUtf8());
        QVERIFY(write_bytes(client.fd, request.left(2)));
        QTest::qWait(15);
        QVERIFY(write_bytes(client.fd, request.mid(2, 3)));
        QTest::qWait(15);
        QCOMPARE(window.findChild<QListWidget*>()->count(), 1);
        QVERIFY(write_bytes(client.fd, request.mid(5)));
        char reply = 0;
        QCOMPARE(::recv(client.fd, &reply, 1, 0), ssize_t(1));
        QCOMPARE(reply, '0');
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QListWidget*>()->count(), 2, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(has_label(window, "Corpus: Not built"));
        QVERIFY(!button(window, "Inspect")->isEnabled());
    }

    void server_shutdown_with_partial_client_and_active_build(){
        QTemporaryDir dir("/tmp/aiws-test-XXXXXX");
        const auto socket = dir.filePath("ipc.sock");
        const auto path = dir.filePath("close.txt");
        QVERIFY(put_file(path, "alpha"));
        aiws::gui::MainWindow window;
        window.show();
        QVERIFY(window.start_ipc_server(socket));
        QVERIFY(window.add_document_file(path));
        window.set_build_delay_ms(1000);
        button(window, "Build Corpus")->click();
        Socket client;
        QVERIFY(client.connect_to(socket));
        QVERIFY(write_bytes(client.fd, QByteArray(1, '\0')));
        QElapsedTimer elapsed;
        elapsed.start();
        window.close();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 450);
        QVERIFY(elapsed.elapsed() < 500);
        QVERIFY(!QFileInfo::exists(socket));
    }

    void disconnected_client_does_not_stop_server(){
        QTemporaryDir dir("/tmp/aiws-test-XXXXXX");
        QVERIFY(dir.isValid());
        const auto socket_path = dir.filePath("ipc.sock");
        const auto document_path = dir.filePath("valid.txt");
        QVERIFY(put_file(document_path, "alpha beta"));

        aiws::gui::MainWindow window;
        QVERIFY(window.start_ipc_server(socket_path));
        {
            Socket client;
            QVERIFY(client.connect_to(socket_path));
            const auto request = request_for(document_path.toUtf8());
            QVERIFY(write_bytes(client.fd, request.left(request.size() - 1)));
        }
        QProcess sender;
        const auto program = QCoreApplication::applicationDirPath() + "/aiws_send";
        sender.start(program, {"--socket", socket_path, "add", document_path});
        QVERIFY(sender.waitForFinished(3000));
        QCOMPARE(sender.exitStatus(), QProcess::NormalExit);
        QCOMPARE(sender.exitCode(), 0);
        auto* documents = window.findChild<QListWidget*>();
        QVERIFY(documents);
        QTRY_COMPARE_WITH_TIMEOUT(documents->count(), 1, 1000);
        QCOMPARE(documents->item(0)->text(), QString("valid.txt"));
    }

    void malformed_request_and_socket_collision_do_not_break_server(){
        QTemporaryDir dir("/tmp/aiws-test-XXXXXX");
        const auto socket = dir.filePath("ipc.sock");
        aiws::gui::MainWindow window;
        QVERIFY(window.start_ipc_server(socket));
        aiws::gui::MainWindow other;
        QVERIFY(!other.start_ipc_server(socket));
        QVERIFY(QFileInfo::exists(socket));
        Socket client;
        QVERIFY(client.connect_to(socket));
        QVERIFY(write_bytes(client.fd, request_for(QByteArray("bad\0path", 8))));
        char reply = 0;
        QCOMPARE(::recv(client.fd, &reply, 1, 0), ssize_t(1));
        QCOMPARE(reply, '1');
        QCOMPARE(window.findChild<QListWidget*>()->count(), 0);
        const auto regular = dir.filePath("keep.txt");
        QVERIFY(put_file(regular, "keep"));
        QVERIFY(!other.start_ipc_server(regular));
        QFile file(regular);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("keep"));
    }

#endif
};

QTEST_MAIN(M4Tests)
#include "student_tests.moc"
